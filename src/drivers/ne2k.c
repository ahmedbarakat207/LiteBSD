#include "drivers/include/ne2k.h"
#include "include/io.h"
#include "include/idt.h"
#include "include/tty.h"
#include "include/heap.h"

#define NE_CMD          0x00
#define NE_DATAPORT     0x10
#define NE_RESET        0x1F

// Page 0 (read/write)
#define NE_PSTART       0x01
#define NE_PSTOP        0x02
#define NE_BNRY         0x03
#define NE_TPSR         0x04
#define NE_TBCR0        0x05
#define NE_TBCR1        0x06
#define NE_ISR          0x07
#define NE_RSAR0        0x08
#define NE_RSAR1        0x09
#define NE_RBCR0        0x0A
#define NE_RBCR1        0x0B
#define NE_RCR          0x0C
#define NE_TCR          0x0D
#define NE_DCR          0x0E
#define NE_IMR          0x0F

// Page 1 (read/write)
#define NE_PAR0         0x01
#define NE_CURR         0x07
#define NE_MAR0         0x08

#define NE_PAGE_TX      0x40
#define NE_PAGE_START   0x46
#define NE_PAGE_STOP    0x80

static uint16_t s_ne_io_base = 0;
static uint8_t  s_ne_irq = 0;
static struct net_device s_ne_dev;
static uint8_t s_rx_buf[1600];

static void ne_dma_read(uint16_t src_addr, void *dst, uint16_t len) {
    outb(s_ne_io_base + NE_CMD, 0x22); // Page 0, abort DMA, Start
    outb(s_ne_io_base + NE_RBCR0, len & 0xFF);
    outb(s_ne_io_base + NE_RBCR1, len >> 8);
    outb(s_ne_io_base + NE_RSAR0, src_addr & 0xFF);
    outb(s_ne_io_base + NE_RSAR1, src_addr >> 8);
    outb(s_ne_io_base + NE_CMD, 0x0A); // Remote DMA read, Start

    uint16_t *dst16 = (uint16_t *)dst;
    uint16_t words = (len + 1) / 2;
    for (uint16_t i = 0; i < words; i++) {
        dst16[i] = inw(s_ne_io_base + NE_DATAPORT);
    }
}

static void ne_dma_write(uint16_t dst_addr, const void *src, uint16_t len) {
    outb(s_ne_io_base + NE_CMD, 0x22); // Page 0, abort DMA, Start
    outb(s_ne_io_base + NE_ISR, 0x40); // Clear RDC bit
    outb(s_ne_io_base + NE_RBCR0, len & 0xFF);
    outb(s_ne_io_base + NE_RBCR1, len >> 8);
    outb(s_ne_io_base + NE_RSAR0, dst_addr & 0xFF);
    outb(s_ne_io_base + NE_RSAR1, dst_addr >> 8);
    outb(s_ne_io_base + NE_CMD, 0x12); // Remote DMA write, Start

    const uint16_t *src16 = (const uint16_t *)src;
    uint16_t words = (len + 1) / 2;
    for (uint16_t i = 0; i < words; i++) {
        outw(s_ne_io_base + NE_DATAPORT, src16[i]);
    }

    int timeout = 10000;
    while (!(inb(s_ne_io_base + NE_ISR) & 0x40) && timeout--) {
        asm volatile("pause");
    }
    outb(s_ne_io_base + NE_ISR, 0x40);
}

static int ne_send_packet(struct net_device *dev, const void *buf, uint16_t len) {
    (void)dev;
    if (!buf || len == 0 || len > 1518) return -1;

    // Minimum Ethernet frame size is 60 bytes (excluding 4-byte FCS)
    uint16_t send_len = (len < 60) ? 60 : len;

    // Write packet to TX ring page
    ne_dma_write(NE_PAGE_TX << 8, buf, send_len);

    outb(s_ne_io_base + NE_CMD, 0x22); // Page 0
    outb(s_ne_io_base + NE_TPSR, NE_PAGE_TX);
    outb(s_ne_io_base + NE_TBCR0, send_len & 0xFF);
    outb(s_ne_io_base + NE_TBCR1, send_len >> 8);
    outb(s_ne_io_base + NE_CMD, 0x26); // Transmit packet

    dev->tx_packets++;
    dev->tx_bytes += send_len;
    return 0;
}

struct ne_pkt_hdr {
    uint8_t status;
    uint8_t next_page;
    uint16_t count;
} __attribute__((packed));

static void ne_poll_rx(struct net_device *dev) {
    outb(s_ne_io_base + NE_CMD, 0x62); // Page 1
    uint8_t curr = inb(s_ne_io_base + NE_CURR);
    outb(s_ne_io_base + NE_CMD, 0x22); // Page 0
    uint8_t bnry = inb(s_ne_io_base + NE_BNRY);

    while (bnry != curr) {
        uint8_t next_page = bnry + 1;
        if (next_page >= NE_PAGE_STOP) next_page = NE_PAGE_START;
        if (next_page == curr) break;

        struct ne_pkt_hdr hdr;
        ne_dma_read(next_page << 8, &hdr, sizeof(hdr));

        uint16_t pkt_len = hdr.count;
        if (pkt_len >= 4 && pkt_len <= 1518) {
            uint16_t data_len = pkt_len - 4; // Exclude 4-byte status/CRC
            ne_dma_read((next_page << 8) + 4, s_rx_buf, data_len);
            netdev_rx_packet(dev, s_rx_buf, data_len);
        }

        bnry = hdr.next_page - 1;
        if (bnry < NE_PAGE_START) bnry = NE_PAGE_STOP - 1;
        outb(s_ne_io_base + NE_BNRY, bnry);

        outb(s_ne_io_base + NE_CMD, 0x62); // Page 1
        curr = inb(s_ne_io_base + NE_CURR);
        outb(s_ne_io_base + NE_CMD, 0x22); // Page 0
    }
}

static void ne_irq_handler(void) {
    uint8_t isr = inb(s_ne_io_base + NE_ISR);
    if (isr) {
        outb(s_ne_io_base + NE_ISR, isr);
        ne_poll_rx(&s_ne_dev);
    }
}

static void ne_poll_wrapper(struct net_device *dev) {
    ne_poll_rx(dev);
}

static int ne_setup(uint16_t io_base, uint8_t irq) {
    s_ne_io_base = io_base;
    s_ne_irq = irq;

    // Reset card
    uint8_t reset_val = inb(io_base + NE_RESET);
    outb(io_base + NE_RESET, reset_val);
    for (volatile int i = 0; i < 50000; i++) asm volatile("pause");
    outb(io_base + NE_ISR, 0xFF); // Clear interrupts

    // Stop chip, Page 0, Abort DMA
    outb(io_base + NE_CMD, 0x21);

    // Data configuration: 16-bit DMA, normal FIFO threshold
    outb(io_base + NE_DCR, 0x49);

    // Clear byte counts
    outb(io_base + NE_RBCR0, 0);
    outb(io_base + NE_RBCR1, 0);

    // Receive config: Monitor mode during init
    outb(io_base + NE_RCR, 0x20);

    // Transmit config: Loopback mode
    outb(io_base + NE_TCR, 0x02);

    // Ring buffer setup
    outb(io_base + NE_TPSR, NE_PAGE_TX);
    outb(io_base + NE_PSTART, NE_PAGE_START);
    outb(io_base + NE_PSTOP, NE_PAGE_STOP);
    outb(io_base + NE_BNRY, NE_PAGE_START);

    // Clear ISR
    outb(io_base + NE_ISR, 0xFF);
    outb(io_base + NE_IMR, 0x00);

    // Switch to Page 1
    outb(io_base + NE_CMD, 0x61);
    outb(io_base + NE_CURR, NE_PAGE_START + 1);

    // Read PROM for MAC address
    uint8_t prom[32];
    ne_dma_read(0, prom, 32);

    for (int i = 0; i < 6; i++) {
        s_ne_dev.mac[i] = prom[i * 2];
    }

    // If PROM is zeroed, fallback to reading Page 1 PAR registers
    if (s_ne_dev.mac[0] == 0 && s_ne_dev.mac[1] == 0 && s_ne_dev.mac[2] == 0 &&
        s_ne_dev.mac[3] == 0 && s_ne_dev.mac[4] == 0 && s_ne_dev.mac[5] == 0) {
        outb(io_base + NE_CMD, 0x61); // Page 1
        for (int i = 0; i < 6; i++) {
            s_ne_dev.mac[i] = inb(io_base + NE_PAR0 + i);
        }
    }

    // If still blank, assign default QEMU MAC
    if (s_ne_dev.mac[0] == 0 && s_ne_dev.mac[1] == 0 && s_ne_dev.mac[2] == 0) {
        s_ne_dev.mac[0] = 0x52;
        s_ne_dev.mac[1] = 0x54;
        s_ne_dev.mac[2] = 0x00;
        s_ne_dev.mac[3] = 0x12;
        s_ne_dev.mac[4] = 0x34;
        s_ne_dev.mac[5] = 0x56;
    }

    // Write MAC to PAR registers
    outb(io_base + NE_CMD, 0x61); // Page 1
    for (int i = 0; i < 6; i++) {
        outb(io_base + NE_PAR0 + i, s_ne_dev.mac[i]);
    }

    // Set multicast filter to accept all
    for (int i = 0; i < 8; i++) {
        outb(io_base + NE_MAR0 + i, 0xFF);
    }

    // Back to Page 0, start chip
    outb(io_base + NE_CMD, 0x22);
    outb(io_base + NE_TCR, 0x00); // Normal transmit
    outb(io_base + NE_RCR, 0x04 | 0x02 | 0x08); // Broadcast, Multicast, Runt
    outb(io_base + NE_IMR, 0x01 | 0x02); // PRX, PTX interrupts

    if (irq > 0 && irq < 16) {
        register_irq_handler(irq, ne_irq_handler);
    }

    // Setup net_device
    const char *name = "eth0";
    int n = 0;
    while (name[n]) { s_ne_dev.name[n] = name[n]; n++; }
    s_ne_dev.name[n] = '\0';

    s_ne_dev.mtu = 1500;
    s_ne_dev.flags = IFF_UP | IFF_BROADCAST | IFF_RUNNING | IFF_MULTICAST;
    // 10.0.2.15
    s_ne_dev.ip = (10) | (0 << 8) | (2 << 16) | (15 << 24);
    // 255.255.255.0
    s_ne_dev.netmask = (255) | (255 << 8) | (255 << 16) | (0 << 24);
    // 10.0.2.2
    s_ne_dev.gateway = (10) | (0 << 8) | (2 << 16) | (2 << 24);
    // 10.0.2.3
    s_ne_dev.dns = (10) | (0 << 8) | (2 << 16) | (3 << 24);

    s_ne_dev.send = ne_send_packet;
    s_ne_dev.poll = ne_poll_wrapper;

    netdev_register(&s_ne_dev);

    print("[NE2K] Registered eth0 (io=0x", VGA_COLOR_GREEN);
    print_hex(io_base, VGA_COLOR_LIGHT_CYAN);
    print(", irq=", VGA_COLOR_GREEN);
    print_dec(irq, VGA_COLOR_LIGHT_GREEN);
    print(") MAC: ", VGA_COLOR_GREEN);
    for (int m = 0; m < 6; m++) {
        print_hex(s_ne_dev.mac[m], VGA_COLOR_WHITE);
        if (m < 5) print(":", VGA_COLOR_DARK_GREY);
    }
    new_line();
    return 0;
}

int ne2k_init_pci(struct pci_device *pdev) {
    if (!pdev || !pdev->bar_is_io[0]) return -1;
    pci_enable_bus_master(pdev);
    println("[NE2K] Initializing NE2000 PCI controller...", VGA_COLOR_LIGHT_CYAN);
    return ne_setup((uint16_t)pdev->bar[0], pdev->irq);
}

int ne2k_init_isa(uint16_t io_base, uint8_t irq) {
    // Probe if ISA card exists at io_base
    uint8_t orig = inb(io_base + NE_CMD);
    outb(io_base + NE_CMD, 0x21); // Stop
    for (volatile int i = 0; i < 1000; i++) asm volatile("pause");
    uint8_t test = inb(io_base + NE_CMD);
    if ((test & 0x03) != 0x01) {
        outb(io_base + NE_CMD, orig);
        return -1; // No device present
    }
    println("[NE2K] Probed NE1000/NE2000 ISA controller...", VGA_COLOR_LIGHT_CYAN);
    return ne_setup(io_base, irq);
}
