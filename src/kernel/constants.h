
// constants.h
//
// Constants.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#ifndef _CONSTANTS_H_
#define _CONSTANTS_H_

#define SYSCALL_INT_IDX 0x80
#define SEGMENT_SELECTOR_KERNEL_CS 8
#define SEGMENT_SELECTOR_KERNEL_DS 0x10
#define KERNEL_START_VADDR 0xC0000000
#define PHYS_ADDR_OFFSET 12
#define PD_VADDR 0xFFFFF000
#define FIRST_PT_VADDR 0xFFC00000
#define PAGE_SIZE 0x1000
#define PAGE_SIZE_DWORDS 0x400

#endif /* _CONSTANTS_H_ */
