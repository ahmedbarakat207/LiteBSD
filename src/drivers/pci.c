#include "drivers/include/pci.h"
#include "include/io.h"
#include "include/tty.h"

static struct pci_device s_pci_devices[MAX_PCI_DEVICES];
static int s_pci_device_count = 0;

uint32_t pci_read_config_32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t address = 0x80000000 | ((uint32_t)bus << 16) | ((uint32_t)slot << 11) |
                       ((uint32_t)func << 8) | (offset & 0xFC);
    outl(PCI_CONFIG_ADDRESS, address);
    return inl(PCI_CONFIG_DATA);
}

uint16_t pci_read_config_16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t val32 = pci_read_config_32(bus, slot, func, offset);
    return (uint16_t)((val32 >> ((offset & 2) * 8)) & 0xFFFF);
}

uint8_t pci_read_config_8(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t val32 = pci_read_config_32(bus, slot, func, offset);
    return (uint8_t)((val32 >> ((offset & 3) * 8)) & 0xFF);
}

void pci_write_config_32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t val) {
    uint32_t address = 0x80000000 | ((uint32_t)bus << 16) | ((uint32_t)slot << 11) |
                       ((uint32_t)func << 8) | (offset & 0xFC);
    outl(PCI_CONFIG_ADDRESS, address);
    outl(PCI_CONFIG_DATA, val);
}

void pci_write_config_16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint16_t val) {
    uint32_t address = 0x80000000 | ((uint32_t)bus << 16) | ((uint32_t)slot << 11) |
                       ((uint32_t)func << 8) | (offset & 0xFC);
    outl(PCI_CONFIG_ADDRESS, address);
    uint32_t old_val = inl(PCI_CONFIG_DATA);
    uint32_t shift = (offset & 2) * 8;
    uint32_t mask = 0xFFFF << shift;
    uint32_t new_val = (old_val & ~mask) | (((uint32_t)val) << shift);
    outl(PCI_CONFIG_DATA, new_val);
}

void pci_write_config_8(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint8_t val) {
    uint32_t address = 0x80000000 | ((uint32_t)bus << 16) | ((uint32_t)slot << 11) |
                       ((uint32_t)func << 8) | (offset & 0xFC);
    outl(PCI_CONFIG_ADDRESS, address);
    uint32_t old_val = inl(PCI_CONFIG_DATA);
    uint32_t shift = (offset & 3) * 8;
    uint32_t mask = 0xFF << shift;
    uint32_t new_val = (old_val & ~mask) | (((uint32_t)val) << shift);
    outl(PCI_CONFIG_DATA, new_val);
}

void pci_enable_bus_master(struct pci_device *dev) {
    uint16_t cmd = pci_read_config_16(dev->bus, dev->slot, dev->func, 0x04);
    cmd |= (PCI_COMMAND_IO | PCI_COMMAND_MEMORY | PCI_COMMAND_BUS_MASTER);
    pci_write_config_16(dev->bus, dev->slot, dev->func, 0x04, cmd);
}

static void probe_bars(struct pci_device *dev) {
    for (int b = 0; b < 6; b++) {
        uint8_t offset = 0x10 + (b * 4);
        uint32_t orig = pci_read_config_32(dev->bus, dev->slot, dev->func, offset);
        if (orig == 0xFFFFFFFF || orig == 0) {
            dev->bar[b] = 0;
            dev->bar_size[b] = 0;
            dev->bar_is_io[b] = 0;
            continue;
        }

        int is_io = (orig & 1);
        dev->bar_is_io[b] = (uint8_t)is_io;

        pci_write_config_32(dev->bus, dev->slot, dev->func, offset, 0xFFFFFFFF);
        uint32_t mask = pci_read_config_32(dev->bus, dev->slot, dev->func, offset);
        pci_write_config_32(dev->bus, dev->slot, dev->func, offset, orig);

        if (is_io) {
            dev->bar[b] = orig & ~0x3;
            dev->bar_size[b] = (~(mask & ~0x3)) + 1;
        } else {
            dev->bar[b] = orig & ~0xF;
            dev->bar_size[b] = (~(mask & ~0xF)) + 1;
        }
    }
}

void pci_init(void) {
    s_pci_device_count = 0;
    println("[PCI] Scanning PCI bus...", VGA_COLOR_LIGHT_CYAN);

    for (uint16_t bus = 0; bus < 4; bus++) {
        for (uint8_t slot = 0; slot < 32; slot++) {
            uint32_t dev_ven = pci_read_config_32(bus, slot, 0, 0x00);
            uint16_t vendor = dev_ven & 0xFFFF;
            if (vendor == 0xFFFF || vendor == 0x0000) continue;

            uint8_t header_type = pci_read_config_8(bus, slot, 0, 0x0E);
            uint8_t func_count = (header_type & 0x80) ? 8 : 1;

            for (uint8_t func = 0; func < func_count; func++) {
                uint32_t id_reg = pci_read_config_32(bus, slot, func, 0x00);
                uint16_t ven = id_reg & 0xFFFF;
                uint16_t dev = (id_reg >> 16) & 0xFFFF;
                if (ven == 0xFFFF || ven == 0x0000) continue;

                if (s_pci_device_count >= MAX_PCI_DEVICES) break;

                struct pci_device *pdev = &s_pci_devices[s_pci_device_count++];
                pdev->bus = (uint8_t)bus;
                pdev->slot = slot;
                pdev->func = func;
                pdev->vendor_id = ven;
                pdev->device_id = dev;

                uint32_t class_reg = pci_read_config_32(bus, slot, func, 0x08);
                pdev->class_code = (class_reg >> 24) & 0xFF;
                pdev->subclass = (class_reg >> 16) & 0xFF;
                pdev->prog_if = (class_reg >> 8) & 0xFF;

                pdev->irq = pci_read_config_8(bus, slot, func, 0x3C);

                probe_bars(pdev);

                print("[PCI] ", VGA_COLOR_DARK_GREY);
                print_hex(ven, VGA_COLOR_LIGHT_CYAN);
                print(":", VGA_COLOR_DARK_GREY);
                print_hex(dev, VGA_COLOR_LIGHT_CYAN);
                print(" class=", VGA_COLOR_DARK_GREY);
                print_hex(pdev->class_code, VGA_COLOR_YELLOW);
                print(" irq=", VGA_COLOR_DARK_GREY);
                print_dec(pdev->irq, VGA_COLOR_LIGHT_GREEN);
                new_line();
            }
        }
    }
}

struct pci_device *pci_find_device(uint16_t vendor, uint16_t device) {
    for (int i = 0; i < s_pci_device_count; i++) {
        if (s_pci_devices[i].vendor_id == vendor &&
            (device == 0xFFFF || s_pci_devices[i].device_id == device)) {
            return &s_pci_devices[i];
        }
    }
    return 0;
}

struct pci_device *pci_find_class(uint8_t class_code, uint8_t subclass) {
    for (int i = 0; i < s_pci_device_count; i++) {
        if (s_pci_devices[i].class_code == class_code &&
            (subclass == 0xFF || s_pci_devices[i].subclass == subclass)) {
            return &s_pci_devices[i];
        }
    }
    return 0;
}
