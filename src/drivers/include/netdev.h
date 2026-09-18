#ifndef _NETDEV_H
#define _NETDEV_H

#include <stdint.h>

#define IFF_UP          0x1
#define IFF_BROADCAST   0x2
#define IFF_DEBUG       0x4
#define IFF_LOOPBACK    0x8
#define IFF_POINTOPOINT 0x10
#define IFF_NOTRAILERS  0x20
#define IFF_RUNNING     0x40
#define IFF_NOARP       0x80
#define IFF_PROMISC     0x100
#define IFF_ALLMULTI    0x200
#define IFF_MULTICAST   0x1000

#define MAX_NET_DEVICES 8

struct net_device {
    char     name[16];
    uint8_t  mac[6];
    uint32_t ip;        // Network byte order
    uint32_t netmask;   // Network byte order
    uint32_t gateway;   // Network byte order
    uint32_t dns;       // Network byte order
    uint16_t mtu;
    uint16_t flags;

    uint32_t rx_packets;
    uint32_t tx_packets;
    uint32_t rx_bytes;
    uint32_t tx_bytes;
    uint32_t rx_errors;
    uint32_t tx_errors;
    uint32_t rx_dropped;
    uint32_t tx_dropped;

    void *priv;

    int  (*send)(struct net_device *dev, const void *buf, uint16_t len);
    void (*poll)(struct net_device *dev);
    int  (*open)(struct net_device *dev);
    int  (*stop)(struct net_device *dev);
};

void netdev_init(void);
int netdev_register(struct net_device *dev);
struct net_device *netdev_get_by_name(const char *name);
struct net_device *netdev_get_default(void);
int netdev_get_count(void);
struct net_device *netdev_get_by_index(int index);
void netdev_poll_all(void);

// Called by drivers when an Ethernet frame is received
void netdev_rx_packet(struct net_device *dev, const void *buf, uint16_t len);

#endif
