#ifndef _E1000_H
#define _E1000_H

#include "pci.h"
#include "netdev.h"

int e1000_init(struct pci_device *pdev);

#endif
