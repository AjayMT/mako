
// ata.h
//
// ATA driver for Mako.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#ifndef _ATA_H_
#define _ATA_H_

#include <stdint.h>
#include <drivers/pci/pci.h>

// This is always the last PRDT entry.
struct prd_s {
  uint32_t buf_paddr;
  uint16_t transfer_size;
  uint16_t end;
} __attribute__((packed));
typedef struct prd_s prd_t;

typedef struct {
  struct {
    uint16_t data;
    uint16_t error;
    uint16_t sector_count;

    uint16_t lba_1;
    uint16_t lba_2;
    uint16_t lba_3;

    uint16_t drive;
    uint16_t command_status;
    uint16_t control_alt_status;

    uint16_t busmaster_command;
    uint16_t busmaster_status;
    uint16_t busmaster_prdt;
  } ports;

  uint8_t is_slave;
  prd_t *prdt;
  uint32_t prdt_paddr;
  uint8_t *buf;
} ata_dev_t;

uint8_t ata_init();

#endif /* _ATA_H_ */
