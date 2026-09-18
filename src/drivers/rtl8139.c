#include "drivers/include/rtl8139.h"
#include "include/io.h"
#include "include/idt.h"
#include "include/tty.h"
#include "include/heap.h"

#define RTL_IDR0        0x00
#define RTL_TSD0        0x10
#define RTL_TSAD0       0x20
#define RTL_RBSTART     0x30
#define RTL_CR          0x37
#define RTL_CAPR        0x38
#define RTL_CBR         0x3A
#define RTL_IMR         0x3C
#define RTL_ISR         0x3E
#define RTL_TCR         0x40
#define RTL_RCR         0x44
#define RTL_CONFIG1     0x52

#define RTL_RX_BUF_SIZE (8192 + 16 + 1536)

static uint16_t s_rtl_io_base = 0;
static struct net_device s_rtl_dev;
static uint8_t *s_rtl_rx_buffer = 0;
static uint8_t *s_rtl_tx_buffers[4];
static uint8_t  s_rtl_tx_cur = 0;
static uint16_t s_rtl_rx_offset = 0;

static int rtl8139_send_packet(struct net_device *dev, const void *buf, uint16_t len) {
    (void)dev;
    if (!buf || len == 0 || len > 1792) return -1;

    uint8_t cur = s_rtl_tx_cur;
    uint8_t *dst = s_rtl_tx_buffers[cur];
    const uint8_t *src = (const uint8_t *)buf;

    uint16_t send_len = (len < 60) ? 60 : len;
    for (uint16_t i = 0; i < len; i++) dst[i] = src[i];
    for (uint16_t i = len; i < send_len; i++) dst[i] = 0;

    outl(s_rtl_io_base + RTL_TSAD0 + (cur * 4), (uint32_t)dst);
    outl(s_rtl_io_base + RTL_TSD0 + (cur * 4), send_len & 0x1FFF);

    s_rtl_tx_cur = (cur + 1) % 4;
    dev->tx_packets++;
    dev->tx_bytes += send_len;
    return 0;
}

static void rtl8139_poll_rx(struct net_device *dev) {
    while ((inb(s_rtl_io_base + RTL_CR) & 0x01) == 0) { // Buffer not empty
        uint8_t *pkt = s_rtl_rx_buffer + s_rtl_rx_offset;
        uint16_t status = *(uint16_t *)pkt;
        uint16_t len = *(uint16_t *)(pkt + 2);

        if (!(status & 0x01) || len > 1536) {
            // Bad packet or sync issue, reset buffer pointer
            break;
        }

        uint8_t *data = pkt + 4;
        uint16_t data_len = (len > 4) ? (len - 4) : 0;

        if (data_len > 0) {
            netdev_rx_packet(dev, data, data_len);
        }

        s_rtl_rx_offset = (s_rtl_rx_offset + len + 4 + 3) & ~3;
        s_rtl_rx_offset %= 8192;
        outw(s_rtl_io_base + RTL_CAPR, s_rtl_rx_offset - 16);
    }
}

static void rtl8139_irq_handler(void) {
    uint16_t isr = inw(s_rtl_io_base + RTL_ISR);
    if (isr) {
        outw(s_rtl_io_base + RTL_ISR, isr);
        rtl8139_poll_rx(&s_rtl_dev);
    }
}

static void rtl8139_poll_wrapper(struct net_device *dev) {
    rtl8139_poll_rx(dev);
}

int rtl8139_init(struct pci_device *pdev) {
    if (!pdev || !pdev->bar_is_io[0]) return -1;

    s_rtl_io_base = (uint16_t)pdev->bar[0];
    pci_enable_bus_master(pdev);

    println("[RTL8139] Initializing Realtek RTL8139 controller...", VGA_COLOR_LIGHT_CYAN);

    // Power on: write 0 to CONFIG1
    outb(s_rtl_io_base + RTL_CONFIG1, 0x00);

    // Software reset
    outb(s_rtl_io_base + RTL_CR, 0x10);
    while (inb(s_rtl_io_base + RTL_CR) & 0x10) {
        asm volatile("pause");
    }

    // Read MAC
    for (int i = 0; i < 6; i++) {
        s_rtl_dev.mac[i] = inb(s_rtl_io_base + RTL_IDR0 + i);
    }

    // Allocate RX buffer
    s_rtl_rx_buffer = (uint8_t *)kmalloc(RTL_RX_BUF_SIZE);
    outl(s_rtl_io_base + RTL_RBSTART, (uint32_t)s_rtl_rx_buffer);

    // Allocate 4 TX buffers
    for (int i = 0; i < 4; i++) {
        s_rtl_tx_buffers[i] = (uint8_t *)kmalloc(2048);
    }
    s_rtl_tx_cur = 0;
    s_rtl_rx_offset = 0;

    // Enable RX and TX
    outw(s_rtl_io_base + RTL_IMR, 0x0005); // ROK (0x01) and TOK (0x04)
    outl(s_rtl_io_base + RTL_RCR, 0x0000000F | (1 << 7)); // AB | AM | APM | AAP | WRAP
    outb(s_rtl_io_base + RTL_CR, 0x0C); // Enable RX and TX

    if (pdev->irq > 0 && pdev->irq < 16) {
        register_irq_handler(pdev->irq, rtl8139_irq_handler);
    }

    // Setup net_device
    const char *name = "eth0";
    int n = 0;
    while (name[n]) { s_rtl_dev.name[n] = name[n]; n++; }
    s_rtl_dev.name[n] = '\0';

    s_rtl_dev.mtu = 1500;
    s_rtl_dev.flags = IFF_UP | IFF_BROADCAST | IFF_RUNNING | IFF_MULTICAST;
    // 10.0.2.15
    s_rtl_dev.ip = (10) | (0 << 8) | (2 << 16) | (15 << 24);
    // 255.255.255.0
    s_rtl_dev.netmask = (255) | (255 << 8) | (255 << 16) | (0 << 24);
    // 10.0.2.2
    s_rtl_dev.gateway = (10) | (0 << 8) | (2 << 16) | (2 << 24);
    // 10.0.2.3
    s_rtl_dev.dns = (10) | (0 << 8) | (2 << 16) | (3 << 24);

    s_rtl_dev.send = rtl8139_send_packet;
    s_rtl_dev.poll = rtl8139_poll_wrapper;

    netdev_register(&s_rtl_dev);

    print("[RTL8139] Registered eth0, MAC: ", VGA_COLOR_GREEN);
    for (int m = 0; m < 6; m++) {
        print_hex(s_rtl_dev.mac[m], VGA_COLOR_WHITE);
        if (m < 5) print(":", VGA_COLOR_DARK_GREY);
    }
    new_line();
    return 0;
}
