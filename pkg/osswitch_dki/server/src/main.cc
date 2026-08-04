#include <cstddef>
#include <stdint.h>
#include <string.h> // TO BE REMOVED - USELESS

#include <unistd.h>
#include <stdio.h>
#include <l4/sys/arm_smccc>
#include <l4/re/env>
#include <l4/sigma0/sigma0.h>

#include "l4/sys/l4int.h"
#include "sha256.h"

#define PAGE_SIZE 0x1000
#define BUFF_ALLOCATE PAGE_SIZE
#define PAGE_MASK (~(PAGE_SIZE - 1))

#define PTE_RDONLY (1 << 7)
#define PTE_DBM (1<<51) /* Dirty Bit Management */

#define SYSTEMREG 0
#define PAGETABLE 1
#define SETUP 2

// Values for QEMU
// #define TOTAL_SIZE 0x2CAE200
// #define PHYS_ADDR 0x80000000
// #define LINUX_SPACE_SIZE 0x2CAE200
// #define PHYS_ADDR_DOWN 0x80000000
// const char true_hash[32] = {0xD5,0xE6,0x7D,0xC0,0x5D,0xC4,0x7E,0x48,0xF7,0x91,0x14,0x6C,0xAB,0x83,0xBA,0xB4,0x6E,0xCC,0x18,0xEA,0xD6,0x8C,0xA6,0x4A,0x6E,0x6F,0xA9,0xDB,0x15,0x1A,0x9B,0x56};

// Values for Rock5b
#define TOTAL_SIZE 0x315BA00
#define PHYS_ADDR 0x12400000
#define PHYS_ADDR_DOWN 0xecbd4000
#define LINUX_SPACE_SIZE 0x342C000
const char true_hash[32] = {0x8E,0x3C,0xA9,0x4F,0x8E,0xB6,0x99,0xA4,0x5A,0xEE,0x79,0x1D,0xDC,0xE7,0x7B,0x3C,0xF6,0x3E,0xA1,0x6F,0x5D,0xE2,0x46,0x24,0x41,0x6E,0x38,0x28,0x40,0xEF,0x71,0xF6};
L4::Cap<L4::Kobject> server;

int get_sigma_caps(){
    server = L4Re::Env::env()->get_cap<L4::Kobject>("sigma0");
    if (!server.is_valid())
    {
        printf("Could not get sigma capability!\n");
        return -1;
    }
    // printf("Sigma0 cap issued\n");
    return 0;
}

l4_addr_t map_kernel_space(){
    l4_addr_t phys = PHYS_ADDR_DOWN;
    l4_addr_t virt = PHYS_ADDR_DOWN;
    // l4_addr_t virt = 0x80000000;
    const unsigned long total_size = LINUX_SPACE_SIZE;

    int r = l4sigma0_map_iomem(server.cap(), phys, virt, total_size, 1);
    if(r!=L4SIGMA0_OK){
        printf("%s\n",l4sigma0_map_errstr(r));
        return 0;
    }
    return virt;
}

l4_addr_t map_kernel_code(){
    l4_addr_t phys = PHYS_ADDR;
    l4_addr_t virt = PHYS_ADDR;
    // l4_addr_t virt = 0x80000000;
    // const unsigned long total_size = TOTAL_SIZE;
    const unsigned long total_size = (TOTAL_SIZE % PAGE_SIZE != 0) ? TOTAL_SIZE + PAGE_SIZE - (TOTAL_SIZE % PAGE_SIZE) : TOTAL_SIZE;

    int r = l4sigma0_map_iomem(server.cap(), phys, virt, total_size, 1);
    if(r!=L4SIGMA0_OK){
        printf("%s\n",l4sigma0_map_errstr(r));
        return 0;
    }
    return virt;
}

int kernel_check(){
    /* initial state */
    uint32_t state[8] = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
    };

    // l4_addr_t phys = PHYS_ADDR;
    l4_addr_t virt = PHYS_ADDR;
    // l4_addr_t virt = 0x80000000;
    const unsigned long total_size = TOTAL_SIZE;

    uint64_t total_size_bit = total_size*8;
    // const unsigned long size = 0x1000;
    const unsigned long size = 0x100000;
    int chunks = (total_size / size) + 1;
    // printf("Chunks = %d\n",chunks);
    for(int i=0;i<chunks;i++){
        // printf("Chunks %d: start\n",i);
        // long r = l4sigma0_map_iomem(server.cap(), phys, virt, size, 1);
        // if (r)
        // {
        //     if (r == -L4SIGMA0_NOTALIGNED)
        //         return -L4_EINVAL;
        //     if (r == -L4SIGMA0_IPCERROR)
        //         return -L4_ENODEV;
        //     return -L4_ENOMEM;
        // }
        // printf("Chunks %d: Memory mapped\n",i);
        // printf("Chunks %d: virt = %lx\n",i,virt);
        // printf("Chunks %d: First bytes are %02X%02X\n",i,((uint8_t *)virt)[0],((uint8_t *)virt)[1]);
        // printf("Chunks %d: Last bytes are %02X%02X\n",i,((uint8_t *)virt)[size-2],((uint8_t *)virt)[size-1]);

        if(i!=chunks-1){
            sha256_process(state, (uint8_t *)virt, size);
        }else{
            const uint64_t remainder = total_size % size;
            if (remainder == 0 || remainder < sizeof(uint64_t)){
                printf("PANIC: CASE NOT IMPLEMENTED. remainder = %lu\n",remainder);
                return -1;
            }
            memset(&(((uint8_t *)virt)[remainder]),0x00,64);
            ((uint8_t *)virt)[remainder] = 0x80;
            for (int i = 8; i > 0; --i) {
                ((uint8_t *)virt)[remainder+55+i] = total_size_bit & 255;
                total_size_bit >>= 8;
            }
            sha256_process(state, (uint8_t *)virt, remainder+64);
        }
        // printf("Chunks %d: Memory hashed\n",i);

        // phys = phys + size;
        virt = virt + size;
    }
    // printf("Chunks done\n");

    char hash[32];
    for(int i=0;i<8;i++){
        hash[i*4] = (uint8_t)(state[i] >> 24);
        hash[i*4+1] = (uint8_t)(state[i] >> 16);
        hash[i*4+2] = (uint8_t)(state[i] >>  8);
        hash[i*4+3] = (uint8_t)(state[i] >>  0);
    }

    for(int i=0;i<32;i++){
        if(hash[i]!=true_hash[i]){
            printf("ERROR: Hash of the Linux kernel is not matching!\n");
            for(int j=0;j<32;j++){printf("0x%02X,",hash[j]);}
            printf("\n");
            // return -1;
            break;
        }
    }
    printf("Linux kernel hash is matching\n");
    return 0;
}

const uint8_t num_pattern = 4;
// const uint8_t inst_pattern[] = {0x10,0x1c,0xd5};// sctlr_el2
// const uint8_t inst_pattern[] = {0x10,0x1d,0xd5};// sctlr_el12
// const uint8_t inst_pattern[] = {0x10,0x18,0xd5};// sctlr_el1
const uint8_t inst_pattern[] = {0x10,0x18,0xd5,0x10,0x1d,0xd5,0x10,0x1c,0xd5,0x20,0x18,0xd5};// sctlr_el*
// const uint8_t nop[] = {0x00,0x00,0x20,0xd4};
// const uint8_t nop[] = {0x1f,0x20,0x03,0xd5};
// const uint8_t inst_pattern[] = {0x10,0x18,0xd5};

void remove_instrs(l4_addr_t start_linux_code, uint64_t size_linux_code){
    // printf("Remove instructions start\n");
    for(uint64_t i=0;i<size_linux_code;i=i+4){
        for(uint8_t k=0;k<num_pattern*3;k=k+3){
            if(!memcmp(&((char *)start_linux_code)[i+1],&(inst_pattern[k]),3)){
                char new_instr[] = {0x00,0x00,0x00,0xd4};
                uint8_t reg = ((uint8_t *)start_linux_code)[i];
                if(reg>=0x40){
                    // printf("Not a sctlr instruction\n");
                    break;
                }
                uint8_t type1 = ((uint8_t *)start_linux_code)[i+1];
                uint8_t type2 = ((uint8_t *)start_linux_code)[i+2];
                uint16_t smc_num = 0x0;
                if(type1==0x10){
                    if(type2==0x18){
                        //sctlr_el1
                        smc_num=0x100;
                    }else if(type2==0x1c){
                        //sctlr_el2
                        smc_num=0x200;
                    }else if(type2==0x1d){
                        //sctlr_el12
                        smc_num=0x300;
                    }else{
                        //unknown
                        printf("ERROR: Unknown instructions %x and %x found, do not replace\n",type1,type2);
                        break;
                    }
                }else if(type1==0x20){
                    if(type2==0x18){
                        if(reg<0x20){
                            //ttbr0_el1
                            smc_num=0x400;
                            // break;
                        }else if(reg<0x40){
                            //ttbr1_el1
                            smc_num=0x500;
                            reg=reg-0x20;
                            // break;
                        }else if(reg<0x60){
                            //tcr_el1
                            smc_num=0x600;
                            reg=reg-0x40;
                            // break;
                        }else{
                            //unknown
                            printf("ERROR: Unknown instructions %x and %x found with reg %x, do not replace\n",type1,type2,reg);
                            break;
                        }
                    }else{
                        //unknown
                        printf("ERROR: Unknown instructions %x and %x found, do not replace\n",type1,type2);
                        break;
                    }
                }else{
                    //unknown
                    printf("ERROR: Unknown instructions %x and %x found, do not replace\n",type1,type2);
                    break;
                }
                // smc instruction is encoded such as smc #n gives 0x20*n as the last bytes of the instruction + 0x03
                smc_num=(reg+smc_num)*0x20 + 0x03;
                // printf("reg=%x,type1=%x,type2=%x,smc_num=%x\n",reg,type1,type2,smc_num);
                memcpy(new_instr, &smc_num, 2);
                memcpy(&((char *)start_linux_code)[i], new_instr, 4);
                // memcpy(&((char *)start_linux_code)[i], nop, 4);
                // memset(&((char *)start_linux_code)[i], 0x0, 4);
                break;
            }
        }
    }
}


int
main()
{
    auto smc = L4Re::Env::env()->get_cap<L4::Arm_smccc>("arm_smc");
    if (!smc){
        printf("osswitch: SMC capability 'arm_smc' not found, disabling.\n");
        return -1;
    }
    l4_umword_t out[4] = {};

    if(get_sigma_caps()!=0){
        return -1;
    }

    l4_addr_t linux_code = map_kernel_code();
    if(linux_code==0){
        printf("map_kernel_code failed\n");
        return -1;
    }
    // remove_instrs(linux_code, TOTAL_SIZE); 


    l4_addr_t linux_start = map_kernel_space();
    if(linux_start==0){
        printf("map_kernel_space failed\n");
        return -1;
    }

    // if(kernel_check()!=0){
    //     printf("Kernel check failed\n");
    //     return -1;
    // }

    long ret = l4_error(smc->call(0xc4000171, 0, 0, SETUP, 0, 0, 0, out, out+1, out+2, out+3, 0));
    if (ret<0){
        printf("Error while calling osswitch\n");
    }
    printf("Hello from the microkernel\n");

    // Remove sctlr_el1 instructions
    remove_instrs(linux_code, TOTAL_SIZE); 

    ret = l4_error(smc->call(0xc4000171, 0, 0, SETUP, 0, 0, 0, out, out+1, out+2, out+3, 0));
    if (ret<0){
        printf("Error while calling osswitch\n");
    }

    while(1){
        uint64_t request_type = *(out+2);
        switch (request_type) {
            case SYSTEMREG:
                {
                    uint64_t value_reg = *(out);
                    uint64_t type = *(out+1);
                    // printf("Request is SYSTEMREG %lx, %lx\n",value_reg,type);
                    ret = l4_error(smc->call(0xc4000171, value_reg, type, SYSTEMREG, 0, 0, 0, out, out + 1, out + 2, out + 3, 0));
                    if (ret<0){
                        printf("Error while calling osswitch\n");
                    }
                }
                break;
            case PAGETABLE:
                {
                    // printf("Request is PAGETABLE\n");
                    l4_addr_t buff_linux = *(out);
                    if(!(buff_linux>=linux_start && buff_linux<=linux_start+TOTAL_SIZE)){
                        unsigned long long pte_val = *((unsigned long long *)buff_linux);
                        pte_val |= ((unsigned long long)1<<51);
                        pte_val &= ~((unsigned long long)1<<7);
                        *((unsigned long long *)buff_linux) = pte_val;
                    }
                    ret = l4_error(smc->call(0xc4000171, 0, 0, PAGETABLE, 0, 0, 0, out, out + 1, out + 2, out + 3, 0));
                    if (ret<0){
                        printf("Error while calling osswitch\n");
                    }
                }
                break;
            default:{
                printf("ERROR: Unknown request of type %lx\n",request_type);
                return -1;
            }
        }
    }
    return 0;
}
