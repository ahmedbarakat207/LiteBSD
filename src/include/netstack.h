#ifndef _NETSTACK_H
#define _NETSTACK_H

#include <stdint.h>
#include "drivers/include/netdev.h"

#define ETHERTYPE_IPV4 0x0800
#define ETHERTYPE_ARP  0x0806

#define IPPROTO_ICMP 1
#define IPPROTO_TCP  6
#define IPPROTO_UDP  17

#define TCP_FLAG_FIN 0x01
#define TCP_FLAG_SYN 0x02
#define TCP_FLAG_RST 0x04
#define TCP_FLAG_PSH 0x08
#define TCP_FLAG_ACK 0x10
#define TCP_FLAG_URG 0x20

struct eth_hdr {
    uint8_t  dst[6];
    uint8_t  src[6];
    uint16_t type;
} __attribute__((packed));

struct arp_hdr {
    uint16_t hw_type;
    uint16_t proto_type;
    uint8_t  hw_len;
    uint8_t  proto_len;
    uint16_t opcode;
    uint8_t  sender_mac[6];
    uint32_t sender_ip;
    uint8_t  target_mac[6];
    uint32_t target_ip;
} __attribute__((packed));

struct ip_hdr {
    uint8_t  ihl_ver;
    uint8_t  tos;
    uint16_t len;
    uint16_t id;
    uint16_t frag_off;
    uint8_t  ttl;
    uint8_t  proto;
    uint16_t checksum;
    uint32_t src;
    uint32_t dst;
} __attribute__((packed));

struct icmp_hdr {
    uint8_t  type;
    uint8_t  code;
    uint16_t checksum;
    uint16_t id;
    uint16_t seq;
} __attribute__((packed));

struct udp_hdr {
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t len;
    uint16_t checksum;
} __attribute__((packed));

struct tcp_hdr {
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t seq;
    uint32_t ack;
    uint8_t  offset;
    uint8_t  flags;
    uint16_t window;
    uint16_t checksum;
    uint16_t urgent;
} __attribute__((packed));

static inline uint16_t net_htons(uint16_t x) {
    return (uint16_t)(((x & 0xFF) << 8) | ((x >> 8) & 0xFF));
}

static inline uint16_t net_ntohs(uint16_t x) {
    return net_htons(x);
}

static inline uint32_t net_htonl(uint32_t x) {
    return ((x & 0x000000FF) << 24) |
           ((x & 0x0000FF00) << 8)  |
           ((x & 0x00FF0000) >> 8)  |
           ((x & 0xFF000000) >> 24);
}

static inline uint32_t net_ntohl(uint32_t x) {
    return net_htonl(x);
}

void netstack_init(void);
void ethernet_rx(struct net_device *dev, const void *buf, uint16_t len);
int  ethernet_send(struct net_device *dev, const uint8_t *dst_mac, uint16_t ethertype, const void *payload, uint16_t len);

int  arp_resolve(struct net_device *dev, uint32_t target_ip, uint8_t *out_mac);
void arp_add_entry(uint32_t ip, const uint8_t *mac);

int  ipv4_send(struct net_device *dev, uint32_t dst_ip, uint8_t proto, const void *payload, uint16_t len);

uint16_t net_checksum(const void *buf, uint16_t len);

#endif
