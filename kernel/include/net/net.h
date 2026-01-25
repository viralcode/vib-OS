/*
 * UnixOS Kernel - Network Stack Header
 */

#ifndef _NET_NET_H
#define _NET_NET_H

#include "types.h"

/* ===================================================================== */
/* Network constants */
/* ===================================================================== */

/* Address families */
#define AF_UNSPEC       0
#define AF_UNIX         1
#define AF_LOCAL        AF_UNIX
#define AF_INET         2
#define AF_INET6        10

/* Socket types */
#define SOCK_STREAM     1   /* TCP */
#define SOCK_DGRAM      2   /* UDP */
#define SOCK_RAW        3   /* Raw socket */
#define SOCK_SEQPACKET  5

/* Socket options */
#define SOL_SOCKET      1

#define SO_DEBUG        1
#define SO_REUSEADDR    2
#define SO_TYPE         3
#define SO_ERROR        4
#define SO_DONTROUTE    5
#define SO_BROADCAST    6
#define SO_SNDBUF       7
#define SO_RCVBUF       8
#define SO_KEEPALIVE    9
#define SO_OOBINLINE    10
#define SO_NO_CHECK     11
#define SO_PRIORITY     12
#define SO_LINGER       13
#define SO_BSDCOMPAT    14
#define SO_REUSEPORT    15

/* IP protocols */
#define IPPROTO_IP      0
#define IPPROTO_ICMP    1
#define IPPROTO_TCP     6
#define IPPROTO_UDP     17

/* Shutdown */
#define SHUT_RD         0
#define SHUT_WR         1
#define SHUT_RDWR       2

/* Special addresses */
#define INADDR_ANY      0x00000000
#define INADDR_BROADCAST 0xffffffff
#define INADDR_LOOPBACK 0x7f000001

/* ===================================================================== */
/* Socket address structures */
/* ===================================================================== */

typedef uint16_t sa_family_t;
typedef uint16_t in_port_t;
typedef uint32_t in_addr_t;

struct in_addr {
    in_addr_t s_addr;
};

struct sockaddr {
    sa_family_t sa_family;
    char sa_data[14];
};

struct sockaddr_in {
    sa_family_t sin_family;
    in_port_t sin_port;
    struct in_addr sin_addr;
    char sin_zero[8];
};

struct sockaddr_in6 {
    sa_family_t sin6_family;
    in_port_t sin6_port;
    uint32_t sin6_flowinfo;
    uint8_t sin6_addr[16];
    uint32_t sin6_scope_id;
};

struct sockaddr_storage {
    sa_family_t ss_family;
    char __ss_padding[128 - sizeof(sa_family_t)];
};

/* ===================================================================== */
/* Ethernet */
/* ===================================================================== */

#define ETH_ALEN        6
#define ETH_HLEN        14
#define ETH_DATA_LEN    1500
#define ETH_FRAME_LEN   1514

/* Ethernet types */
#define ETH_P_IP        0x0800
#define ETH_P_ARP       0x0806
#define ETH_P_IPV6      0x86DD

struct ethhdr {
    uint8_t h_dest[ETH_ALEN];
    uint8_t h_source[ETH_ALEN];
    uint16_t h_proto;
} __attribute__((packed));

/* ===================================================================== */
/* IP Header */
/* ===================================================================== */

struct iphdr {
    uint8_t version_ihl;
    uint8_t tos;
    uint16_t tot_len;
    uint16_t id;
    uint16_t frag_off;
    uint8_t ttl;
    uint8_t protocol;
    uint16_t check;
    uint32_t saddr;
    uint32_t daddr;
} __attribute__((packed));

/* ===================================================================== */
/* TCP Header */
/* ===================================================================== */

struct tcphdr {
    uint16_t source;
    uint16_t dest;
    uint32_t seq;
    uint32_t ack_seq;
    uint16_t flags;
    uint16_t window;
    uint16_t check;
    uint16_t urg_ptr;
} __attribute__((packed));

/* TCP flags */
#define TCP_FIN     0x01
#define TCP_SYN     0x02
#define TCP_RST     0x04
#define TCP_PSH     0x08
#define TCP_ACK     0x10
#define TCP_URG     0x20

/* ===================================================================== */
/* UDP Header */
/* ===================================================================== */

struct udphdr {
    uint16_t source;
    uint16_t dest;
    uint16_t len;
    uint16_t check;
} __attribute__((packed));

/* ===================================================================== */
/* Socket structure */
/* ===================================================================== */

struct socket {
    int type;
    int protocol;
    int state;
    struct sockaddr_storage local_addr;
    struct sockaddr_storage remote_addr;
    void *sk;   /* Protocol-specific data */
};

/* Socket states */
#define SS_FREE         0
#define SS_UNCONNECTED  1
#define SS_CONNECTING   2
#define SS_CONNECTED    3
#define SS_DISCONNECTING 4

/* ===================================================================== */
/* Function declarations */
/* ===================================================================== */

/**
 * net_init - Initialize network stack
 */
void net_init(void);

/**
 * socket_create - Create a socket
 * @family: Address family (AF_INET, etc.)
 * @type: Socket type (SOCK_STREAM, etc.)
 * @protocol: Protocol (usually 0)
 * 
 * Return: Socket descriptor or negative error
 */
int socket_create(int family, int type, int protocol);

/**
 * socket_bind - Bind socket to address
 * @sockfd: Socket descriptor
 * @addr: Address to bind
 * @addrlen: Address length
 * 
 * Return: 0 on success, negative error
 */
int socket_bind(int sockfd, const struct sockaddr *addr, unsigned int addrlen);

/**
 * socket_listen - Mark socket as listening
 * @sockfd: Socket descriptor
 * @backlog: Connection queue size
 * 
 * Return: 0 on success, negative error
 */
int socket_listen(int sockfd, int backlog);

/**
 * socket_accept - Accept incoming connection
 * @sockfd: Listening socket
 * @addr: Output for peer address
 * @addrlen: Address length (in/out)
 * 
 * Return: New socket descriptor or negative error
 */
int socket_accept(int sockfd, struct sockaddr *addr, unsigned int *addrlen);

/**
 * socket_connect - Connect to remote address
 * @sockfd: Socket descriptor
 * @addr: Remote address
 * @addrlen: Address length
 * 
 * Return: 0 on success, negative error
 */
int socket_connect(int sockfd, const struct sockaddr *addr, unsigned int addrlen);

/**
 * socket_send - Send data on socket
 * @sockfd: Socket descriptor
 * @buf: Data to send
 * @len: Data length
 * @flags: Send flags
 * 
 * Return: Bytes sent or negative error
 */
ssize_t socket_send(int sockfd, const void *buf, size_t len, int flags);

/**
 * socket_recv - Receive data from socket
 * @sockfd: Socket descriptor
 * @buf: Buffer for data
 * @len: Buffer size
 * @flags: Receive flags
 * 
 * Return: Bytes received or negative error
 */
ssize_t socket_recv(int sockfd, void *buf, size_t len, int flags);

/**
 * socket_close - Close socket
 * @sockfd: Socket descriptor
 * 
 * Return: 0 on success, negative error
 */
int socket_close(int sockfd);

/* Utility functions */
uint16_t htons(uint16_t hostshort);
uint16_t ntohs(uint16_t netshort);
uint32_t htonl(uint32_t hostlong);
uint32_t ntohl(uint32_t netlong);

/* ===================================================================== */
/* Network Interface */
/* ===================================================================== */

struct net_interface {
    char name[16];
    uint8_t mac[ETH_ALEN];
    uint32_t ip;
    uint32_t netmask;
    uint32_t gateway;
    bool up;
    uint64_t rx_packets;
    uint64_t tx_packets;
    uint64_t rx_bytes;
    uint64_t tx_bytes;
    
    /* Driver Send Function */
    int (*send)(struct net_interface *iface, const void *data, size_t len);
    void *priv; /* Driver private data */
};

/**
 * net_add_interface - Register a network interface
 */
struct net_interface *net_add_interface(const char *name, uint8_t *mac, uint32_t ip, 
                      uint32_t netmask, uint32_t gateway);

/**
 * net_rx - Receive a packet from a driver
 */
void net_rx(struct net_interface *iface, const void *data, size_t len);

#endif /* _NET_NET_H */
