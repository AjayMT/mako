
// constants.h
//
// Constants.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#ifndef _CONSTANTS_H_
#define _CONSTANTS_H_

#include <stdint.h>

#define SEGMENT_SELECTOR_KERNEL_CS  8
#define KERNEL_START_VADDR          0xC0000000
#define KERNEL_PD_IDX               (KERNEL_START_VADDR >> 22)

#endif /* _CONSTANTS_H_ */
