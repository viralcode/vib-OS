/*
 * UnixOS Kernel - Network Stack Implementation
 */

#include "fs/vfs.h"
#include "mm/kmalloc.h"
#include "net/net.h"
#include "printk.h"

/* Additional error codes for sockets */
#ifndef EMFILE
#define EMFILE 24
#endif
#ifndef EAFNOSUPPORT
#define EAFNOSUPPORT 97
#endif
#ifndef ESOCKTNOSUPPORT
#define ESOCKTNOSUPPORT 94
#endif
#ifndef EOPNOTSUPP
#define EOPNOTSUPP 95
#endif
#ifndef ENOTCONN
#define ENOTCONN 107
#endif

/* ===================================================================== */
/* Socket table */
/* ===================================================================== */

#define MAX_SOCKETS 256

static struct socket *socket_table[MAX_SOCKETS];
static int next_sockfd = 0;

/* ===================================================================== */
/* Byte order functions */
/* ===================================================================== */

uint16_t htons(uint16_t hostshort) {
  return ((hostshort >> 8) & 0xFF) | ((hostshort & 0xFF) << 8);
}

uint16_t ntohs(uint16_t netshort) { return htons(netshort); }

uint32_t htonl(uint32_t hostlong) {
  return ((hostlong >> 24) & 0xFF) | ((hostlong >> 8) & 0xFF00) |
         ((hostlong & 0xFF00) << 8) | ((hostlong & 0xFF) << 24);
}

uint32_t ntohl(uint32_t netlong) { return htonl(netlong); }

/* ===================================================================== */
/* IP checksum */
/* ===================================================================== */

static uint16_t __attribute__((unused)) ip_checksum(void *data, int len) {
  uint32_t sum = 0;
  uint16_t *p = data;

  while (len > 1) {
    sum += *p++;
    len -= 2;
  }

  if (len == 1) {
    sum += *(uint8_t *)p;
  }

  while (sum >> 16) {
    sum = (sum & 0xFFFF) + (sum >> 16);
  }

  return ~sum;
}

/* ===================================================================== */
/* Initialization */
/* ===================================================================== */

void net_init(void) {
  printk(KERN_INFO "NET: Initializing network stack\n");

  /* Clear socket table */
  for (int i = 0; i < MAX_SOCKETS; i++) {
    socket_table[i] = NULL;
  }

  printk(KERN_INFO "NET: TCP/IP stack initialized\n");
  printk(KERN_INFO "NET: IPv4 support enabled\n");
}

/* ===================================================================== */
/* Socket operations */
/* ===================================================================== */

static int alloc_sockfd(void) {
  for (int i = 0; i < MAX_SOCKETS; i++) {
    int fd = (next_sockfd + i) % MAX_SOCKETS;
    if (!socket_table[fd]) {
      next_sockfd = fd + 1;
      return fd;
    }
  }
  return -EMFILE;
}

int socket_create(int family, int type, int protocol) {
  if (family != AF_INET && family != AF_INET6) {
    return -EAFNOSUPPORT;
  }

  if (type != SOCK_STREAM && type != SOCK_DGRAM && type != SOCK_RAW) {
    return -ESOCKTNOSUPPORT;
  }

  int fd = alloc_sockfd();
  if (fd < 0) {
    return fd;
  }

  struct socket *sock = kzalloc(sizeof(struct socket), GFP_KERNEL);
  if (!sock) {
    return -ENOMEM;
  }

  sock->type = type;
  sock->protocol = protocol;
  sock->state = SS_UNCONNECTED;
  sock->local_addr.ss_family = family;
  sock->remote_addr.ss_family = family;
  sock->sk = NULL;

  socket_table[fd] = sock;

  printk(KERN_DEBUG "NET: Created socket %d (type=%d)\n", fd, type);

  return fd;
}

int socket_bind(int sockfd, const struct sockaddr *addr, unsigned int addrlen) {
  if (sockfd < 0 || sockfd >= MAX_SOCKETS || !socket_table[sockfd]) {
    return -EBADF;
  }

  if (!addr || addrlen < sizeof(struct sockaddr)) {
    return -EINVAL;
  }

  struct socket *sock = socket_table[sockfd];

  /* Copy address */
  uint8_t *dst = (uint8_t *)&sock->local_addr;
  const uint8_t *src = (const uint8_t *)addr;
  for (unsigned int i = 0; i < addrlen && i < sizeof(sock->local_addr); i++) {
    dst[i] = src[i];
  }

  printk(KERN_DEBUG "NET: Socket %d bound\n", sockfd);

  return 0;
}

int socket_listen(int sockfd, int backlog) {
  if (sockfd < 0 || sockfd >= MAX_SOCKETS || !socket_table[sockfd]) {
    return -EBADF;
  }

  struct socket *sock = socket_table[sockfd];

  if (sock->type != SOCK_STREAM) {
    return -EOPNOTSUPP;
  }

  (void)backlog;
  sock->state = SS_UNCONNECTED; /* Listening state would be set here */

  printk(KERN_DEBUG "NET: Socket %d listening\n", sockfd);

  return 0;
}

int socket_accept(int sockfd, struct sockaddr *addr, unsigned int *addrlen) {
  if (sockfd < 0 || sockfd >= MAX_SOCKETS || !socket_table[sockfd]) {
    return -EBADF;
  }

  (void)addr;
  (void)addrlen;

  /* Stub - not implemented */
  return -EAGAIN;
}

int socket_connect(int sockfd, const struct sockaddr *addr,
                   unsigned int addrlen) {
  if (sockfd < 0 || sockfd >= MAX_SOCKETS || !socket_table[sockfd]) {
    return -EBADF;
  }

  if (!addr || addrlen < sizeof(struct sockaddr)) {
    return -EINVAL;
  }

  struct socket *sock = socket_table[sockfd];

  /* Copy remote address */
  uint8_t *dst = (uint8_t *)&sock->remote_addr;
  const uint8_t *src = (const uint8_t *)addr;
  for (unsigned int i = 0; i < addrlen && i < sizeof(sock->remote_addr); i++) {
    dst[i] = src[i];
  }

  sock->state = SS_CONNECTING;

  /* Stub - immediate "connection" */
  sock->state = SS_CONNECTED;

  return 0;
}

ssize_t socket_send(int sockfd, const void *buf, size_t len, int flags) {
  if (sockfd < 0 || sockfd >= MAX_SOCKETS || !socket_table[sockfd]) {
    return -EBADF;
  }

  if (!buf) {
    return -EINVAL;
  }

  (void)flags;

  struct socket *sock = socket_table[sockfd];

  if (sock->state != SS_CONNECTED && sock->type == SOCK_STREAM) {
    return -ENOTCONN;
  }

  /* Stub - pretend we sent all data */
  return len;
}

ssize_t socket_recv(int sockfd, void *buf, size_t len, int flags) {
  if (sockfd < 0 || sockfd >= MAX_SOCKETS || !socket_table[sockfd]) {
    return -EBADF;
  }

  if (!buf) {
    return -EINVAL;
  }

  (void)flags;

  struct socket *sock = socket_table[sockfd];

  if (sock->state != SS_CONNECTED && sock->type == SOCK_STREAM) {
    return -ENOTCONN;
  }

  /* Stub - no data available */
  return 0;
}

int socket_close(int sockfd) {
  if (sockfd < 0 || sockfd >= MAX_SOCKETS || !socket_table[sockfd]) {
    return -EBADF;
  }

  struct socket *sock = socket_table[sockfd];

  kfree(sock);
  socket_table[sockfd] = NULL;

  return 0;
}
