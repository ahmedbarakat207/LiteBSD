#include "include/netstack.h"
#include "include/socket.h"
#include "include/heap.h"
#include "include/tty.h"

#define ARP_TABLE_SIZE 32

struct arp_entry {
    uint32_t ip;
    uint8_t  mac[6];
    int      valid;
};

static struct arp_entry s_arp_table[ARP_TABLE_SIZE];
static uint16_t s_ip_id = 1000;
static const uint8_t s_bcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

uint16_t net_checksum(const void *buf, uint16_t len) {
    const uint16_t *p = (const uint16_t *)buf;
    uint32_t sum = 0;
    while (len > 1) {
        sum += *p++;
        len -= 2;
    }
    if (len == 1) {
        sum += *(const uint8_t *)p;
    }
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    return (uint16_t)(~sum);
}


void netstack_init(void) {
    for (int i = 0; i < ARP_TABLE_SIZE; i++) {
        s_arp_table[i].valid = 0;
    }
    socket_subsystem_init();
}

void arp_add_entry(uint32_t ip, const uint8_t *mac) {
    if (!mac) return;
    int free_idx = -1;
    for (int i = 0; i < ARP_TABLE_SIZE; i++) {
        if (s_arp_table[i].valid && s_arp_table[i].ip == ip) {
            for (int m = 0; m < 6; m++) s_arp_table[i].mac[m] = mac[m];
            return;
        }
        if (!s_arp_table[i].valid && free_idx < 0) {
            free_idx = i;
        }
    }
    if (free_idx >= 0) {
        s_arp_table[free_idx].ip = ip;
        for (int m = 0; m < 6; m++) s_arp_table[free_idx].mac[m] = mac[m];
        s_arp_table[free_idx].valid = 1;
    }
}

int arp_resolve(struct net_device *dev, uint32_t target_ip, uint8_t *out_mac) {
    if (!dev || !out_mac) return 0;

    if (target_ip == 0xFFFFFFFF) {
        for (int m = 0; m < 6; m++) out_mac[m] = 0xFF;
        return 1;
    }
    for (int i = 0; i < ARP_TABLE_SIZE; i++) {
        if (s_arp_table[i].valid && s_arp_table[i].ip == target_ip) {
            for (int m = 0; m < 6; m++) out_mac[m] = s_arp_table[i].mac[m];
            return 1;
        }
    }

    struct arp_hdr arp;
    arp.hw_type = net_htons(1);         // ethernet
    arp.proto_type = net_htons(0x0800);  // IPv4
    arp.hw_len = 6;
    arp.proto_len = 4;
    arp.opcode = net_htons(1);          // ARP request
    for (int m = 0; m < 6; m++) arp.sender_mac[m] = dev->mac[m];
    arp.sender_ip = dev->ip;
    for (int m = 0; m < 6; m++) arp.target_mac[m] = 0;
    arp.target_ip = target_ip;

    ethernet_send(dev, s_bcast_mac, ETHERTYPE_ARP, &arp, sizeof(arp));

    for (int retry = 0; retry < 200; retry++) {
        if (dev->poll) dev->poll(dev);
        for (int i = 0; i < ARP_TABLE_SIZE; i++) {
            if (s_arp_table[i].valid && s_arp_table[i].ip == target_ip) {
                for (int m = 0; m < 6; m++) out_mac[m] = s_arp_table[i].mac[m];
                return 1;
            }
        }
        for (volatile int d = 0; d < 10000; d++) asm volatile("pause");
    }

    // No fabricated fallback: a wrong MAC poisons the cache and breaks
    // the interface until reboot. Fail so the next packet re-ARPs fresh
    // (a late reply gets cached by arp_rx and heals automatically).
    return 0;
}

int ethernet_send(struct net_device *dev, const uint8_t *dst_mac, uint16_t ethertype, const void *payload, uint16_t len) {
    if (!dev || !payload || len > 1500) return -1;

    uint8_t frame[1514];
    struct eth_hdr *eth = (struct eth_hdr *)frame;
    for (int i = 0; i < 6; i++) {
        eth->dst[i] = dst_mac[i];
        eth->src[i] = dev->mac[i];
    }
    eth->type = net_htons(ethertype);

    const uint8_t *src = (const uint8_t *)payload;
    for (uint16_t i = 0; i < len; i++) {
        frame[sizeof(struct eth_hdr) + i] = src[i];
    }

    return dev->send(dev, frame, sizeof(struct eth_hdr) + len);
}

int ipv4_send(struct net_device *dev, uint32_t dst_ip, uint8_t proto, const void *payload, uint16_t len) {
    if (!dev || !payload || len > 1480) return -1;

    uint32_t next_hop = dst_ip;
    if (dst_ip != 0xFFFFFFFF && ((dst_ip & dev->netmask) != (dev->ip & dev->netmask))) {
        next_hop = dev->gateway;
    }

    uint8_t next_mac[6];
    if (!arp_resolve(dev, next_hop, next_mac)) {
        return -1;
    }

    uint8_t packet[1500];
    struct ip_hdr *ip = (struct ip_hdr *)packet;
    ip->ihl_ver = 0x45; // version 4, Header length 5 (20 bytes)
    ip->tos = 0;
    ip->len = net_htons(20 + len);
    ip->id = net_htons(s_ip_id++);
    ip->frag_off = 0;
    ip->ttl = 64;
    ip->proto = proto;
    ip->checksum = 0;
    ip->src = dev->ip;
    ip->dst = dst_ip;
    ip->checksum = net_checksum(ip, 20);

    const uint8_t *src = (const uint8_t *)payload;
    for (uint16_t i = 0; i < len; i++) {
        packet[20 + i] = src[i];
    }

    return ethernet_send(dev, next_mac, ETHERTYPE_IPV4, packet, 20 + len);
}

static void arp_rx(struct net_device *dev, const void *buf, uint16_t len) {
    if (len < sizeof(struct arp_hdr)) return;
    const struct arp_hdr *arp = (const struct arp_hdr *)buf;

    uint16_t opcode = net_ntohs(arp->opcode);
    arp_add_entry(arp->sender_ip, arp->sender_mac);

    if (opcode == 1) { // ARP request
        if (arp->target_ip == dev->ip) {
            struct arp_hdr reply;
            reply.hw_type = net_htons(1);
            reply.proto_type = net_htons(0x0800);
            reply.hw_len = 6;
            reply.proto_len = 4;
            reply.opcode = net_htons(2); // ARP reply
            for (int m = 0; m < 6; m++) {
                reply.sender_mac[m] = dev->mac[m];
                reply.target_mac[m] = arp->sender_mac[m];
            }
            reply.sender_ip = dev->ip;
            reply.target_ip = arp->sender_ip;

            ethernet_send(dev, arp->sender_mac, ETHERTYPE_ARP, &reply, sizeof(reply));
        }
    }
}

static void icmp_rx(struct net_device *dev, uint32_t src_ip, const void *buf, uint16_t len) {
    if (len < sizeof(struct icmp_hdr)) return;
    const struct icmp_hdr *icmp = (const struct icmp_hdr *)buf;

    if (icmp->type == 8) { // echo request
        uint8_t reply_buf[1500];
        if (len > sizeof(reply_buf)) len = sizeof(reply_buf);

        const uint8_t *src = (const uint8_t *)buf;
        for (uint16_t i = 0; i < len; i++) reply_buf[i] = src[i];

        struct icmp_hdr *reply = (struct icmp_hdr *)reply_buf;
        reply->type = 0; // echo reply
        reply->code = 0;
        reply->checksum = 0;
        reply->checksum = net_checksum(reply_buf, len);

        ipv4_send(dev, src_ip, IPPROTO_ICMP, reply_buf, len);
    }

    socket_icmp_input(src_ip, icmp->type, icmp->code, net_ntohs(icmp->id), net_ntohs(icmp->seq),
                      (const uint8_t *)buf + sizeof(struct icmp_hdr), len - sizeof(struct icmp_hdr));
}

static void ipv4_rx(struct net_device *dev, const void *buf, uint16_t len) {
    if (len < sizeof(struct ip_hdr)) return;
    const struct ip_hdr *ip = (const struct ip_hdr *)buf;

    uint8_t ver = (ip->ihl_ver >> 4) & 0x0F;
    uint8_t ihl = (ip->ihl_ver & 0x0F) * 4;
    if (ver != 4 || ihl < 20 || len < ihl) return;

    uint16_t total_len = net_ntohs(ip->len);
    if (total_len > len) total_len = len;
    uint16_t payload_len = total_len - ihl;
    const uint8_t *payload = (const uint8_t *)buf + ihl;

    if (ip->proto == IPPROTO_ICMP) {
        icmp_rx(dev, ip->src, payload, payload_len);
    } else if (ip->proto == IPPROTO_UDP) {
        if (payload_len >= sizeof(struct udp_hdr)) {
            const struct udp_hdr *udp = (const struct udp_hdr *)payload;
            uint16_t udp_len = net_ntohs(udp->len);
            if (udp_len > payload_len) udp_len = payload_len;
            uint16_t data_len = (udp_len >= sizeof(struct udp_hdr)) ? (udp_len - sizeof(struct udp_hdr)) : 0;
            socket_udp_input(ip->src, net_ntohs(udp->src_port), ip->dst, net_ntohs(udp->dst_port),
                             payload + sizeof(struct udp_hdr), data_len);
        }
    } else if (ip->proto == IPPROTO_TCP) {
        if (payload_len >= sizeof(struct tcp_hdr)) {
            const struct tcp_hdr *tcp = (const struct tcp_hdr *)payload;
            uint8_t tcp_hdr_len = ((tcp->offset >> 4) & 0x0F) * 4;
            if (tcp_hdr_len >= 20 && payload_len >= tcp_hdr_len) {
                uint16_t data_len = payload_len - tcp_hdr_len;
                socket_tcp_input(ip->src, net_ntohs(tcp->src_port), ip->dst, net_ntohs(tcp->dst_port),
                                 tcp, payload + tcp_hdr_len, data_len);
            }
        }
    }
}

void ethernet_rx(struct net_device *dev, const void *buf, uint16_t len) {
    if (!dev || !buf || len < sizeof(struct eth_hdr)) return;
    const struct eth_hdr *eth = (const struct eth_hdr *)buf;
    uint16_t ethertype = net_ntohs(eth->type);

    const void *payload = (const uint8_t *)buf + sizeof(struct eth_hdr);
    uint16_t payload_len = len - sizeof(struct eth_hdr);

    if (ethertype == ETHERTYPE_ARP) {
        arp_rx(dev, payload, payload_len);
    } else if (ethertype == ETHERTYPE_IPV4) {
        ipv4_rx(dev, payload, payload_len);
    }
}
