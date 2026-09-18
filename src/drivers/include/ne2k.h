#ifndef _NE2K_H
#define _NE2K_H

#include "pci.h"
#include "netdev.h"

int ne2k_init_pci(struct pci_device *pdev);
int ne2k_init_isa(uint16_t io_base, uint8_t irq);

#endif
