#include <cstddef>
#include <stdint.h>
#include <string.h> // TO BE REMOVED - USELESS

#include <unistd.h>
#include <stdio.h>
#include <l4/sys/arm_smccc>
#include <l4/re/env>
#include <l4/sigma0/sigma0.h>

#include "l4/sys/l4int.h"

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
#define MICRO_ADDR_DOWN 0xa0000000
#define MICRO_ADDR_SIZE 0x40000000
#define LINUX_SPACE_SIZE 0x342C000
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

    // l4_addr_t linux_code = map_kernel_code();
    // if(linux_code==0){
    //     printf("map_kernel_code failed\n");
    //     return -1;
    // }
    // remove_instrs(linux_code, TOTAL_SIZE); 


    // l4_addr_t linux_start = map_kernel_space();
    // if(linux_start==0){
    //     printf("map_kernel_space failed\n");
    //     return -1;
    // }

    // if(kernel_check()!=0){
    //     printf("Kernel check failed\n");
    //     return -1;
    // }

    // long ret = l4_error(smc->call(0xc4000171, 0, 0, SETUP, 0, 0, 0, out, out+1, out+2, out+3, 0));
    // if (ret<0){
    //     printf("Error while calling osswitch\n");
    // }
    // printf("Hello from the microkernel\n");

    // Remove sctlr_el1 instructions
    // remove_instrs(linux_code, TOTAL_SIZE); 

    long ret = l4_error(smc->call(0xc4000171, 0, 0, SETUP, 0, 0, 0, out, out+1, out+2, out+3, 0));
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
                    size_t size = *(out+1);
                    if(size==0){
                        // printf("Unmap iova %lx\n",buff_linux);
                    }
                    else if(!(buff_linux+size>=PHYS_ADDR_DOWN && buff_linux<=PHYS_ADDR_DOWN+TOTAL_SIZE && buff_linux+size<=MICRO_ADDR_DOWN && buff_linux<=MICRO_ADDR_DOWN+MICRO_ADDR_SIZE)){
                        // printf("Mapping page iova of size %zu to address %lx\n",size,buff_linux);
                    }else{
                        printf("ERROR: Linux is trying to map an iova to a forbidden physical page\n");
                    }
                    ret = l4_error(smc->call(0xc4000171, 0, 0, PAGETABLE, 0, 0, 0, out, out + 1, out + 2, out + 3, 0));
                    if (ret<0){
                        printf("Error while calling osswitch\n");
                    }
                }
                break;
            default:
                {
                    printf("ERROR: Unknown request of type %lx\n",request_type);
                    return -1;
                }
        }
    }
    return 0;
}
