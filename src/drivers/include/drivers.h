#ifndef _DRIVERS_H
#define _DRIVERS_H

#include <stdint.h>
#include "multiboot.h"
#include "fb.h"
#include "keyboard.h"
#include "pci.h"
#include "netdev.h"
#include "e1000.h"
#include "rtl8139.h"
#include "ne2k.h"

void drivers_init_early(struct mb_info *info);

void drivers_init_input(void);

void drivers_init_devices(void);

#endif /* _DRIVERS_H */
