
// ata.c
//
// ATA driver for Mako.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#include <stdint.h>
#include <drivers/pci/pci.h>
#include <drivers/io/io.h>
#include <kheap/kheap.h>
#include <pmm/pmm.h>
#include <paging/paging.h>
#include <interrupt/interrupt.h>
#include <fs/fs.h>
#include <common/constants.h>
#include <util/util.h>
#include <debug/log.h>
#include "ata.h"

// PCI info.
static const uint16_t ATA_VENDOR_ID   = 0x8086;
static const uint16_t ATA_DEVICE_ID   = 0x7010;

// PRDT.
static const uint16_t SECTOR_SIZE     = 0x200;
static const uint16_t PRDT_END        = 0x8000;

// Control/Alt-status register.
static const uint8_t CONTROL_RESET    = 4;

// Command/status register.
static const uint8_t COMMAND_IDENTIFY = 0xEC;
static const uint8_t COMMAND_DMA_READ = 0xC8;
static const uint8_t STATUS_ERR       = 1;
static const uint8_t STATUS_DRQ       = 8;
static const uint8_t STATUS_BSY       = 0x80;

static pci_dev_t ata_pci_device;
static ata_dev_t primary_master;
static ata_dev_t primary_slave;
static ata_dev_t secondary_master;
static ata_dev_t secondary_slave;

// TODO better error handling.
#define CHECK(err, msg) if ((err)) { log_error("ata", msg "\n"); return 1; }

static uint8_t ata_dev_init(ata_dev_t *dev, uint8_t is_primary)
{
  static uint32_t prdt_page_paddr = 0;
  static uint32_t prdt_page_vaddr = 0;
  static uint32_t prdt_offset = 0;

  page_table_entry_t flags; u_memset(&flags, 0, sizeof(flags));
  flags.rw = 1;
  paging_result_t res;

  if (!prdt_page_paddr || !prdt_page_vaddr) {
    prdt_page_paddr = pmm_alloc(1);
    CHECK(!prdt_page_paddr, "No memory.");
    prdt_page_vaddr = paging_next_vaddr(1, KERNEL_START_VADDR);
    CHECK(!prdt_page_vaddr, "No memory.");
    res = paging_map(prdt_page_vaddr, prdt_page_paddr, flags);
    CHECK(res != PAGING_OK, "paging_map failed.");
  }

  dev->prdt_paddr = prdt_page_paddr + prdt_offset;
  dev->prdt = (prd_t *)(prdt_page_vaddr + prdt_offset);
  prdt_offset += sizeof(prd_t);

  dev->prdt->transfer_size = SECTOR_SIZE;
  dev->prdt->end = PRDT_END;
  dev->prdt->buf_paddr = pmm_alloc(1);
  CHECK(!(dev->prdt->buf_paddr), "No memory.");
  dev->buf = (uint8_t *)paging_next_vaddr(1, KERNEL_START_VADDR);
  CHECK(!(dev->buf), "No memory.");
  res = paging_map((uint32_t)dev->buf, dev->prdt->buf_paddr, flags);
  CHECK(res != PAGING_OK, "paging_map failed.");

  dev->ports.data = is_primary ? 0x1F0 : 0x170;
  dev->ports.error = dev->ports.data + 1;
  dev->ports.sector_count = dev->ports.data + 2;
  dev->ports.lba_1 = dev->ports.data + 3;
  dev->ports.lba_2 = dev->ports.data + 4;
  dev->ports.lba_3 = dev->ports.data + 5;
  dev->ports.drive = dev->ports.data + 6;
  dev->ports.command_status = dev->ports.data + 7;
  dev->ports.control_alt_status = is_primary ? 0x3F6 : 0x376;

  dev->ports.busmaster_command = pci_config_read(
    ata_pci_device, PCI_BAR4, 4
    );
  CHECK(dev->ports.busmaster_command == PCI_NONE, "Failed to read BAR4.");
  if (dev->ports.busmaster_command & 1)
    dev->ports.busmaster_command &= 0xFFFFFFFC;
  dev->ports.busmaster_status = dev->ports.busmaster_command + 2;
  dev->ports.busmaster_prdt = dev->ports.busmaster_command + 4;

  return 0;
}

static void wait_io(ata_dev_t *dev)
{
  for (uint8_t i = 0; i < 4; ++i)
    inb(dev->ports.control_alt_status);
}

static uint8_t wait_status(ata_dev_t *dev, int32_t timeout)
{
  uint8_t status = inb(dev->ports.command_status);
  if (timeout > 0)
    for (
      int32_t i = 0; (status & STATUS_BSY) && i < timeout; ++i
      ) status = inb(dev->ports.command_status);
  else for (; (status & STATUS_BSY);)
         status = inb(dev->ports.command_status);
  return status;
}

static void soft_reset(ata_dev_t *dev)
{
  outb(dev->ports.control_alt_status, CONTROL_RESET);
  wait_io(dev);
  outb(dev->ports.control_alt_status, 0);
}

static uint8_t ata_dev_setup(ata_dev_t *dev, uint8_t is_primary)
{
  CHECK(ata_dev_init(dev, is_primary), "Failed to initialize device.");
  soft_reset(dev);
  wait_io(dev);

  outb(dev->ports.error, 1);
  outb(dev->ports.control_alt_status, 0);
  outb(dev->ports.drive, dev->is_slave ? 0xB0 : 0xA0);
  wait_io(dev);
  uint8_t status = wait_status(dev, 10000);
  CHECK(status & STATUS_ERR, "Error after selecting drive.");

  outb(dev->ports.lba_1, 0xFE);
  outb(dev->ports.lba_2, 0xED);
  uint8_t lba_1 = inb(dev->ports.lba_1);
  uint8_t lba_2 = inb(dev->ports.lba_2);
  CHECK(lba_1 != 0xFE, "No drive present.");
  CHECK(lba_2 != 0xED, "No drive present.");

  outb(dev->ports.command_status, COMMAND_IDENTIFY);
  CHECK(!inb(dev->ports.command_status), "Drive does not exist.");

  for (uint32_t i = 0; i < 256; ++i) inw(dev->ports.data);

  uint32_t command_reg = pci_config_read(ata_pci_device, PCI_COMMAND, 2);
  if ((command_reg & 4) == 0) {
    command_reg |= 4;
    pci_config_write(ata_pci_device, PCI_COMMAND, command_reg);
  }

  return 0;
}

void ata_primary_interrupt_handler()
{
  // TODO
}

void ata_secondary_interrupt_handler()
{
  // TODO
}

uint8_t ata_init()
{
  ata_pci_device = pci_find_device(ATA_VENDOR_ID, ATA_DEVICE_ID, -1);
  CHECK(!ata_pci_device.bits, "PCI device not found.");

  register_interrupt_handler(46, ata_primary_interrupt_handler);
  register_interrupt_handler(47, ata_secondary_interrupt_handler);

  primary_master.is_slave = 0;
  primary_slave.is_slave = 1;
  secondary_master.is_slave = 0;
  secondary_slave.is_slave = 1;

  CHECK(
    ata_dev_setup(&primary_master, 1),
    "Failed to setup primary master drive."
    );
  log_info("ata", "Attempting to set up primary slave.\n");
  if (ata_dev_setup(&primary_slave, 1))
    log_info("ata", "Could not set up primary slave.\n");
  log_info("ata", "Attempting to set up secondary master.\n");
  if (ata_dev_setup(&secondary_master, 0))
    log_info("ata", "Could not set up secondary master.\n");
  log_info("ata", "Attempting to set up secondary slave.\n");
  if (ata_dev_setup(&secondary_slave, 0))
    log_info("ata", "Could not set up secondary slave.\n");

  return 0;
}
