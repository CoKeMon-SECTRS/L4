#include <stdint.h>

#include <unistd.h>
#include <stdio.h>
#include <l4/sys/arm_smccc>
#include <l4/re/env>
#include <l4/sigma0/sigma0.h>

#define PAGE_SIZE 0x1000
#define BUFF_ALLOCATE PAGE_SIZE
#define PAGE_MASK (~(PAGE_SIZE - 1))

#define SETUP 2
#define LOGGING 3

// Values for QEMU
// #define TOTAL_SIZE 0x2CAE200
// #define PHYS_ADDR 0x80000000
// const char true_hash[32] = {0xD5,0xE6,0x7D,0xC0,0x5D,0xC4,0x7E,0x48,0xF7,0x91,0x14,0x6C,0xAB,0x83,0xBA,0xB4,0x6E,0xCC,0x18,0xEA,0xD6,0x8C,0xA6,0x4A,0x6E,0x6F,0xA9,0xDB,0x15,0x1A,0x9B,0x56};

// Values for Rock5b
#define TOTAL_SIZE 0x315BA00
#define PHYS_ADDR 0x12400000

int
main()
{
    char name[20];

    auto smc = L4Re::Env::env()->get_cap<L4::Arm_smccc>("arm_smc");
    if (!smc){
        printf("osswitch: SMC capability 'arm_smc' not found, disabling.\n");
        return -1;
    }
    l4_umword_t out[4] = {};
    // long ret = l4_error(smc->call(0xffffffff, 0, 0, 0, 0, 0, 0, out, out + 1, out + 2, out + 3, 0));
    // if (ret<0){
    //     printf("Error w314BA00hile setting performance counters\n");
    // }

    // unsigned long long start_cycles, end_cycles;
    // // asm volatile ("isb; mrs %0, pmccntr_el0; isb;" : "=r" (start_cycles));
    // if(kernel_check()!=0){
    //     printf("Kernel check failed\n");
    //     return -1;
    // }
    // // asm volatile ("isb; mrs %0, pmccntr_el0;" : "=r" (end_cycles));
    // unsigned long long total_cycles=(unsigned long long)(end_cycles - start_cycles);
    // printf("Write operation took %llu cycles\n", total_cycles);

    long ret = l4_error(smc->call(0xc4000171, 0, 0, SETUP, 0, 0, 0, out, out + 1, out + 2, out + 3, 0));
    if (ret<0){
        printf("Error while calling osswitch\n");
    }

    // Not doing anything
    // while(1){
    //     // printf("Hello from the Microkernel\n");
    //     ret = l4_error(smc->call(0xc4000171, 0, 0, LOGGING, 0, 0, 0, out, out + 1, out + 2, out + 3, 0));
    //     if (ret<0){
    //         printf("Error while calling osswitch\n");
    //     }
    // }
    // End of not doing anything
    
    // printf("Addr = %lx, Addr2 = %lx, length = %lu\n",*out,*(out+1), *(out+2));
    l4_addr_t buff_linux = *(out+1);
    int len_buf = *(out+2);
    l4_addr_t buff_linux_down = buff_linux & PAGE_MASK;
    l4_addr_t virt_buff_linux = 0x80000000;
    // l4_addr_t virt_buff_linux = buff_linux_down;
    int buff_allocate = BUFF_ALLOCATE;
    l4_addr_t buff_linux_up = buff_linux_down + buff_allocate;
    int64_t offset = buff_linux - buff_linux_down;
    // printf("buff_linux_up %lx, buff_linux_down %lx, offset %lx, len_buf %d\n",buff_linux_up,buff_linux_down,offset,len_buf);
    // if(virt_buff_linux+offset+len_buf>buff_linux_up){
    // TODO: double check the line bellow is correct and that we shoud not use the line above instead
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
    // printf("virt buff: %lx, offset: %ld, virt with offset: %lx \n", virt_buff_linux, offset, virt_buff_linux+offset);
    write(STDOUT_FILENO, (char *)virt_buff_linux+offset, len_buf);
    while(1){
        ret = l4_error(smc->call(0xc4000171, 0, 0, LOGGING, 0, 0, 0, out, out + 1, out + 2, out + 3, 0));
        if (ret<0){
            printf("Error while calling osswitch\n");
        }
        // printf("Addr = %lx, Addr2 = %lx, length = %lu\n",*out,*(out+1), *(out+2));
        buff_linux = *(out+1);
        len_buf = *(out+2);
        offset = buff_linux - (buff_linux & PAGE_MASK);
        if( buff_linux < buff_linux_down || buff_linux + len_buf > buff_linux_up){
            // printf("New buffer outside of former buffer, allocating new memory range\n");
            buff_linux_down = buff_linux & PAGE_MASK;
            buff_allocate = BUFF_ALLOCATE;
            buff_linux_up = buff_linux_down + buff_allocate;
            // if(virt_buff_linux+offset+len_buf>buff_linux_up){
            // TODO: double check the line bellow is correct and that we shoud not use the line above instead
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
    }
    // fflush(stdin);
    // fgets(name,21,stdin);
    // printf("Input Received\n");
    // printf("ERROR: should not return\n");
    return 0;
}
