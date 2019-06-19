
// pci.h
//
// PCI driver for Mako.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#ifndef _PCI_H_
#define _PCI_H_

#include <stdint.h>

#define PCI_ADDRESS_PORT 0xCF8
#define PCI_VALUE_PORT   0xCFC

// Field offsets.
#define PCI_VENDOR_ID            0
#define PCI_DEVICE_ID            2
#define PCI_COMMAND              4
#define PCI_STATUS               6
#define PCI_REVISION_ID          8
#define PCI_PROG_IF              9
#define PCI_SUBCLASS             0xA
#define PCI_CLASS                0xB
#define PCI_CACHE_LINE_SIZE      0xB
#define PCI_LATENCY_TIMER        0xD
#define PCI_HEADER_TYPE          0xE
#define PCI_BIST                 0xF
#define PCI_BAR0                 0x10
#define PCI_BAR1                 0x14
#define PCI_BAR2                 0x18
#define PCI_BAR3                 0x1C
#define PCI_BAR4                 0x20
#define PCI_BAR5                 0x24
#define PCI_INTERRUPT_LINE       0x3C
#define PCI_SECONDARY_BUS        9

// Device types.
#define PCI_HEADER_TYPE_DEVICE  0
#define PCI_HEADER_TYPE_BRIDGE  1
#define PCI_HEADER_TYPE_CARDBUS 2
#define PCI_TYPE_BRIDGE         0x0604
#define PCI_TYPE_SATA           0x0106
#define PCI_NONE                0xFFFF

typedef union {
  uint32_t bits;
  struct {
    uint32_t zero     : 2;
    uint32_t field    : 6;
    uint32_t function : 3;
    uint32_t device   : 5;
    uint32_t bus      : 8;
    uint32_t reserved : 7;
    uint32_t enable   : 1;
  };
} pci_dev_t;

uint32_t pci_config_read(pci_dev_t dev, uint32_t field, uint32_t size);
void pci_config_write(pci_dev_t dev, uint32_t field, uint32_t value);

pci_dev_t pci_find_device(
  uint16_t vendor_id, uint16_t dev_id, int32_t dev_type
  );

#endif /* _PCI_H_ */
