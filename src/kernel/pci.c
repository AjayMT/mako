
// pci.c
//
// PCI driver for Mako.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#include "../common/stdint.h"
#include "io.h"
#include "log.h"
#include "pci.h"

static const pci_dev_t zero = {0};

uint32_t pci_config_read(pci_dev_t dev, uint32_t field, uint32_t size)
{
  dev.field = field >> 2; // Only the top 6 bits.
  dev.enable = 1;
  outl(PCI_ADDRESS_PORT, dev.bits);

  switch (size) {
  case 1: return inb(PCI_VALUE_PORT + (field & 3));
  case 2: return inw(PCI_VALUE_PORT + (field & 2));
  case 4: return inl(PCI_VALUE_PORT);
  }

  return PCI_NONE;
}

void pci_config_write(pci_dev_t dev, uint32_t field, uint32_t value)
{
  dev.field = field >> 2;
  dev.enable = 1;
  outl(PCI_ADDRESS_PORT, dev.bits);
  outl(PCI_VALUE_PORT, value);
}

static inline uint32_t device_type(pci_dev_t d)
{
  return (pci_config_read(d, PCI_CLASS, 1) << 8)
    | pci_config_read(d, PCI_SUBCLASS, 1);
}

static pci_dev_t pci_scan_bus(
  uint16_t vendor_id, uint16_t dev_id, uint32_t bus, int32_t dev_type
  );

static pci_dev_t pci_scan_function(
  uint16_t vendor_id,
  uint16_t dev_id,
  uint32_t bus,
  uint32_t dev,
  uint32_t func,
  int32_t dev_type
  )
{
  pci_dev_t d = {0};
  d.bus = bus;
  d.device = dev;
  d.function = func;

  if (device_type(d) == PCI_TYPE_BRIDGE) {
    uint32_t secondary_bus = pci_config_read(d, PCI_SECONDARY_BUS, 1);
    pci_dev_t res = pci_scan_bus(vendor_id, dev_id, secondary_bus, dev_type);
    if (res.bits) return res;
  }

  if (dev_type == -1 || device_type(d) == (uint32_t)dev_type) {
    uint32_t d_id = pci_config_read(d, PCI_DEVICE_ID, 2);
    uint32_t v_id = pci_config_read(d, PCI_VENDOR_ID, 2);
    if (d_id == dev_id && v_id == vendor_id) return d;
  }

  return zero;
}

static pci_dev_t pci_scan_device(
  uint16_t vendor_id,
  uint16_t dev_id,
  uint32_t bus,
  uint32_t dev,
  int32_t dev_type
  )
{
  pci_dev_t d = {0};
  d.bus = bus;
  d.device = dev;

  if (pci_config_read(d, PCI_VENDOR_ID, 2) == PCI_NONE)
    return zero;

  pci_dev_t res = pci_scan_function(
    vendor_id, dev_id, bus, dev, 0, dev_type
    );
  if (res.bits) return res;

  uint32_t header_type = pci_config_read(d, PCI_HEADER_TYPE, 1);
  if (header_type & 0x80)
    for (uint32_t func = 1; func < 32; ++func) {
      if (pci_config_read(d, PCI_VENDOR_ID, 2) == PCI_NONE)
        continue;
      pci_dev_t res = pci_scan_function(
        vendor_id, dev_id, bus, dev, func, dev_type
        );
      if (res.bits) return res;
    }

  return zero;
}

static pci_dev_t pci_scan_bus(
  uint16_t vendor_id, uint16_t dev_id, uint32_t bus, int32_t dev_type
  )
{
  for (uint32_t dev = 0; dev < 32; ++dev) {
    pci_dev_t res = pci_scan_device(vendor_id, dev_id, bus, dev, dev_type);
    if (res.bits) return res;
  }
  return zero;
}

pci_dev_t pci_find_device(
  uint16_t vendor_id, uint16_t dev_id, int32_t dev_type
  )
{
  pci_dev_t res = pci_scan_bus(vendor_id, dev_id, 0, dev_type);
  if (res.bits) return res;

  uint32_t header_type = pci_config_read(zero, PCI_HEADER_TYPE, 1);
  if ((header_type & 0x80) == 0) return zero;

  for (uint32_t func = 1; func < 32; ++func) {
    pci_dev_t d = {0};
    d.function = func;
    if (pci_config_read(d, PCI_VENDOR_ID, 2) == PCI_NONE)
      return zero;

    d = pci_scan_bus(vendor_id, dev_id, func, dev_type);
    if (d.bits) return d;
  }

  return zero;
}
