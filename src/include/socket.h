#ifndef _SOCKET_H
#define _SOCKET_H

#include <stdint.h>
#include <stddef.h>

#define AF_UNSPEC 0
#define AF_UNIX   1
#define AF_LOCAL  AF_UNIX
#define AF_INET   2

#define SOCK_STREAM 1
#define SOCK_DGRAM  2
#define SOCK_RAW    3

#define IPPROTO_IP   0
#define IPPROTO_ICMP 1
#define IPPROTO_TCP  6
#define IPPROTO_UDP  17
#define IPPROTO_RAW  255

#define SOL_SOCKET   1
#define SO_REUSEADDR 2
#define SO_ERROR     4
#define SO_RCVTIMEO  20
#define SO_SNDTIMEO  21

#define TCP_STATE_CLOSED      0
#define TCP_STATE_SYN_SENT    1
#define TCP_STATE_ESTABLISHED 2
#define TCP_STATE_FIN_WAIT1   3
#define TCP_STATE_FIN_WAIT2   4
#define TCP_STATE_CLOSE_WAIT  5
#define TCP_STATE_LAST_ACK    6
#define TCP_STATE_TIME_WAIT   7
#define TCP_STATE_LISTEN      8

#define SOCKET_RX_BUF_SIZE 65536
#define MAX_DGRAM_QUEUE 16

struct dgram_packet {
    uint32_t src_ip;
    uint16_t src_port;
    uint16_t len;
    uint8_t  data[1500];
};

struct socket {
    int      domain;
    int      type;
    int      protocol;
    int      state;

    uint32_t local_ip;
    uint16_t local_port;
    uint32_t remote_ip;
    uint16_t remote_port;

    // TCP state variables
    int      tcp_state;
    uint32_t local_seq;
    uint32_t remote_seq;
    uint32_t ack_seq;
    uint16_t remote_window;
    int      fin_received;

    // Stream RX ring buffer (TCP)
    uint8_t  *rx_stream_buf;
    uint32_t rx_head;
    uint32_t rx_tail;
    uint32_t rx_count;

    // Datagram queue (UDP, ICMP)
    struct dgram_packet dgram_queue[MAX_DGRAM_QUEUE];
    int      dgram_head;
    int      dgram_tail;
    int      dgram_count;

    // Options and flags
    int      flags;
    int      nonblocking;
    int      error;
    int      ref_count;

    // Backlog for listening sockets
    struct socket *accept_queue[8];
    int      accept_count;
    struct socket *parent_listen;
};

void socket_subsystem_init(void);

struct socket *sock_create(int domain, int type, int protocol);
void sock_destroy(struct socket *sock);

int sock_bind(struct socket *sock, uint32_t ip, uint16_t port);
int sock_connect(struct socket *sock, uint32_t ip, uint16_t port);
int sock_listen(struct socket *sock, int backlog);
struct socket *sock_accept(struct socket *sock, uint32_t *client_ip, uint16_t *client_port);

int sock_sendto(struct socket *sock, const void *buf, size_t len, int flags, uint32_t dst_ip, uint16_t dst_port);
int sock_recvfrom(struct socket *sock, void *buf, size_t len, int flags, uint32_t *src_ip, uint16_t *src_port);

int sock_close(struct socket *sock);
int sock_poll(struct socket *sock, int events);

// Protocol engine callbacks into socket subsystem
void socket_tcp_input(uint32_t src_ip, uint16_t src_port, uint32_t dst_ip, uint16_t dst_port,
                      const void *tcp_hdr, const void *payload, uint16_t len);

void socket_udp_input(uint32_t src_ip, uint16_t src_port, uint32_t dst_ip, uint16_t dst_port,
                      const void *payload, uint16_t len);

void socket_icmp_input(uint32_t src_ip, uint8_t type, uint8_t code, uint16_t id, uint16_t seq,
                       const void *payload, uint16_t len);

#endif
