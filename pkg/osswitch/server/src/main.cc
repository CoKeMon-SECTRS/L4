/*
 * (c) 2008-2009 Adam Lackorzynski <adam@os.inf.tu-dresden.de>,
 *               Frank Mehnert <fm3@os.inf.tu-dresden.de>,
 *               Lukas Grützmacher <lg2@os.inf.tu-dresden.de>
 *     economic rights: Technische Universität Dresden (Germany)
 *
 * License: see LICENSE.spdx (in this directory or the directories above)
 */
#include <stdint.h>
#include <string.h>

#include <unistd.h>
#include <stdio.h>
#include <l4/sys/arm_smccc>
#include <l4/re/env>
#include <l4/sigma0/sigma0.h>

#include <l4/l4virtio/client/virtio-block>
#include <l4/util/util.h>

#define PAGE_SIZE 0x1000
#define BUFF_ALLOCATE PAGE_SIZE
#define PAGE_MASK (~(PAGE_SIZE - 1))

#define SETUP 2
#define LOGGING 3

#define SECTOR_SIZE 512
#define BLK_USERMEM (64 * 1024)

static L4::Cap<L4::Kobject> server;

static L4virtio::Driver::Block_device blk;
static void *blk_buf = nullptr;
static L4virtio::Ptr<void> blk_devaddr(0);
static l4_uint64_t blk_sector = 0;
static bool blk_ready = false;

// PCI ECAM save/restore for SDHCI (bus 0, dev 3, func 0)
// ECAM base from DTS pcie@10000000 reg: <0x40 0x10000000 ...>
#define ECAM_SDHCI_PHYS  0x4010018000ULL
#define ECAM_SDHCI_VIRT  0x90000000UL
static volatile uint32_t *pci_cfg = nullptr;
static uint32_t saved_pci[16];

// SMMUv3 full state save/restore across OS switch
#define SMMU_PHYS          0x9050000ULL
#define SMMU_VIRT          0x91000000UL
#define SMMU_CR0           (0x20 / 4)
#define SMMU_CR0ACK        (0x24 / 4)
#define SMMU_CR1           (0x28 / 4)
#define SMMU_CR2           (0x2C / 4)
#define SMMU_GBPA          (0x44 / 4)
#define SMMU_IRQ_CTRL      (0x50 / 4)
#define SMMU_STRTAB_BASE_LO  (0x80 / 4)
#define SMMU_STRTAB_BASE_HI  (0x84 / 4)
#define SMMU_STRTAB_BASE_CFG (0x88 / 4)
#define SMMU_CMDQ_BASE_LO (0x90 / 4)
#define SMMU_CMDQ_BASE_HI (0x94 / 4)
#define SMMU_CMDQ_PROD     (0x98 / 4)
#define SMMU_CMDQ_CONS     (0x9C / 4)
#define SMMU_EVENTQ_BASE_LO (0xA0 / 4)
#define SMMU_EVENTQ_BASE_HI (0xA4 / 4)
static volatile uint32_t *smmu = nullptr;
struct smmu_state {
    uint32_t cr0, cr1, cr2, gbpa, irq_ctrl;
    uint32_t strtab_base_lo, strtab_base_hi, strtab_base_cfg;
    uint32_t cmdq_base_lo, cmdq_base_hi, cmdq_prod, cmdq_cons;
    uint32_t eventq_base_lo, eventq_base_hi;
};
static struct smmu_state saved_smmu;

static int blk_init()
{
    auto cap = L4Re::Env::env()->get_cap<L4virtio::Device>("blk");
    if (!cap.is_valid())
      {
        printf("osswitch: block device cap 'blk' not found, disk logging disabled.\n");
        return -1;
      }

    printf("osswitch: waiting for eMMC driver to initialize...\n");
    l4_sleep(2000);

    printf("osswitch: connecting to block device...\n");
    try
      {
        blk.setup_device(cap, BLK_USERMEM, &blk_buf, blk_devaddr);
      }
    catch (L4::Runtime_error const &e)
      {
        printf("osswitch: block device setup failed: %s: %s\n", e.str(), e.extra_str());
        return -1;
      }
    printf("osswitch: block device connected\n");

    auto const &cfg = blk.device_config();
    printf("osswitch: block device ready, capacity=%llu sectors (%llu MB)\n",
           (unsigned long long)cfg.capacity,
           (unsigned long long)(cfg.capacity * SECTOR_SIZE / (1024 * 1024)));

    blk_ready = true;
    return 0;
}

static int blk_write(const char *data, int len)
{
    if (!blk_ready || len <= 0)
        return -1;

    int written = 0;
    while (written < len)
      {
        int chunk = len - written;
        if (chunk > BLK_USERMEM)
            chunk = BLK_USERMEM;

        memcpy(blk_buf, data + written, chunk);
        int padded = (chunk + SECTOR_SIZE - 1) & ~(SECTOR_SIZE - 1);
        if (padded > chunk)
            memset((char *)blk_buf + chunk, 0, padded - chunk);

        auto handle = blk.start_request(blk_sector, L4VIRTIO_BLOCK_T_OUT, nullptr);
        if (!handle.valid())
          {
            printf("osswitch: blk start_request failed\n");
            return -1;
          }

        int ret = blk.add_block(handle, blk_devaddr, padded);
        if (ret < 0)
          {
            printf("osswitch: blk add_block failed: %d\n", ret);
            blk.free_request(handle);
            return -1;
          }

        ret = blk.process_request(handle);
        if (ret < 0)
          {
            printf("osswitch: blk write failed: %d\n", ret);
            return -1;
          }

        blk_sector += padded / SECTOR_SIZE;
        written += chunk;
      }

    return 0;
}

static int pci_map_ecam()
{
    long r = l4sigma0_map_iomem(server.cap(), ECAM_SDHCI_PHYS,
                                ECAM_SDHCI_VIRT, PAGE_SIZE, 0);
    if (r)
      {
        printf("osswitch: failed to map PCI ECAM: %ld\n", r);
        return -1;
      }
    pci_cfg = (volatile uint32_t *)ECAM_SDHCI_VIRT;
    printf("osswitch: PCI ECAM mapped for SDHCI (0000:00:03.0)\n");
    return 0;
}

static void pci_save_config()
{
    if (!pci_cfg) return;
    for (int i = 0; i < 16; i++)
        saved_pci[i] = pci_cfg[i];
    printf("osswitch: PCI saved: cmd=0x%x BAR0=0x%x BAR1=0x%x\n",
           saved_pci[1] & 0xffff, saved_pci[4], saved_pci[5]);
}

static void pci_restore_config()
{
    if (!pci_cfg) return;
    for (int i = 4; i <= 9; i++)
        pci_cfg[i] = saved_pci[i];
    pci_cfg[1] = saved_pci[1];
    pci_cfg[15] = saved_pci[15];
    printf("osswitch: PCI restored: cmd=0x%x BAR0=0x%x\n",
           pci_cfg[1] & 0xffff, pci_cfg[4]);
}

static int smmu_map()
{
    long r = l4sigma0_map_iomem(server.cap(), SMMU_PHYS,
                                SMMU_VIRT, PAGE_SIZE, 0);
    if (r)
      {
        printf("osswitch: failed to map SMMU: %ld\n", r);
        return -1;
      }
    smmu = (volatile uint32_t *)SMMU_VIRT;
    printf("osswitch: SMMU mapped, CR0=0x%x\n", smmu[SMMU_CR0]);
    return 0;
}

static void smmu_save()
{
    if (!smmu) return;
    saved_smmu.cr0 = smmu[SMMU_CR0];
    saved_smmu.cr1 = smmu[SMMU_CR1];
    saved_smmu.cr2 = smmu[SMMU_CR2];
    saved_smmu.gbpa = smmu[SMMU_GBPA];
    saved_smmu.irq_ctrl = smmu[SMMU_IRQ_CTRL];
    saved_smmu.strtab_base_lo = smmu[SMMU_STRTAB_BASE_LO];
    saved_smmu.strtab_base_hi = smmu[SMMU_STRTAB_BASE_HI];
    saved_smmu.strtab_base_cfg = smmu[SMMU_STRTAB_BASE_CFG];
    saved_smmu.cmdq_base_lo = smmu[SMMU_CMDQ_BASE_LO];
    saved_smmu.cmdq_base_hi = smmu[SMMU_CMDQ_BASE_HI];
    saved_smmu.cmdq_prod = smmu[SMMU_CMDQ_PROD];
    saved_smmu.cmdq_cons = smmu[SMMU_CMDQ_CONS];
    saved_smmu.eventq_base_lo = smmu[SMMU_EVENTQ_BASE_LO];
    saved_smmu.eventq_base_hi = smmu[SMMU_EVENTQ_BASE_HI];
    printf("osswitch: SMMU saved: CR0=0x%x STRTAB=0x%x%08x\n",
           saved_smmu.cr0, saved_smmu.strtab_base_hi,
           saved_smmu.strtab_base_lo);
}

static void smmu_restore()
{
    if (!smmu) return;
    // 1. Disable SMMU
    smmu[SMMU_CR0] = 0;
    for (int i = 0; i < 1000; i++)
        if (smmu[SMMU_CR0ACK] == 0) break;
    // 2. Restore stream table and queue bases
    smmu[SMMU_STRTAB_BASE_LO] = saved_smmu.strtab_base_lo;
    smmu[SMMU_STRTAB_BASE_HI] = saved_smmu.strtab_base_hi;
    smmu[SMMU_STRTAB_BASE_CFG] = saved_smmu.strtab_base_cfg;
    smmu[SMMU_CMDQ_BASE_LO] = saved_smmu.cmdq_base_lo;
    smmu[SMMU_CMDQ_BASE_HI] = saved_smmu.cmdq_base_hi;
    smmu[SMMU_CMDQ_PROD] = 0;
    smmu[SMMU_CMDQ_CONS] = 0;
    smmu[SMMU_EVENTQ_BASE_LO] = saved_smmu.eventq_base_lo;
    smmu[SMMU_EVENTQ_BASE_HI] = saved_smmu.eventq_base_hi;
    // 3. Restore control registers
    smmu[SMMU_CR1] = saved_smmu.cr1;
    smmu[SMMU_CR2] = saved_smmu.cr2;
    smmu[SMMU_IRQ_CTRL] = saved_smmu.irq_ctrl;
    smmu[SMMU_GBPA] = saved_smmu.gbpa | (1u << 31);
    // 4. Re-enable SMMU
    smmu[SMMU_CR0] = saved_smmu.cr0;
    for (int i = 0; i < 1000; i++)
        if (smmu[SMMU_CR0ACK] == saved_smmu.cr0) break;
    printf("osswitch: SMMU restored: CR0=0x%x ACK=0x%x STRTAB=0x%x%08x\n",
           smmu[SMMU_CR0], smmu[SMMU_CR0ACK],
           smmu[SMMU_STRTAB_BASE_HI], smmu[SMMU_STRTAB_BASE_LO]);
}

static int get_sigma_caps()
{
    server = L4Re::Env::env()->get_cap<L4::Kobject>("sigma0");
    if (!server.is_valid())
      {
        printf("osswitch: could not get sigma0 capability\n");
        return -1;
      }
    return 0;
}


int
main()
{
    printf("osswitch: starting up\n");

    auto smc = L4Re::Env::env()->get_cap<L4::Arm_smccc>("arm_smc");
    if (!smc)
      {
        printf("osswitch: SMC capability 'arm_smc' not found\n");
        return -1;
      }
    l4_umword_t out[4] = {};

    if (get_sigma_caps() != 0)
        return -1;

    pci_map_ecam();
    smmu_map();

    if (blk_init() != 0)
        printf("osswitch: continuing without disk logging\n");

    pci_save_config();
    smmu_save();
    printf("osswitch: switching to Linux (SETUP)\n");
    long ret = l4_error(smc->call(0xc4000171, 0, 0, SETUP, 0, 0, 0, out, out + 1, out + 2, out + 3, 0));
    if (ret<0){
        printf("Error while calling osswitch\n");
    }
    pci_restore_config();
    smmu_restore();

    l4_addr_t buff_linux = (*out << 32) | *(out+1);
    int len_buf = *(out+2);
    l4_addr_t buff_linux_down = buff_linux & PAGE_MASK;
    l4_addr_t virt_buff_linux = 0x80000000;
    int buff_allocate = BUFF_ALLOCATE;
    l4_addr_t buff_linux_up = buff_linux_down + buff_allocate;
    int64_t offset = buff_linux - buff_linux_down;
    if(buff_linux_down+offset+len_buf>buff_linux_up){
        printf("WARN: Buffer allocation is too small, doubling it\n");
        buff_allocate = 2*buff_allocate;
        buff_linux_up = buff_linux_down + buff_allocate;
    }
    long r = l4sigma0_map_iomem(server.cap(), buff_linux_down, virt_buff_linux, buff_allocate, 0);
    if (r)
    {
        if (r == -L4SIGMA0_NOTALIGNED)
            return -L4_EINVAL;
        if (r == -L4SIGMA0_IPCERROR)
            return -L4_ENODEV;
        return -L4_ENOMEM;
    }
    write(STDOUT_FILENO, (char *)virt_buff_linux+offset, len_buf);
    blk_write((char *)virt_buff_linux+offset, len_buf);
    while(1){
        ret = l4_error(smc->call(0xc4000171, 0, 0, LOGGING, 0, 0, 0, out, out + 1, out + 2, out + 3, 0));
        if (ret<0){
            printf("Error while calling osswitch\n");
        }
        pci_restore_config();
        smmu_restore();
        buff_linux = (*out << 32) | *(out+1);
        len_buf = *(out+2);
        offset = buff_linux - (buff_linux & PAGE_MASK);
        if( buff_linux < buff_linux_down || buff_linux + len_buf > buff_linux_up){
            buff_linux_down = buff_linux & PAGE_MASK;
            buff_allocate = BUFF_ALLOCATE;
            buff_linux_up = buff_linux_down + buff_allocate;
            if(buff_linux_down+offset+len_buf>buff_linux_up){
                printf("WARN: Buffer allocation is too small, doubling it\n");
                buff_allocate = 2*buff_allocate;
                buff_linux_up = buff_linux_down + buff_allocate;
            }
            long r = l4sigma0_map_iomem(server.cap(), buff_linux_down, virt_buff_linux, buff_allocate, 0);
            if (r)
            {
                if (r == -L4SIGMA0_NOTALIGNED)
                    return -L4_EINVAL;
                if (r == -L4SIGMA0_IPCERROR)
                    return -L4_ENODEV;
                return -L4_ENOMEM;
            }
        }
        write(STDOUT_FILENO, (char *)virt_buff_linux+offset, len_buf);
        blk_write((char *)virt_buff_linux+offset, len_buf);
    }
    return 0;
}
