#ifndef _RTL8139_H
#define _RTL8139_H

#include "pci.h"
#include "netdev.h"

int rtl8139_init(struct pci_device *pdev);

#endif
