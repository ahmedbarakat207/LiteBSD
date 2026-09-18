#include "drivers/include/netdev.h"
#include "include/heap.h"
#include "include/tty.h"

extern void ethernet_rx(struct net_device *dev, const void *buf, uint16_t len);

static struct net_device *s_devices[MAX_NET_DEVICES];
static int s_device_count = 0;
static struct net_device s_loopback_dev;

static int loopback_send(struct net_device *dev, const void *buf, uint16_t len) {
    if (!dev || !buf || len == 0) return -1;
    dev->tx_packets++;
    dev->tx_bytes += len;
    netdev_rx_packet(dev, buf, len);
    return 0;
}

void netdev_init(void) {
    s_device_count = 0;

    // Initialize loopback interface
    const char *loname = "lo";
    int i = 0;
    while (loname[i]) { s_loopback_dev.name[i] = loname[i]; i++; }
    s_loopback_dev.name[i] = '\0';

    s_loopback_dev.mac[0] = 0;
    s_loopback_dev.mac[1] = 0;
    s_loopback_dev.mac[2] = 0;
    s_loopback_dev.mac[3] = 0;
    s_loopback_dev.mac[4] = 0;
    s_loopback_dev.mac[5] = 0;

    // 127.0.0.1
    s_loopback_dev.ip = 0x0100007F;
    // 255.0.0.0
    s_loopback_dev.netmask = 0x000000FF;
    s_loopback_dev.gateway = 0;
    s_loopback_dev.dns = 0;
    s_loopback_dev.mtu = 65535;
    s_loopback_dev.flags = IFF_UP | IFF_LOOPBACK | IFF_RUNNING;
    s_loopback_dev.send = loopback_send;
    s_loopback_dev.poll = 0;

    netdev_register(&s_loopback_dev);
}

int netdev_register(struct net_device *dev) {
    if (!dev || s_device_count >= MAX_NET_DEVICES) return -1;
    s_devices[s_device_count++] = dev;
    return 0;
}

struct net_device *netdev_get_by_name(const char *name) {
    if (!name) return 0;
    for (int i = 0; i < s_device_count; i++) {
        const char *s1 = s_devices[i]->name;
        const char *s2 = name;
        int match = 1;
        while (*s1 && *s2) {
            if (*s1 != *s2) { match = 0; break; }
            s1++; s2++;
        }
        if (match && *s1 == '\0' && *s2 == '\0') {
            return s_devices[i];
        }
    }
    return 0;
}

struct net_device *netdev_get_default(void) {
    // Return first non-loopback device
    for (int i = 0; i < s_device_count; i++) {
        if (!(s_devices[i]->flags & IFF_LOOPBACK)) {
            return s_devices[i];
        }
    }
    // Fallback to loopback if no physical device
    if (s_device_count > 0) return s_devices[0];
    return 0;
}

int netdev_get_count(void) {
    return s_device_count;
}

struct net_device *netdev_get_by_index(int index) {
    if (index >= 0 && index < s_device_count) {
        return s_devices[index];
    }
    return 0;
}

void netdev_poll_all(void) {
    for (int i = 0; i < s_device_count; i++) {
        if (s_devices[i]->poll) {
            s_devices[i]->poll(s_devices[i]);
        }
    }
}

void netdev_rx_packet(struct net_device *dev, const void *buf, uint16_t len) {
    if (!dev || !buf || len < 14) return;
    dev->rx_packets++;
    dev->rx_bytes += len;
    ethernet_rx(dev, buf, len);
}
