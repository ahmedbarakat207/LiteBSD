#ifndef _PCI_H
#define _PCI_H

#include <stdint.h>

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

#define PCI_COMMAND_IO          0x0001
#define PCI_COMMAND_MEMORY      0x0002
#define PCI_COMMAND_BUS_MASTER  0x0004

#define MAX_PCI_DEVICES 32

struct pci_device {
    uint8_t  bus;
    uint8_t  slot;
    uint8_t  func;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t  class_code;
    uint8_t  subclass;
    uint8_t  prog_if;
    uint8_t  irq;
    uint32_t bar[6];
    uint32_t bar_size[6];
    uint8_t  bar_is_io[6];
};

uint32_t pci_read_config_32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
uint16_t pci_read_config_16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
uint8_t  pci_read_config_8(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);

void pci_write_config_32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t val);
void pci_write_config_16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint16_t val);
void pci_write_config_8(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint8_t val);

void pci_enable_bus_master(struct pci_device *dev);

void pci_init(void);
struct pci_device *pci_find_device(uint16_t vendor, uint16_t device);
struct pci_device *pci_find_class(uint8_t class_code, uint8_t subclass);

#endif
