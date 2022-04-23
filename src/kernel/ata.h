
// ata.h
//
// ATA driver for Mako.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#ifndef _ATA_H_
#define _ATA_H_

#include "../common/stdint.h"
#include "pci.h"

// This is always the last PRDT entry.
struct prd_s {
  uint32_t buf_paddr;
  uint16_t transfer_size;
  uint16_t end;
} __attribute__((packed));
typedef struct prd_s prd_t;

// ATA identification format.
struct ata_identify_s {
  uint16_t flags;
  uint16_t unused1[9];
  uint8_t  serial[20];
  uint16_t unused2[3];
  uint8_t  firmware[8];
  uint8_t  model[40];
  uint16_t sectors_per_int;
  uint16_t unused3;
  uint16_t capabilities[2];
  uint16_t unused4[2];
  uint16_t valid_ext_data;
  uint16_t unused5[5];
  uint16_t size_of_rw_mult;
  uint32_t sectors_28;
  uint16_t unused6[38];
  uint64_t sectors_48;
  uint16_t unused7[152];
} __attribute__((packed));
typedef struct ata_identify_s ata_identify_t;

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
  ata_identify_t identity;
} ata_dev_t;

uint8_t ata_init();

#endif /* _ATA_H_ */
