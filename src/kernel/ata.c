
// ata.c
//
// ATA driver for Mako.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#include "ata.h"
#include "../common/errno.h"
#include "../common/stdint.h"
#include "constants.h"
#include "fs.h"
#include "interrupt.h"
#include "io.h"
#include "kheap.h"
#include "klock.h"
#include "log.h"
#include "paging.h"
#include "pci.h"
#include "pmm.h"
#include "util.h"

#define CHECK(err, msg, code)                                                                      \
  if ((err)) {                                                                                     \
    log_error("ata", msg "\n");                                                                    \
    return (code);                                                                                 \
  }
#define CHECK_UNLOCK(err, msg, code)                                                               \
  if ((err)) {                                                                                     \
    log_error("ata", msg "\n");                                                                    \
    kunlock(&dev->lock);                                                                           \
    return (code);                                                                                 \
  }

// PCI info.
static const uint16_t ATA_VENDOR_ID = 0x8086;
static const uint16_t ATA_DEVICE_ID = 0x7010;

// PRDT.
static const uint16_t SECTOR_SIZE = 0x200;
static const uint16_t PRDT_END = 0x8000;

// Control/Alt-status register.
static const uint8_t CONTROL_RESET = 4;

// Command/status register.
static const uint8_t COMMAND_IDENTIFY = 0xEC;
static const uint8_t COMMAND_DMA_READ = 0x25;
static const uint8_t COMMAND_DMA_WRITE = 0x35;
static const uint8_t STATUS_ERR = 1;
static const uint8_t STATUS_DRQ = 8;
static const uint8_t STATUS_BSY = 0x80;
static const uint8_t STATUS_DRDY = 0x40;

static pci_dev_t ata_pci_device;
static ata_dev_t primary_master;
static ata_dev_t primary_slave;
static ata_dev_t secondary_master;
static ata_dev_t secondary_slave;

static void wait_io(ata_dev_t *);
static uint8_t wait_status(ata_dev_t *, int32_t);

static uint8_t ata_read_sector(ata_dev_t *dev, uint32_t block, uint8_t *buf)
{
  klock(&dev->lock);

  wait_io(dev);
  CHECK_UNLOCK(wait_status(dev, -1) & STATUS_ERR, "Error status.", 1);

  // Reset busmaster command register.
  outb(dev->ports.busmaster_command, 0);

  // Set PRDT.
  outl(dev->ports.busmaster_prdt, dev->prdt_paddr);

  // Set read bit.
  outb(dev->ports.busmaster_command, 8);

  // Enable error and IRQ status.
  uint8_t busmaster_status = inb(dev->ports.busmaster_status);
  outb(dev->ports.busmaster_status, busmaster_status | 2 | 4);
  uint32_t eflags = interrupt_save_disable();
  enable_interrupts();
  CHECK_UNLOCK(wait_status(dev, -1) & STATUS_ERR, "Error status.", 1);

  // Select drive.
  outb(dev->ports.control_alt_status, 0);
  outb(dev->ports.drive, 0xE0 | (dev->is_slave << 4));
  wait_io(dev);

  // Set sector count and LBA registers.
  outb(dev->ports.sector_count, 0);
  outb(dev->ports.lba_1, (block & 0xFF000000) >> 24);
  outb(dev->ports.lba_2, (block & 0xFF00000000) >> 32);
  outb(dev->ports.lba_3, (block & 0xFF0000000000) >> 40);
  outb(dev->ports.sector_count, 1);
  outb(dev->ports.lba_1, block & 0xFF);
  outb(dev->ports.lba_2, (block & 0xFF00) >> 8);
  outb(dev->ports.lba_3, (block & 0xFF0000) >> 16);
  while (1) {
    uint8_t status = inb(dev->ports.command_status);
    if (!(status & STATUS_BSY) && (status & STATUS_DRDY))
      break;
  }

  // Set the command register to the READ DMA command.
  outb(dev->ports.command_status, COMMAND_DMA_READ);
  wait_io(dev);

  // Start reading.
  outb(dev->ports.busmaster_command, 8 | 1);

  // Wait for DMA write to complete.
  busmaster_status = inb(dev->ports.busmaster_status);
  uint8_t status = inb(dev->ports.command_status);
  for (; (!(busmaster_status & 4)) || (status & STATUS_BSY);
       busmaster_status = inb(dev->ports.busmaster_status), status = inb(dev->ports.command_status))
    ;

  interrupt_restore(eflags);

  // Copy to output buffer.
  u_memcpy(buf, dev->buf, SECTOR_SIZE);

  // Inform device we are done.
  busmaster_status = inb(dev->ports.busmaster_status);
  outb(dev->ports.busmaster_status, busmaster_status | 4 | 2);

  kunlock(&dev->lock);
  return 0;
}

static uint32_t ata_read(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buf)
{
  ata_dev_t *dev = (ata_dev_t *)node->device;
  uint32_t max_offset = dev->identity.sectors_28 * SECTOR_SIZE;
  if (offset > max_offset)
    return 0;
  if (offset + size > max_offset)
    size = max_offset - offset;

  uint32_t start_block = offset / SECTOR_SIZE;
  uint32_t start_offset = offset % SECTOR_SIZE;
  uint32_t end_block = (offset + size - 1) / SECTOR_SIZE;
  uint32_t end_offset = (offset + size - 1) % SECTOR_SIZE;
  uint32_t read_size = 0;

  uint8_t *bufp = buf;
  uint32_t current_block = start_block;
  uint8_t *tmp_buf = kmalloc(SECTOR_SIZE);
  CHECK(tmp_buf == NULL, "No memory.", read_size);

  while (current_block <= end_block) {
    uint32_t res = ata_read_sector(dev, current_block, tmp_buf);
    CHECK(res, "Error reading ATA device.", read_size);

    uint32_t offset = 0;
    uint32_t size = SECTOR_SIZE;
    if (current_block == start_block) {
      offset = start_offset;
      size -= start_offset;
    }
    if (current_block == end_block)
      size = end_offset - offset + 1;

    u_memcpy(bufp, tmp_buf + offset, size);
    bufp += size;
    read_size += size;
    ++current_block;
  }

  kfree(tmp_buf);
  return read_size;
}

static uint8_t ata_write_sector(ata_dev_t *dev, uint32_t block, uint8_t *buf)
{
  klock(&dev->lock);

  u_memcpy(dev->buf, buf, SECTOR_SIZE);
  wait_io(dev);
  CHECK_UNLOCK(wait_status(dev, -1) & STATUS_ERR, "Error status.", 1);
  // Reset busmaster command register.
  outb(dev->ports.busmaster_command, 0);

  // Set PRDT.
  outl(dev->ports.busmaster_prdt, dev->prdt_paddr);

  // Enable error and IRQ status.
  uint8_t busmaster_status = inb(dev->ports.busmaster_status);
  outb(dev->ports.busmaster_status, busmaster_status | 2 | 4);
  uint32_t eflags = interrupt_save_disable();
  enable_interrupts();
  CHECK_UNLOCK(wait_status(dev, -1) & STATUS_ERR, "Error status.", 1);

  // Select drive.
  outb(dev->ports.control_alt_status, 0);
  outb(dev->ports.drive, 0xE0 | (dev->is_slave << 4));

  // Set sector count and LBA registers.
  outb(dev->ports.sector_count, 0);
  outb(dev->ports.lba_1, (block & 0xFF000000) >> 24);
  outb(dev->ports.lba_2, (block & 0xFF00000000) >> 32);
  outb(dev->ports.lba_3, (block & 0xFF0000000000) >> 40);
  outb(dev->ports.sector_count, 1);
  outb(dev->ports.lba_1, block & 0xFF);
  outb(dev->ports.lba_2, (block & 0xFF00) >> 8);
  outb(dev->ports.lba_3, (block & 0xFF0000) >> 16);
  while (1) {
    uint8_t status = inb(dev->ports.command_status);
    if (!(status & STATUS_BSY) && (status & STATUS_DRDY))
      break;
  }

  // Set the command register to the WRITE DMA command.
  outb(dev->ports.command_status, COMMAND_DMA_WRITE);
  wait_io(dev);

  // Start writing.
  outb(dev->ports.busmaster_command, 1);

  // Wait for DMA write to complete.
  busmaster_status = inb(dev->ports.busmaster_status);
  uint8_t status = inb(dev->ports.command_status);
  for (; (!(busmaster_status & 4)) || (status & STATUS_BSY);
       busmaster_status = inb(dev->ports.busmaster_status), status = inb(dev->ports.command_status))
    ;

  interrupt_restore(eflags);

  // Inform device we are done.
  busmaster_status = inb(dev->ports.busmaster_status);
  outb(dev->ports.busmaster_status, busmaster_status | 4 | 2);

  kunlock(&dev->lock);
  return 0;
}

static uint32_t ata_write(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buf)
{
  ata_dev_t *dev = (ata_dev_t *)node->device;
  uint32_t max_offset = dev->identity.sectors_28 * SECTOR_SIZE;
  if (offset > max_offset)
    return 0;
  if (offset + size > max_offset)
    size = max_offset - offset;

  uint32_t start_block = offset / SECTOR_SIZE;
  uint32_t start_offset = offset % SECTOR_SIZE;
  uint32_t end_block = (offset + size - 1) / SECTOR_SIZE;
  uint32_t end_offset = (offset + size - 1) % SECTOR_SIZE;
  uint32_t written_size = 0;

  uint8_t *bufp = buf;
  uint32_t current_block = start_block;
  uint8_t *tmp_buf = kmalloc(SECTOR_SIZE);
  CHECK(tmp_buf == NULL, "No memory.", written_size);

  while (current_block <= end_block) {
    uint32_t res = ata_read_sector(dev, current_block, tmp_buf);
    CHECK(res, "Error reading ATA device.", written_size);

    uint32_t offset = 0;
    uint32_t size = SECTOR_SIZE;
    if (current_block == start_block) {
      offset = start_offset;
      size -= start_offset;
    }
    if (current_block == end_block)
      size = end_offset - offset + 1;

    u_memcpy(tmp_buf + offset, bufp, size);
    res = ata_write_sector(dev, current_block, tmp_buf);
    CHECK(res, "Error writing ATA device.", written_size);

    bufp += size;
    written_size += size;
    ++current_block;
  }

  kfree(tmp_buf);
  return written_size;
}

static uint8_t ata_dev_init(ata_dev_t *dev, uint8_t is_primary)
{
  static uint32_t prdt_page_paddr = 0;
  static uint32_t prdt_page_vaddr = 0;
  static uint32_t prdt_offset = 0;

  page_table_entry_t flags;
  u_memset(&flags, 0, sizeof(flags));
  flags.rw = 1;
  paging_result_t res;

  if (!prdt_page_paddr || !prdt_page_vaddr) {
    prdt_page_paddr = pmm_alloc(1);
    CHECK(!prdt_page_paddr, "No memory.", ENOMEM);
    prdt_page_vaddr = paging_next_vaddr(1, KERNEL_START_VADDR);
    CHECK(!prdt_page_vaddr, "No memory.", ENOMEM);
    res = paging_map(prdt_page_vaddr, prdt_page_paddr, flags);
    CHECK(res != PAGING_OK, "paging_map failed.", ENOMEM);
  }

  dev->prdt_paddr = prdt_page_paddr + prdt_offset;
  dev->prdt = (prd_t *)(prdt_page_vaddr + prdt_offset);
  prdt_offset += sizeof(prd_t);

  dev->prdt->transfer_size = SECTOR_SIZE;
  dev->prdt->end = PRDT_END;
  dev->prdt->buf_paddr = pmm_alloc(1);
  CHECK(!(dev->prdt->buf_paddr), "No memory.", ENOMEM);
  dev->buf = (uint8_t *)paging_next_vaddr(1, KERNEL_START_VADDR);
  CHECK(!(dev->buf), "No memory.", ENOMEM);
  res = paging_map((uint32_t)dev->buf, dev->prdt->buf_paddr, flags);
  CHECK(res != PAGING_OK, "paging_map failed.", ENOMEM);

  dev->ports.data = is_primary ? 0x1F0 : 0x170;
  dev->ports.error = dev->ports.data + 1;
  dev->ports.sector_count = dev->ports.data + 2;
  dev->ports.lba_1 = dev->ports.data + 3;
  dev->ports.lba_2 = dev->ports.data + 4;
  dev->ports.lba_3 = dev->ports.data + 5;
  dev->ports.drive = dev->ports.data + 6;
  dev->ports.command_status = dev->ports.data + 7;
  dev->ports.control_alt_status = dev->ports.data + 0xC;

  dev->ports.busmaster_command = pci_config_read(ata_pci_device, PCI_BAR4, 4);
  CHECK(dev->ports.busmaster_command == PCI_NONE, "Failed to read BAR4.", 1);
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
    for (int32_t i = 0; (status & STATUS_BSY) && i < timeout; ++i)
      status = inb(dev->ports.command_status);
  else
    for (; (status & STATUS_BSY);)
      status = inb(dev->ports.command_status);
  return status;
}

static void soft_reset(ata_dev_t *dev)
{
  outb(dev->ports.control_alt_status, CONTROL_RESET);
  wait_io(dev);
  outb(dev->ports.control_alt_status, 0);
}

static uint8_t ata_dev_setup(ata_dev_t *dev, fs_node_t *node, uint8_t is_primary)
{
  CHECK(ata_dev_init(dev, is_primary), "Failed to initialize device.", 1);
  soft_reset(dev);
  wait_io(dev);

  outb(dev->ports.error, 1);
  outb(dev->ports.control_alt_status, 0);
  outb(dev->ports.drive, dev->is_slave ? 0xB0 : 0xA0);
  wait_io(dev);
  uint8_t status = wait_status(dev, 10000);
  CHECK(status & STATUS_ERR, "Error after selecting drive.", 1);

  outb(dev->ports.lba_1, 0xFE);
  outb(dev->ports.lba_2, 0xED);
  uint8_t lba_1 = inb(dev->ports.lba_1);
  uint8_t lba_2 = inb(dev->ports.lba_2);
  CHECK(lba_1 != 0xFE, "No drive present.", 1);
  CHECK(lba_2 != 0xED, "No drive present.", 1);

  outb(dev->ports.command_status, COMMAND_IDENTIFY);
  CHECK(!inb(dev->ports.command_status), "Drive does not exist.", 1);

  uint16_t *buf = (uint16_t *)(&dev->identity);
  for (uint32_t i = 0; i < 256; ++i)
    buf[i] = inw(dev->ports.data);

  uint32_t command_reg = pci_config_read(ata_pci_device, PCI_COMMAND, 2);
  if ((command_reg & 4) == 0) {
    command_reg |= 4;
    pci_config_write(ata_pci_device, PCI_COMMAND, command_reg);
  }

  u_memset(node, 0, sizeof(fs_node_t));
  u_memcpy(node->name, "atadev", 7);
  node->mask = 0660;
  node->type = FS_BLOCKDEVICE;
  node->size = dev->identity.sectors_28 * SECTOR_SIZE;
  node->device = dev;
  node->read = ata_read;
  node->write = ata_write;

  return 0;
}

void ata_primary_interrupt_handler()
{
  uint8_t status = inb(primary_master.ports.command_status);
  inb(primary_master.ports.busmaster_status);
  outb(primary_master.ports.busmaster_command, 0);
}

void ata_secondary_interrupt_handler()
{
  uint8_t status = inb(secondary_master.ports.command_status);
  inb(secondary_master.ports.busmaster_status);
  outb(secondary_master.ports.busmaster_command, 0);
}

uint8_t ata_init(fs_node_t *node)
{
  ata_pci_device = pci_find_device(ATA_VENDOR_ID, ATA_DEVICE_ID, -1);
  CHECK(!ata_pci_device.bits, "PCI device not found.", 1);

  register_interrupt_handler(46, ata_primary_interrupt_handler);
  register_interrupt_handler(47, ata_secondary_interrupt_handler);

  primary_master.is_slave = 0;
  primary_slave.is_slave = 1;
  secondary_master.is_slave = 0;
  secondary_slave.is_slave = 1;

  CHECK(ata_dev_setup(&primary_master, node, 1), "Failed to setup primary master drive.", 1);
  /* log_info("ata", "Attempting to set up primary slave.\n"); */
  /* if (ata_dev_setup(&primary_slave, 1)) */
  /*   log_info("ata", "Could not set up primary slave.\n"); */
  /* log_info("ata", "Attempting to set up secondary master.\n"); */
  /* if (ata_dev_setup(&secondary_master, 0)) */
  /*   log_info("ata", "Could not set up secondary master.\n"); */
  /* log_info("ata", "Attempting to set up secondary slave.\n"); */
  /* if (ata_dev_setup(&secondary_slave, 0)) */
  /*   log_info("ata", "Could not set up secondary slave.\n"); */

  return 0;
}
