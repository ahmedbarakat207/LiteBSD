#include "drivers/include/e1000.h"
#include "include/heap.h"
#include "include/idt.h"
#include "include/tty.h"

#define REG_CTRL    0x0000
#define REG_STATUS  0x0008
#define REG_EECD    0x0010
#define REG_ICR     0x00C0
#define REG_IMS     0x00D0
#define REG_IMC     0x00D8
#define REG_RCTL    0x0100
#define REG_TCTL    0x0400
#define REG_TIPG    0x0410
#define REG_RDBAL   0x2800
#define REG_RDBAH   0x2804
#define REG_RDLEN   0x2808
#define REG_RDH     0x2810
#define REG_RDT     0x2818
#define REG_TDBAL   0x3800
#define REG_TDBAH   0x3804
#define REG_TDLEN   0x3808
#define REG_TDH     0x3810
#define REG_TDT     0x3818
#define REG_RAL     0x5400
#define REG_RAH     0x5404

#define CTRL_SLU    (1 << 6)
#define CTRL_RST    (1 << 26)

#define RCTL_EN     (1 << 1)
#define RCTL_SBP    (1 << 2)
#define RCTL_UPE    (1 << 3)
#define RCTL_MPE    (1 << 4)
#define RCTL_BAM    (1 << 15)
#define RCTL_BSIZE_2048 (0 << 16)
#define RCTL_SECRC  (1 << 26)

#define TCTL_EN     (1 << 1)
#define TCTL_PSP    (1 << 3)

#define NUM_RX_DESC 32
#define NUM_TX_DESC 16
#define RX_BUFFER_SIZE 2048

struct e1000_rx_desc {
    volatile uint64_t addr;
    volatile uint16_t length;
    volatile uint16_t checksum;
    volatile uint8_t  status;
    volatile uint8_t  errors;
    volatile uint16_t special;
} __attribute__((packed));

struct e1000_tx_desc {
    volatile uint64_t addr;
    volatile uint16_t length;
    volatile uint8_t  cso;
    volatile uint8_t  cmd;
    volatile uint8_t  status;
    volatile uint8_t  css;
    volatile uint16_t special;
} __attribute__((packed));

static volatile uint8_t *s_mmio_base = 0;
static struct net_device s_e1000_dev;
static struct e1000_rx_desc *s_rx_descs = 0;
static struct e1000_tx_desc *s_tx_descs = 0;
static uint8_t *s_rx_buffers[NUM_RX_DESC];
static uint8_t *s_tx_buffers[NUM_TX_DESC];
static uint16_t s_rx_cur = 0;
static uint16_t s_tx_cur = 0;

static inline void e1000_write(uint16_t reg, uint32_t val) {
    *(volatile uint32_t *)(s_mmio_base + reg) = val;
}

static inline uint32_t e1000_read(uint16_t reg) {
    return *(volatile uint32_t *)(s_mmio_base + reg);
}

static int e1000_send_packet(struct net_device *dev, const void *buf, uint16_t len) {
    (void)dev;
    if (!buf || len == 0 || len > RX_BUFFER_SIZE) return -1;

    uint16_t cur = s_tx_cur;
    struct e1000_tx_desc *desc = &s_tx_descs[cur];

    // Wait if previous transmission still in flight
    int timeout = 100000;
    while (!(desc->status & 1) && desc->cmd != 0 && timeout--) {
        asm volatile("pause");
    }

    // Copy to tx buffer
    uint8_t *dst = s_tx_buffers[cur];
    const uint8_t *src = (const uint8_t *)buf;
    for (uint16_t i = 0; i < len; i++) {
        dst[i] = src[i];
    }

    desc->length = len;
    desc->cmd = (1 << 0) | (1 << 1) | (1 << 3); // EOP | IFCS | RS
    desc->status = 0;

    s_tx_cur = (cur + 1) % NUM_TX_DESC;
    e1000_write(REG_TDT, s_tx_cur);

    dev->tx_packets++;
    dev->tx_bytes += len;
    return 0;
}

static void e1000_poll_rx(struct net_device *dev) {
    while (s_rx_descs[s_rx_cur].status & 1) { // Descriptor Done (DD)
        uint16_t cur = s_rx_cur;
        uint16_t len = s_rx_descs[cur].length;
        uint8_t *buf = s_rx_buffers[cur];

        if (len > 0) {
            netdev_rx_packet(dev, buf, len);
        }

        s_rx_descs[cur].status = 0;
        s_rx_cur = (cur + 1) % NUM_RX_DESC;
        e1000_write(REG_RDT, cur);
    }
}

static void e1000_irq_handler(void) {
    uint32_t icr = e1000_read(REG_ICR);
    if (icr) {
        e1000_poll_rx(&s_e1000_dev);
    }
}

static void e1000_poll_wrapper(struct net_device *dev) {
    e1000_poll_rx(dev);
}

int e1000_init(struct pci_device *pdev) {
    if (!pdev || pdev->bar_is_io[0]) return -1;

    s_mmio_base = (volatile uint8_t *)pdev->bar[0];
    pci_enable_bus_master(pdev);

    println("[E1000] Initializing Intel 82540EM Ethernet controller...", VGA_COLOR_LIGHT_CYAN);

    // Reset device
    e1000_write(REG_CTRL, e1000_read(REG_CTRL) | CTRL_RST);
    for (volatile int i = 0; i < 50000; i++) asm volatile("pause");
    e1000_write(REG_CTRL, (e1000_read(REG_CTRL) & ~CTRL_RST) | CTRL_SLU);

    // Read MAC address from RAL/RAH
    uint32_t ral = e1000_read(REG_RAL);
    uint32_t rah = e1000_read(REG_RAH);

    s_e1000_dev.mac[0] = ral & 0xFF;
    s_e1000_dev.mac[1] = (ral >> 8) & 0xFF;
    s_e1000_dev.mac[2] = (ral >> 16) & 0xFF;
    s_e1000_dev.mac[3] = (ral >> 24) & 0xFF;
    s_e1000_dev.mac[4] = rah & 0xFF;
    s_e1000_dev.mac[5] = (rah >> 8) & 0xFF;

    // Allocate descriptors
    s_rx_descs = (struct e1000_rx_desc *)kmalloc(sizeof(struct e1000_rx_desc) * NUM_RX_DESC + 16);
    // 16-byte align
    s_rx_descs = (struct e1000_rx_desc *)(((uint32_t)s_rx_descs + 15) & ~15);

    s_tx_descs = (struct e1000_tx_desc *)kmalloc(sizeof(struct e1000_tx_desc) * NUM_TX_DESC + 16);
    s_tx_descs = (struct e1000_tx_desc *)(((uint32_t)s_tx_descs + 15) & ~15);

    for (int i = 0; i < NUM_RX_DESC; i++) {
        s_rx_buffers[i] = (uint8_t *)kmalloc(RX_BUFFER_SIZE);
        s_rx_descs[i].addr = (uint32_t)s_rx_buffers[i];
        s_rx_descs[i].status = 0;
    }

    for (int i = 0; i < NUM_TX_DESC; i++) {
        s_tx_buffers[i] = (uint8_t *)kmalloc(RX_BUFFER_SIZE);
        s_tx_descs[i].addr = (uint32_t)s_tx_buffers[i];
        s_tx_descs[i].cmd = 0;
        s_tx_descs[i].status = 1; // DD
    }

    // Configure RX
    e1000_write(REG_RDBAL, (uint32_t)s_rx_descs);
    e1000_write(REG_RDBAH, 0);
    e1000_write(REG_RDLEN, NUM_RX_DESC * sizeof(struct e1000_rx_desc));
    e1000_write(REG_RDH, 0);
    e1000_write(REG_RDT, NUM_RX_DESC - 1);
    s_rx_cur = 0;

    e1000_write(REG_RCTL, RCTL_EN | RCTL_SBP | RCTL_UPE | RCTL_MPE | RCTL_BAM | RCTL_BSIZE_2048 | RCTL_SECRC);

    // Configure TX
    e1000_write(REG_TDBAL, (uint32_t)s_tx_descs);
    e1000_write(REG_TDBAH, 0);
    e1000_write(REG_TDLEN, NUM_TX_DESC * sizeof(struct e1000_tx_desc));
    e1000_write(REG_TDH, 0);
    e1000_write(REG_TDT, 0);
    s_tx_cur = 0;

    e1000_write(REG_TIPG, 0x0060200A); // Standard IPG
    e1000_write(REG_TCTL, TCTL_EN | TCTL_PSP | (15 << 4) | (64 << 12));

    // Interrupts
    if (pdev->irq > 0 && pdev->irq < 16) {
        register_irq_handler(pdev->irq, e1000_irq_handler);
        e1000_write(REG_IMS, 0x1F6DC); // Enable all standard interrupts
        e1000_read(REG_ICR);           // Clear pending
    }

    // Setup net_device
    const char *name = "eth0";
    int n = 0;
    while (name[n]) { s_e1000_dev.name[n] = name[n]; n++; }
    s_e1000_dev.name[n] = '\0';

    s_e1000_dev.mtu = 1500;
    s_e1000_dev.flags = IFF_UP | IFF_BROADCAST | IFF_RUNNING | IFF_MULTICAST;
    // Default QEMU network configuration: 10.0.2.15/24, gateway 10.0.2.2, DNS 10.0.2.3
    // 10.0.2.15 -> 0x0F02000A in network byte order
    s_e1000_dev.ip = (10) | (0 << 8) | (2 << 16) | (15 << 24);
    // 255.255.255.0 -> 0x00FFFFFF
    s_e1000_dev.netmask = (255) | (255 << 8) | (255 << 16) | (0 << 24);
    // 10.0.2.2 -> 0x0202000A
    s_e1000_dev.gateway = (10) | (0 << 8) | (2 << 16) | (2 << 24);
    // 10.0.2.3 -> 0x0302000A
    s_e1000_dev.dns = (10) | (0 << 8) | (2 << 16) | (3 << 24);

    s_e1000_dev.send = e1000_send_packet;
    s_e1000_dev.poll = e1000_poll_wrapper;

    netdev_register(&s_e1000_dev);

    print("[E1000] Registered eth0, MAC: ", VGA_COLOR_GREEN);
    for (int m = 0; m < 6; m++) {
        print_hex(s_e1000_dev.mac[m], VGA_COLOR_WHITE);
        if (m < 5) print(":", VGA_COLOR_DARK_GREY);
    }
    new_line();
    return 0;
}
