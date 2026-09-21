#include "include/socket.h"
#include "include/netstack.h"
#include "drivers/include/netdev.h"
#include "include/heap.h"
#include "include/sched.h"
#include "include/tty.h"
#include "include/time.h"
#include "include/io.h"

#define MAX_SOCKETS 32

static struct socket s_sockets[MAX_SOCKETS];
static uint16_t s_ephemeral_port = 49152;

static uint16_t next_ephemeral_port(void) {
    uint16_t p = s_ephemeral_port++;
    if (s_ephemeral_port > 65000) s_ephemeral_port = 49152;
    return p;
}

static uint16_t tcp_checksum(uint32_t src_ip, uint32_t dst_ip, const void *tcp_data, uint16_t len) {
    uint32_t sum = 0;
    const uint16_t *p = (const uint16_t *)tcp_data;
    uint16_t l = len;

    while (l > 1) {
        sum += *p++;
        l -= 2;
    }
    if (l == 1) {
        sum += *(const uint8_t *)p;
    }
    // src_ip (in memory: network order, add lower and upper 16 bits)
    sum += (uint16_t)(src_ip & 0xFFFF);
    sum += (uint16_t)(src_ip >> 16);

    // dst_ip (in memory: network order, add lower and upper 16 bits)
    sum += (uint16_t)(dst_ip & 0xFFFF);
    sum += (uint16_t)(dst_ip >> 16);

    // Protocol: (IPPROTO_TCP = 6) in upper byte -> 0x0600 on little endian
    sum += (uint16_t)net_htons(IPPROTO_TCP);

    // TCP Length: in network byte order
    sum += (uint16_t)net_htons(len);

    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }

    return (uint16_t)(~sum);
}

static int tcp_send_packet(struct socket *sock, uint8_t flags, const void *payload, uint16_t len) {
    struct net_device *dev = netdev_get_default();
    if (!dev) return -1;

    uint8_t buf[1500];
    struct tcp_hdr *tcp = (struct tcp_hdr *)buf;

    tcp->src_port = net_htons(sock->local_port);
    tcp->dst_port = net_htons(sock->remote_port);
    tcp->seq = net_htonl(sock->local_seq);
    tcp->ack = (flags & TCP_FLAG_ACK) ? net_htonl(sock->remote_seq) : 0;
    tcp->offset = (sizeof(struct tcp_hdr) / 4) << 4;
    tcp->flags = flags;
    tcp->window = net_htons(65535);
    tcp->checksum = 0;
    tcp->urgent = 0;

    const uint8_t *src = (const uint8_t *)payload;
    for (uint16_t i = 0; i < len; i++) {
        buf[sizeof(struct tcp_hdr) + i] = src[i];
    }

    uint16_t total_len = sizeof(struct tcp_hdr) + len;
    tcp->checksum = tcp_checksum(dev->ip, sock->remote_ip, buf, total_len);

    return ipv4_send(dev, sock->remote_ip, IPPROTO_TCP, buf, total_len);
}

void socket_subsystem_init(void) {
    for (int i = 0; i < MAX_SOCKETS; i++) {
        s_sockets[i].state = 0;
        s_sockets[i].ref_count = 0;
        s_sockets[i].rx_stream_buf = 0;
    }
}

struct socket *sock_create(int domain, int type, int protocol) {
    if (domain != AF_INET && domain != AF_UNIX) return 0;

    struct socket *sock = 0;
    for (int i = 0; i < MAX_SOCKETS; i++) {
        if (s_sockets[i].ref_count == 0) {
            sock = &s_sockets[i];
            break;
        }
    }
    if (!sock) return 0;

    sock->domain = domain;
    sock->type = type;
    sock->protocol = protocol;
    sock->state = 1; // Open
    sock->ref_count = 1;

    sock->local_ip = 0;
    sock->local_port = next_ephemeral_port();
    sock->remote_ip = 0;
    sock->remote_port = 0;

    sock->tcp_state = TCP_STATE_CLOSED;
    sock->local_seq = 0;
    sock->remote_seq = 0;
    sock->ack_seq = 0;
    sock->remote_window = 65535;
    sock->fin_received = 0;

    sock->rx_head = 0;
    sock->rx_tail = 0;
    sock->rx_count = 0;
    if (type == SOCK_STREAM) {
        if (!sock->rx_stream_buf) {
            sock->rx_stream_buf = (uint8_t *)kmalloc(SOCKET_RX_BUF_SIZE);
        }
    }

    sock->dgram_head = 0;
    sock->dgram_tail = 0;
    sock->dgram_count = 0;

    sock->flags = 0;
    sock->nonblocking = 0;
    sock->error = 0;
    sock->accept_count = 0;
    sock->parent_listen = 0;

    return sock;
}

void sock_destroy(struct socket *sock) {
    if (!sock) return;
    sock->state = 0;
    sock->ref_count = 0;
    sock->tcp_state = TCP_STATE_CLOSED;
}

int sock_bind(struct socket *sock, uint32_t ip, uint16_t port) {
    if (!sock) return -1;
    sock->local_ip = ip;
    sock->local_port = port;
    return 0;
}

int sock_listen(struct socket *sock, int backlog) {
    (void)backlog;
    if (!sock || sock->type != SOCK_STREAM) return -1;
    sock->tcp_state = TCP_STATE_LISTEN;
    return 0;
}

struct socket *sock_accept(struct socket *sock, uint32_t *client_ip, uint16_t *client_port) {
    if (!sock || sock->tcp_state != TCP_STATE_LISTEN) return 0;

    int timeout_ms = 5000;
    while (sock->accept_count == 0 && timeout_ms > 0) {
        netdev_poll_all();
        if (sock->nonblocking) return 0;
        for (int j = 0; j < 1000; j++) io_wait();
        timeout_ms--;
    }

    if (sock->accept_count > 0) {
        struct socket *child = sock->accept_queue[0];
        for (int i = 0; i < sock->accept_count - 1; i++) {
            sock->accept_queue[i] = sock->accept_queue[i + 1];
        }
        sock->accept_count--;
        if (client_ip) *client_ip = child->remote_ip;
        if (client_port) *client_port = child->remote_port;
        return child;
    }
    return 0;
}

int sock_connect(struct socket *sock, uint32_t ip, uint16_t port) {
    if (!sock) return -1;
    sock->remote_ip = ip;
    sock->remote_port = port;

    if (sock->type == SOCK_DGRAM || sock->type == SOCK_RAW) {
        return 0;
    }

    if (sock->type != SOCK_STREAM) return -1;

    sock->tcp_state = TCP_STATE_SYN_SENT;
    sock->local_seq = 100000 + (uint32_t)sock->local_port * 100;
    sock->remote_seq = 0;
    sock->error = 0;
    sock->fin_received = 0;

    tcp_send_packet(sock, TCP_FLAG_SYN, 0, 0);

    int timeout_ms = 4000;
    while (sock->tcp_state != TCP_STATE_ESTABLISHED && timeout_ms > 0) {
        netdev_poll_all();
        if (sock->tcp_state == TCP_STATE_ESTABLISHED) break;
        if (sock->error) return -sock->error;
        if (timeout_ms == 2000 && sock->tcp_state == TCP_STATE_SYN_SENT) {
            tcp_send_packet(sock, TCP_FLAG_SYN, 0, 0);
        }
        for (int j = 0; j < 1000; j++) io_wait();
        timeout_ms--;
    }

    if (sock->tcp_state != TCP_STATE_ESTABLISHED) {
        sock->tcp_state = TCP_STATE_CLOSED;
        sock->error = 110; // ETIMEDOUT
        return -110;
    }

    return 0;
}

int sock_sendto(struct socket *sock, const void *buf, size_t len, int flags, uint32_t dst_ip, uint16_t dst_port) {
    (void)flags;
    if (!sock || !buf || len == 0) return 0;

    uint32_t target_ip = dst_ip ? dst_ip : sock->remote_ip;
    uint16_t target_port = dst_port ? dst_port : sock->remote_port;

    if (sock->type == SOCK_STREAM) {
        if (sock->tcp_state != TCP_STATE_ESTABLISHED) return -1;

        const uint8_t *p = (const uint8_t *)buf;
        size_t remaining = len;

        while (remaining > 0) {
            uint16_t chunk = (remaining > 1460) ? 1460 : (uint16_t)remaining;
            if (tcp_send_packet(sock, TCP_FLAG_ACK | TCP_FLAG_PSH, p, chunk) < 0) {
                return (len == remaining) ? -1 : (int)(len - remaining);
            }
            sock->local_seq += chunk;
            p += chunk;
            remaining -= chunk;
        }
        return (int)len;
    } else if (sock->type == SOCK_DGRAM) {
        struct net_device *dev = netdev_get_default();
        if (!dev) return -1;

        uint8_t packet[1500];
        struct udp_hdr *udp = (struct udp_hdr *)packet;
        udp->src_port = net_htons(sock->local_port);
        udp->dst_port = net_htons(target_port);
        udp->len = net_htons(sizeof(struct udp_hdr) + (uint16_t)len);
        udp->checksum = 0;

        const uint8_t *src = (const uint8_t *)buf;
        for (size_t i = 0; i < len && i < 1472; i++) {
            packet[sizeof(struct udp_hdr) + i] = src[i];
        }

        int r = ipv4_send(dev, target_ip, IPPROTO_UDP, packet, sizeof(struct udp_hdr) + (uint16_t)len);
        return (r == 0) ? (int)len : -1;
    } else if (sock->type == SOCK_RAW) {
        struct net_device *dev = netdev_get_default();
        if (!dev) return -1;
        uint8_t proto = sock->protocol ? (uint8_t)sock->protocol : IPPROTO_ICMP;
        int r = ipv4_send(dev, target_ip, proto, buf, (uint16_t)len);
        return (r == 0) ? (int)len : -1;
    }

    return -1;
}

int sock_recvfrom(struct socket *sock, void *buf, size_t len, int flags, uint32_t *src_ip, uint16_t *src_port) {
    (void)flags;
    if (!sock || !buf || len == 0) return 0;

    if (sock->type == SOCK_STREAM) {
        int timeout_ms = 5000;
        while (sock->rx_count == 0 && timeout_ms > 0) {
            netdev_poll_all();
            if (sock->rx_count > 0) break;
            if (sock->fin_received) return 0; // EOF
            if (sock->tcp_state == TCP_STATE_CLOSED) return 0;
            if (sock->nonblocking) return -35; // EAGAIN
            for (int j = 0; j < 1000; j++) io_wait();
            timeout_ms--;
        }

        if (sock->rx_count == 0) {
            if (sock->fin_received) return 0;
            return -35; // EAGAIN (timeout)
        }

        uint32_t to_copy = (len < sock->rx_count) ? (uint32_t)len : sock->rx_count;
        uint8_t *dst = (uint8_t *)buf;
        for (uint32_t i = 0; i < to_copy; i++) {
            dst[i] = sock->rx_stream_buf[sock->rx_head];
            sock->rx_head = (sock->rx_head + 1) % SOCKET_RX_BUF_SIZE;
        }
        sock->rx_count -= to_copy;
        return (int)to_copy;
    } else { // SOCK_DGRAM or SOCK_RAW
        int timeout_ms = 3000;
        while (sock->dgram_count == 0 && timeout_ms > 0) {
            netdev_poll_all();
            if (sock->dgram_count > 0) break;
            if (sock->nonblocking) return -35; // EAGAIN
            for (int j = 0; j < 1000; j++) io_wait();
            timeout_ms--;
        }

        if (sock->dgram_count == 0) return -35; // EAGAIN (timeout)

        struct dgram_packet *pkt = &sock->dgram_queue[sock->dgram_head];
        uint32_t to_copy = (len < pkt->len) ? (uint32_t)len : pkt->len;
        uint8_t *dst = (uint8_t *)buf;
        for (uint32_t i = 0; i < to_copy; i++) {
            dst[i] = pkt->data[i];
        }
        if (src_ip) *src_ip = pkt->src_ip;
        if (src_port) *src_port = pkt->src_port;

        sock->dgram_head = (sock->dgram_head + 1) % MAX_DGRAM_QUEUE;
        sock->dgram_count--;
        return (int)to_copy;
    }
}

int sock_close(struct socket *sock) {
    if (!sock) return -1;
    if (sock->type == SOCK_STREAM && sock->tcp_state == TCP_STATE_ESTABLISHED) {
        tcp_send_packet(sock, TCP_FLAG_ACK | TCP_FLAG_FIN, 0, 0);
        sock->local_seq++;
        sock->tcp_state = TCP_STATE_FIN_WAIT1;
    }
    sock->state = 0;
    sock->ref_count = 0;
    return 0;
}

int sock_poll(struct socket *sock, int events) {
    if (!sock) return 0;
    int revents = 0;

    if (events & 0x0001) { // POLLIN
        if (sock->type == SOCK_STREAM) {
            if (sock->rx_count > 0 || sock->fin_received || sock->tcp_state == TCP_STATE_CLOSED) {
                revents |= 0x0001;
            }
        } else {
            if (sock->dgram_count > 0) revents |= 0x0001;
        }
    }

    if (events & 0x0004) { // POLLOUT
        if (sock->type == SOCK_STREAM) {
            if (sock->tcp_state == TCP_STATE_ESTABLISHED) revents |= 0x0004;
        } else {
            revents |= 0x0004;
        }
    }

    return revents;
}

void socket_tcp_input(uint32_t src_ip, uint16_t src_port, uint32_t dst_ip, uint16_t dst_port,
                      const void *tcp_hdr_raw, const void *payload, uint16_t len) {
    (void)src_port;
    (void)dst_ip;
    const struct tcp_hdr *tcp = (const struct tcp_hdr *)tcp_hdr_raw;

    for (int i = 0; i < MAX_SOCKETS; i++) {
        struct socket *s = &s_sockets[i];
        if (s->ref_count == 0 || s->type != SOCK_STREAM) continue;

        if (s->local_port == dst_port && (s->remote_ip == 0 || s->remote_ip == src_ip)) {
            if (tcp->flags & TCP_FLAG_RST) {
                s->tcp_state = TCP_STATE_CLOSED;
                s->error = 104; // ECONNRESET
                return;
            }

            if (s->tcp_state == TCP_STATE_SYN_SENT) {
                if ((tcp->flags & (TCP_FLAG_SYN | TCP_FLAG_ACK)) == (TCP_FLAG_SYN | TCP_FLAG_ACK)) {
                    s->remote_seq = net_ntohl(tcp->seq) + 1;
                    s->local_seq = net_ntohl(tcp->ack);
                    s->remote_window = net_ntohs(tcp->window);
                    s->tcp_state = TCP_STATE_ESTABLISHED;
                    tcp_send_packet(s, TCP_FLAG_ACK, 0, 0);
                    return;
                }
            } else if (s->tcp_state == TCP_STATE_ESTABLISHED ||
                       s->tcp_state == TCP_STATE_FIN_WAIT1 ||
                       s->tcp_state == TCP_STATE_FIN_WAIT2) {

                if (len > 0 && s->rx_stream_buf) {
                    const uint8_t *src = (const uint8_t *)payload;
                    for (uint16_t b = 0; b < len; b++) {
                        if (s->rx_count < SOCKET_RX_BUF_SIZE) {
                            s->rx_stream_buf[s->rx_tail] = src[b];
                            s->rx_tail = (s->rx_tail + 1) % SOCKET_RX_BUF_SIZE;
                            s->rx_count++;
                        }
                    }
                    s->remote_seq += len;
                    tcp_send_packet(s, TCP_FLAG_ACK, 0, 0);
                }

                if (tcp->flags & TCP_FLAG_FIN) {
                    s->fin_received = 1;
                    s->remote_seq++;
                    tcp_send_packet(s, TCP_FLAG_ACK, 0, 0);
                    if (s->tcp_state == TCP_STATE_ESTABLISHED) {
                        s->tcp_state = TCP_STATE_CLOSE_WAIT;
                    }
                }
            }
            return;
        }
    }
}

void socket_udp_input(uint32_t src_ip, uint16_t src_port, uint32_t dst_ip, uint16_t dst_port,
                      const void *payload, uint16_t len) {
    (void)dst_ip;
    for (int i = 0; i < MAX_SOCKETS; i++) {
        struct socket *s = &s_sockets[i];
        if (s->ref_count == 0 || s->type != SOCK_DGRAM) continue;

        if (s->local_port == dst_port || s->local_port == 0) {
            if (s->dgram_count < MAX_DGRAM_QUEUE) {
                struct dgram_packet *pkt = &s->dgram_queue[s->dgram_tail];
                pkt->src_ip = src_ip;
                pkt->src_port = src_port;
                pkt->len = (len > 1500) ? 1500 : len;
                const uint8_t *src = (const uint8_t *)payload;
                for (uint16_t b = 0; b < pkt->len; b++) pkt->data[b] = src[b];

                s->dgram_tail = (s->dgram_tail + 1) % MAX_DGRAM_QUEUE;
                s->dgram_count++;
            }
            return;
        }
    }
}

void socket_icmp_input(uint32_t src_ip, uint8_t type, uint8_t code, uint16_t id, uint16_t seq,
                       const void *payload, uint16_t len) {
    (void)code;
    for (int i = 0; i < MAX_SOCKETS; i++) {
        struct socket *s = &s_sockets[i];
        if (s->ref_count == 0 || s->type != SOCK_RAW) continue;

        if (s->protocol == IPPROTO_ICMP) {
            if (s->dgram_count < MAX_DGRAM_QUEUE) {
                struct dgram_packet *pkt = &s->dgram_queue[s->dgram_tail];
                pkt->src_ip = src_ip;
                pkt->src_port = 0;

                struct icmp_hdr *icmp = (struct icmp_hdr *)pkt->data;
                icmp->type = type;
                icmp->code = code;
                icmp->checksum = 0;
                icmp->id = net_htons(id);
                icmp->seq = net_htons(seq);

                uint16_t total = sizeof(struct icmp_hdr) + len;
                if (total > 1500) total = 1500;
                const uint8_t *src = (const uint8_t *)payload;
                for (uint16_t b = 0; b < total - sizeof(struct icmp_hdr); b++) {
                    pkt->data[sizeof(struct icmp_hdr) + b] = src[b];
                }
                pkt->len = total;

                s->dgram_tail = (s->dgram_tail + 1) % MAX_DGRAM_QUEUE;
                s->dgram_count++;
            }
        }
    }
}
