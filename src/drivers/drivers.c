#include "drivers/include/drivers.h"
#include "include/tty.h"

#ifndef NULL
#define NULL ((void*)0)
#endif

void drivers_init_early(struct mb_info *info) {
    fb_init(info);
}

void drivers_init_input(void) {
    keyboard_init();
}

void drivers_init_devices(void) {
    pci_init();
    netdev_init();

    int nic_found = 0;

    struct pci_device *pdev_e1000 = pci_find_device(0x8086, 0x100E);
    if (!pdev_e1000) pdev_e1000 = pci_find_device(0x8086, 0x100F);
    if (!pdev_e1000) pdev_e1000 = pci_find_device(0x8086, 0x10D3);
    if (pdev_e1000) {
        if (e1000_init(pdev_e1000) == 0) nic_found = 1;
    }

    struct pci_device *pdev_ne2k = pci_find_device(0x10EC, 0x8029);
    if (!pdev_ne2k) pdev_ne2k = pci_find_device(0x1050, 0x0940);
    if (pdev_ne2k) {
        if (ne2k_init_pci(pdev_ne2k) == 0) nic_found = 1;
    }

    struct pci_device *pdev_rtl = pci_find_device(0x10EC, 0x8139);
    if (pdev_rtl) {
        if (rtl8139_init(pdev_rtl) == 0) nic_found = 1;
    }

    if (!nic_found) {
        ne2k_init_isa(0x300, 9);
    }
}
