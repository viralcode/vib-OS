/*
 * vib-OS Kernel - Full TCP/IP Network Stack
 * 
 * Complete implementation of Ethernet, ARP, IP, ICMP, UDP, TCP
 */

#include "net/net.h"
#include "printk.h"
#include "mm/kmalloc.h"
#include "types.h"

/* ===================================================================== */
/* Protocol Constants */
/* ===================================================================== */

#define ETH_ALEN            6
#define ETH_HLEN            14
#define ETH_P_IP            0x0800
#define ETH_P_ARP           0x0806
#define ETH_P_IPV6          0x86DD

#define IP_PROTO_ICMP       1
#define IP_PROTO_TCP        6
#define IP_PROTO_UDP        17

#define TCP_FIN             0x01
#define TCP_SYN             0x02
#define TCP_RST             0x04
#define TCP_PSH             0x08
#define TCP_ACK             0x10
#define TCP_URG             0x20

/* TCP States */
#define TCP_CLOSED          0
#define TCP_LISTEN          1
#define TCP_SYN_SENT        2
#define TCP_SYN_RECEIVED    3
#define TCP_ESTABLISHED     4
#define TCP_FIN_WAIT_1      5
#define TCP_FIN_WAIT_2      6
#define TCP_CLOSE_WAIT      7
#define TCP_CLOSING         8
#define TCP_LAST_ACK        9
#define TCP_TIME_WAIT       10

/* ===================================================================== */
/* Protocol Headers */
/* ===================================================================== */

struct eth_hdr {
    uint8_t  dest[ETH_ALEN];
    uint8_t  src[ETH_ALEN];
    uint16_t type;
} __attribute__((packed));

struct arp_hdr {
    uint16_t hw_type;
    uint16_t proto_type;
    uint8_t  hw_len;
    uint8_t  proto_len;
    uint16_t opcode;
    uint8_t  sender_mac[ETH_ALEN];
    uint32_t sender_ip;
    uint8_t  target_mac[ETH_ALEN];
    uint32_t target_ip;
} __attribute__((packed));

struct ip_hdr {
    uint8_t  version_ihl;
    uint8_t  tos;
    uint16_t total_len;
    uint16_t id;
    uint16_t flags_frag;
    uint8_t  ttl;
    uint8_t  protocol;
    uint16_t checksum;
    uint32_t src_ip;
    uint32_t dst_ip;
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
    uint16_t length;
    uint16_t checksum;
} __attribute__((packed));

struct tcp_hdr {
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t seq;
    uint32_t ack;
    uint8_t  data_offset;
    uint8_t  flags;
    uint16_t window;
    uint16_t checksum;
    uint16_t urgent;
} __attribute__((packed));

/* ===================================================================== */
/* Network Interface */
/* ===================================================================== */

/* ===================================================================== */
/* ARP Cache */
/* ===================================================================== */

#define ARP_CACHE_SIZE  64
#define ARP_TIMEOUT     300  /* 5 minutes */

struct arp_entry {
    uint32_t ip;
    uint8_t mac[ETH_ALEN];
    uint64_t timestamp;
    bool valid;
};

static struct arp_entry arp_cache[ARP_CACHE_SIZE];

/* Forward declarations */
static void arp_add(uint32_t ip, uint8_t *mac);
static struct arp_entry *arp_lookup(uint32_t ip);

/* ===================================================================== */
/* Network Interface Globals */
/* ===================================================================== */

#define MAX_INTERFACES  4
static struct net_interface interfaces[MAX_INTERFACES];
static int num_interfaces = 0;

/* ===================================================================== */
/* RX Handler */
/* ===================================================================== */

/* Forward declaration */
void tcp_handle_segment(uint32_t src_ip, uint32_t dst_ip,
                        struct tcp_hdr *tcp, size_t tcp_len);

/* Handle incoming ARP packets */
static void arp_handle(struct net_interface *iface, struct arp_hdr *arp)
{
    uint16_t opcode = ntohs(arp->opcode);
    
    if (opcode == 1) {
        /* ARP Request - check if it's for us */
        if (arp->target_ip == iface->ip) {
            printk(KERN_DEBUG "ARP: Request for our IP, sending reply\n");
            
            /* Build ARP reply */
            uint8_t packet[ETH_HLEN + sizeof(struct arp_hdr)];
            struct eth_hdr *eth = (struct eth_hdr *)packet;
            struct arp_hdr *reply = (struct arp_hdr *)(packet + ETH_HLEN);
            
            /* Ethernet header */
            for (int i = 0; i < ETH_ALEN; i++) {
                eth->dest[i] = arp->sender_mac[i];
                eth->src[i] = iface->mac[i];
            }
            eth->type = htons(ETH_P_ARP);
            
            /* ARP reply */
            reply->hw_type = htons(1);
            reply->proto_type = htons(ETH_P_IP);
            reply->hw_len = ETH_ALEN;
            reply->proto_len = 4;
            reply->opcode = htons(2);  /* Reply */
            for (int i = 0; i < ETH_ALEN; i++) {
                reply->sender_mac[i] = iface->mac[i];
                reply->target_mac[i] = arp->sender_mac[i];
            }
            reply->sender_ip = iface->ip;
            reply->target_ip = arp->sender_ip;
            
            if (iface->send) {
                iface->send(iface, packet, sizeof(packet));
            }
        }
    } else if (opcode == 2) {
        /* ARP Reply - add to cache */
        arp_add(arp->sender_ip, arp->sender_mac);
        printk(KERN_DEBUG "ARP: Added entry for IP\n");
    }
}

/* Handle incoming IP packets */
static void ip_handle(struct net_interface *iface, struct ip_hdr *ip, size_t len)
{
    (void)iface;
    
    /* Verify header */
    if ((ip->version_ihl >> 4) != 4) return;  /* Not IPv4 */
    
    size_t ip_hlen = (ip->version_ihl & 0xF) * 4;
    size_t total_len = ntohs(ip->total_len);
    
    if (len < total_len) return;  /* Truncated packet */
    
    uint8_t *payload = (uint8_t *)ip + ip_hlen;
    size_t payload_len = total_len - ip_hlen;
    
    switch (ip->protocol) {
        case IP_PROTO_ICMP:
            /* Handle ICMP - echo reply etc */
            {
                struct icmp_hdr *icmp = (struct icmp_hdr *)payload;
                if (icmp->type == 0) {
                    /* Echo reply */
                    printk(KERN_INFO "ICMP: Received echo reply seq=%u\n", ntohs(icmp->seq));
                } else if (icmp->type == 8) {
                    /* Echo request - send reply */
                    printk(KERN_DEBUG "ICMP: Received echo request, sending reply\n");
                    /* TODO: Send ICMP echo reply */
                }
            }
            break;
            
        case IP_PROTO_TCP:
            {
                struct tcp_hdr *tcp = (struct tcp_hdr *)payload;
                tcp_handle_segment(ip->src_ip, ip->dst_ip, tcp, payload_len);
            }
            break;
            
        case IP_PROTO_UDP:
            /* Handle UDP */
            printk(KERN_DEBUG "UDP: Received packet\n");
            break;
            
        default:
            break;
    }
}

void net_rx(struct net_interface *iface, const void *data, size_t len)
{
    if (len < sizeof(struct eth_hdr)) return;
    
    struct eth_hdr *eth = (struct eth_hdr *)data;
    uint16_t type = ntohs(eth->type);
    
    iface->rx_packets++;
    iface->rx_bytes += len;
    
    uint8_t *payload = (uint8_t *)data + ETH_HLEN;
    size_t payload_len = len - ETH_HLEN;
    
    switch (type) {
        case ETH_P_ARP:
            if (payload_len >= sizeof(struct arp_hdr)) {
                arp_handle(iface, (struct arp_hdr *)payload);
            }
            break;
            
        case ETH_P_IP:
            if (payload_len >= sizeof(struct ip_hdr)) {
                ip_handle(iface, (struct ip_hdr *)payload, payload_len);
            }
            break;
            
        default:
            /* Unknown protocol */
            break;
    }
}


/* ===================================================================== */
/* TCP Connection Table */
/* ===================================================================== */

#define MAX_TCP_CONNECTIONS 256

struct tcp_connection {
    uint32_t local_ip;
    uint16_t local_port;
    uint32_t remote_ip;
    uint16_t remote_port;
    int state;
    uint32_t seq;
    uint32_t ack;
    uint32_t recv_wnd;
    uint32_t send_wnd;
    uint8_t *recv_buf;
    size_t recv_len;
    size_t recv_capacity;
    uint8_t *send_buf;
    size_t send_len;
    size_t send_capacity;
    bool in_use;
};

static struct tcp_connection tcp_connections[MAX_TCP_CONNECTIONS];
static uint16_t next_ephemeral_port = 49152;

/* ===================================================================== */
/* Checksum Calculation */
/* ===================================================================== */

static uint16_t checksum(void *data, size_t len)
{
    uint32_t sum = 0;
    uint16_t *ptr = (uint16_t *)data;
    
    while (len > 1) {
        sum += *ptr++;
        len -= 2;
    }
    
    if (len == 1) {
        sum += *(uint8_t *)ptr;
    }
    
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    
    return ~sum;
}

static uint16_t tcp_checksum(struct ip_hdr *ip, struct tcp_hdr *tcp, size_t tcp_len)
{
    uint32_t sum = 0;
    
    /* Pseudo header */
    sum += (ip->src_ip >> 16) & 0xFFFF;
    sum += ip->src_ip & 0xFFFF;
    sum += (ip->dst_ip >> 16) & 0xFFFF;
    sum += ip->dst_ip & 0xFFFF;
    sum += htons(IP_PROTO_TCP);
    sum += htons(tcp_len);
    
    /* TCP header + data */
    uint16_t *ptr = (uint16_t *)tcp;
    size_t len = tcp_len;
    
    while (len > 1) {
        sum += *ptr++;
        len -= 2;
    }
    
    if (len == 1) {
        sum += *(uint8_t *)ptr;
    }
    
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    
    return ~sum;
}

/* ===================================================================== */
/* Byte Order - use functions from net.h */
/* ===================================================================== */


/* ===================================================================== */
/* ARP Functions */
/* ===================================================================== */

static struct arp_entry *arp_lookup(uint32_t ip)
{
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].valid && arp_cache[i].ip == ip) {
            return &arp_cache[i];
        }
    }
    return NULL;
}

static void arp_add(uint32_t ip, uint8_t *mac)
{
    /* Find empty or oldest slot */
    int oldest = 0;
    uint64_t oldest_time = ~0ULL;
    
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (!arp_cache[i].valid) {
            oldest = i;
            break;
        }
        if (arp_cache[i].timestamp < oldest_time) {
            oldest_time = arp_cache[i].timestamp;
            oldest = i;
        }
    }
    
    arp_cache[oldest].ip = ip;
    for (int i = 0; i < ETH_ALEN; i++) {
        arp_cache[oldest].mac[i] = mac[i];
    }
    arp_cache[oldest].timestamp = 0; /* TODO: get_time() */
    arp_cache[oldest].valid = true;
}

int arp_send_request(uint32_t target_ip)
{
    if (num_interfaces == 0) return -1;
    
    struct net_interface *iface = &interfaces[0];
    
    /* Build ARP request */
    uint8_t packet[ETH_HLEN + sizeof(struct arp_hdr)];
    struct eth_hdr *eth = (struct eth_hdr *)packet;
    struct arp_hdr *arp = (struct arp_hdr *)(packet + ETH_HLEN);
    
    /* Ethernet header - broadcast */
    for (int i = 0; i < ETH_ALEN; i++) {
        eth->dest[i] = 0xFF;
        eth->src[i] = iface->mac[i];
    }
    eth->type = htons(ETH_P_ARP);
    
    /* ARP header */
    arp->hw_type = htons(1);        /* Ethernet */
    arp->proto_type = htons(ETH_P_IP);
    arp->hw_len = ETH_ALEN;
    arp->proto_len = 4;
    arp->opcode = htons(1);         /* Request */
    for (int i = 0; i < ETH_ALEN; i++) {
        arp->sender_mac[i] = iface->mac[i];
        arp->target_mac[i] = 0;
    }
    arp->sender_ip = iface->ip;
    arp->target_ip = target_ip;
    
    /* Send packet via network driver */
    if (iface->send) {
        iface->send(iface, packet, sizeof(packet));
        iface->tx_packets++;
        iface->tx_bytes += sizeof(packet);
    }
    printk(KERN_DEBUG "ARP: Sending request for IP\n");
    
    return 0;
}

/* ===================================================================== */
/* ICMP Functions */
/* ===================================================================== */

int icmp_send_echo(uint32_t dest_ip, uint16_t id, uint16_t seq)
{
    if (num_interfaces == 0) return -1;
    
    printk(KERN_DEBUG "ICMP: Sending echo request\n");
    
    /* Build ICMP echo request */
    size_t total_len = ETH_HLEN + sizeof(struct ip_hdr) + sizeof(struct icmp_hdr);
    uint8_t *packet = kmalloc(total_len);
    if (!packet) return -1;
    
    struct eth_hdr *eth = (struct eth_hdr *)packet;
    struct ip_hdr *ip = (struct ip_hdr *)(packet + ETH_HLEN);
    struct icmp_hdr *icmp = (struct icmp_hdr *)(packet + ETH_HLEN + sizeof(struct ip_hdr));
    
    /* Fill headers */
    struct net_interface *iface = &interfaces[0];
    
    /* Ethernet - need ARP lookup */
    eth->type = htons(ETH_P_IP);
    for (int i = 0; i < ETH_ALEN; i++) {
        eth->src[i] = iface->mac[i];
    }
    
    /* IP header */
    ip->version_ihl = 0x45;
    ip->tos = 0;
    ip->total_len = htons(sizeof(struct ip_hdr) + sizeof(struct icmp_hdr));
    ip->id = htons(1);
    ip->flags_frag = 0;
    ip->ttl = 64;
    ip->protocol = IP_PROTO_ICMP;
    ip->src_ip = iface->ip;
    ip->dst_ip = dest_ip;
    ip->checksum = 0;
    ip->checksum = checksum(ip, sizeof(struct ip_hdr));
    
    /* ICMP header */
    icmp->type = 8;  /* Echo request */
    icmp->code = 0;
    icmp->id = htons(id);
    icmp->seq = htons(seq);
    icmp->checksum = 0;
    icmp->checksum = checksum(icmp, sizeof(struct icmp_hdr));
    
    /* Send via driver */
    if (iface->send) {
        iface->send(iface, packet, total_len);
        iface->tx_packets++;
        iface->tx_bytes += total_len;
    }
    printk(KERN_DEBUG "ICMP: Sent echo request to %08x\n", dest_ip);
    
    kfree(packet);
    return 0;
}

/* ===================================================================== */
/* TCP Functions */
/* ===================================================================== */

static struct tcp_connection *tcp_alloc_connection(void)
{
    for (int i = 0; i < MAX_TCP_CONNECTIONS; i++) {
        if (!tcp_connections[i].in_use) {
            struct tcp_connection *conn = &tcp_connections[i];
            conn->in_use = true;
            conn->state = TCP_CLOSED;
            conn->recv_capacity = 65536;
            conn->send_capacity = 65536;
            conn->recv_buf = kmalloc(conn->recv_capacity);
            conn->send_buf = kmalloc(conn->send_capacity);
            conn->recv_len = 0;
            conn->send_len = 0;
            conn->recv_wnd = 65535;
            conn->send_wnd = 65535;
            return conn;
        }
    }
    return NULL;
}

static void tcp_free_connection(struct tcp_connection *conn)
{
    if (conn->recv_buf) kfree(conn->recv_buf);
    if (conn->send_buf) kfree(conn->send_buf);
    conn->in_use = false;
}

/* Build and send a TCP packet */
static int tcp_send_packet(struct tcp_connection *conn, uint8_t flags, 
                           const void *data, size_t data_len)
{
    if (num_interfaces == 0) return -1;
    struct net_interface *iface = &interfaces[0];
    
    size_t tcp_len = sizeof(struct tcp_hdr) + data_len;
    size_t total_len = ETH_HLEN + sizeof(struct ip_hdr) + tcp_len;
    
    uint8_t *packet = kmalloc(total_len);
    if (!packet) return -1;
    
    struct eth_hdr *eth = (struct eth_hdr *)packet;
    struct ip_hdr *ip = (struct ip_hdr *)(packet + ETH_HLEN);
    struct tcp_hdr *tcp = (struct tcp_hdr *)(packet + ETH_HLEN + sizeof(struct ip_hdr));
    uint8_t *payload = (uint8_t *)tcp + sizeof(struct tcp_hdr);
    
    /* Ethernet header */
    eth->type = htons(ETH_P_IP);
    for (int i = 0; i < ETH_ALEN; i++) {
        eth->src[i] = iface->mac[i];
        eth->dest[i] = 0xFF; /* TODO: ARP lookup for real dest MAC */
    }
    
    /* IP header */
    ip->version_ihl = 0x45;
    ip->tos = 0;
    ip->total_len = htons(sizeof(struct ip_hdr) + tcp_len);
    ip->id = htons(conn->seq & 0xFFFF);
    ip->flags_frag = htons(0x4000); /* Don't fragment */
    ip->ttl = 64;
    ip->protocol = IP_PROTO_TCP;
    ip->src_ip = conn->local_ip;
    ip->dst_ip = conn->remote_ip;
    ip->checksum = 0;
    ip->checksum = checksum(ip, sizeof(struct ip_hdr));
    
    /* TCP header */
    tcp->src_port = htons(conn->local_port);
    tcp->dst_port = htons(conn->remote_port);
    tcp->seq = htonl(conn->seq);
    tcp->ack = htonl(conn->ack);
    tcp->data_offset = (sizeof(struct tcp_hdr) / 4) << 4;
    tcp->flags = flags;
    tcp->window = htons(conn->recv_wnd);
    tcp->urgent = 0;
    tcp->checksum = 0;
    
    /* Copy payload data */
    if (data && data_len > 0) {
        for (size_t i = 0; i < data_len; i++) {
            payload[i] = ((const uint8_t *)data)[i];
        }
    }
    
    /* Calculate TCP checksum */
    tcp->checksum = tcp_checksum(ip, tcp, tcp_len);
    
    /* Send via driver */
    if (iface->send) {
        iface->send(iface, packet, total_len);
        iface->tx_packets++;
        iface->tx_bytes += total_len;
    }
    
    kfree(packet);
    return 0;
}

/* Simple pseudo-random number generator for initial sequence numbers */
static uint32_t tcp_isn_counter = 0x12345678;
static uint32_t tcp_generate_isn(void)
{
    tcp_isn_counter = tcp_isn_counter * 1103515245 + 12345;
    return tcp_isn_counter;
}

int tcp_connect(uint32_t dest_ip, uint16_t dest_port)
{
    struct tcp_connection *conn = tcp_alloc_connection();
    if (!conn) return -1;
    
    if (num_interfaces == 0) {
        tcp_free_connection(conn);
        return -1;
    }
    
    struct net_interface *iface = &interfaces[0];
    
    conn->local_ip = iface->ip;
    conn->local_port = next_ephemeral_port++;
    if (next_ephemeral_port > 65000) next_ephemeral_port = 49152;
    
    conn->remote_ip = dest_ip;
    conn->remote_port = dest_port;
    conn->seq = tcp_generate_isn();
    conn->ack = 0;
    conn->state = TCP_SYN_SENT;
    
    /* Send SYN packet */
    printk(KERN_INFO "TCP: Connecting to %d.%d.%d.%d:%u (seq=%u)\n",
           (dest_ip >> 24) & 0xFF, (dest_ip >> 16) & 0xFF,
           (dest_ip >> 8) & 0xFF, dest_ip & 0xFF, dest_port, conn->seq);
    
    int ret = tcp_send_packet(conn, TCP_SYN, NULL, 0);
    if (ret < 0) {
        tcp_free_connection(conn);
        return -1;
    }
    
    conn->seq++; /* SYN consumes one sequence number */
    
    /* Return connection index for tracking */
    return (int)(conn - tcp_connections);
}

int tcp_send(struct tcp_connection *conn, const void *data, size_t len)
{
    if (conn->state != TCP_ESTABLISHED) return -1;
    
    if (conn->send_len + len > conn->send_capacity) {
        return -1;  /* Buffer full */
    }
    
    /* Copy to send buffer */
    for (size_t i = 0; i < len; i++) {
        conn->send_buf[conn->send_len++] = ((uint8_t *)data)[i];
    }
    
    /* Send data with PSH+ACK flags */
    int ret = tcp_send_packet(conn, TCP_PSH | TCP_ACK, data, len);
    if (ret == 0) {
        conn->seq += len;
    }
    
    return ret == 0 ? (int)len : -1;
}

int tcp_recv(struct tcp_connection *conn, void *data, size_t len)
{
    if (conn->recv_len == 0) return 0;
    
    size_t to_copy = (len < conn->recv_len) ? len : conn->recv_len;
    
    for (size_t i = 0; i < to_copy; i++) {
        ((uint8_t *)data)[i] = conn->recv_buf[i];
    }
    
    /* Shift remaining data */
    for (size_t i = to_copy; i < conn->recv_len; i++) {
        conn->recv_buf[i - to_copy] = conn->recv_buf[i];
    }
    conn->recv_len -= to_copy;
    
    return to_copy;
}

int tcp_close(struct tcp_connection *conn)
{
    if (!conn || !conn->in_use) return -1;
    
    switch (conn->state) {
        case TCP_ESTABLISHED:
            /* Active close - send FIN */
            conn->state = TCP_FIN_WAIT_1;
            tcp_send_packet(conn, TCP_FIN | TCP_ACK, NULL, 0);
            conn->seq++;  /* FIN consumes one sequence number */
            printk(KERN_DEBUG "TCP: Sent FIN, entering FIN_WAIT_1\n");
            break;
            
        case TCP_CLOSE_WAIT:
            /* Passive close - send FIN */
            conn->state = TCP_LAST_ACK;
            tcp_send_packet(conn, TCP_FIN | TCP_ACK, NULL, 0);
            conn->seq++;
            printk(KERN_DEBUG "TCP: Sent FIN, entering LAST_ACK\n");
            break;
            
        case TCP_SYN_SENT:
        case TCP_LISTEN:
            /* Just close immediately */
            tcp_free_connection(conn);
            return 0;
            
        default:
            /* Already closing */
            break;
    }
    
    /* Don't free immediately - wait for state machine to complete */
    return 0;
}

/* Find a connection by remote IP/port */
static struct tcp_connection *tcp_find_connection(uint32_t remote_ip, uint16_t remote_port,
                                                   uint32_t local_ip, uint16_t local_port)
{
    for (int i = 0; i < MAX_TCP_CONNECTIONS; i++) {
        struct tcp_connection *c = &tcp_connections[i];
        if (c->in_use &&
            c->remote_ip == remote_ip && c->remote_port == remote_port &&
            c->local_ip == local_ip && c->local_port == local_port) {
            return c;
        }
    }
    return NULL;
}

/* Handle incoming TCP segment - called from IP layer */
void tcp_handle_segment(uint32_t src_ip, uint32_t dst_ip,
                        struct tcp_hdr *tcp, size_t tcp_len)
{
    uint16_t src_port = ntohs(tcp->src_port);
    uint16_t dst_port = ntohs(tcp->dst_port);
    uint32_t seq = ntohl(tcp->seq);
    uint32_t ack = ntohl(tcp->ack);
    uint8_t flags = tcp->flags;
    
    struct tcp_connection *conn = tcp_find_connection(src_ip, src_port, dst_ip, dst_port);
    
    if (!conn) {
        /* No connection - send RST if not a RST */
        if (!(flags & TCP_RST)) {
            printk(KERN_DEBUG "TCP: No connection for port %u, would send RST\n", dst_port);
        }
        return;
    }
    
    size_t header_len = ((tcp->data_offset >> 4) & 0xF) * 4;
    size_t data_len = tcp_len - header_len;
    uint8_t *data = (uint8_t *)tcp + header_len;
    
    /* TCP State Machine */
    switch (conn->state) {
        case TCP_SYN_SENT:
            /* Expecting SYN+ACK */
            if ((flags & (TCP_SYN | TCP_ACK)) == (TCP_SYN | TCP_ACK)) {
                conn->ack = seq + 1;
                conn->state = TCP_ESTABLISHED;
                /* Send ACK */
                tcp_send_packet(conn, TCP_ACK, NULL, 0);
                printk(KERN_INFO "TCP: Connection established!\n");
            } else if (flags & TCP_RST) {
                printk(KERN_INFO "TCP: Connection refused (RST)\n");
                conn->state = TCP_CLOSED;
                tcp_free_connection(conn);
            }
            break;
            
        case TCP_ESTABLISHED:
            /* Handle incoming data */
            if (flags & TCP_FIN) {
                /* Remote is closing */
                conn->ack = seq + data_len + 1;
                conn->state = TCP_CLOSE_WAIT;
                tcp_send_packet(conn, TCP_ACK, NULL, 0);
                printk(KERN_DEBUG "TCP: Received FIN, entering CLOSE_WAIT\n");
            } else if (data_len > 0) {
                /* Received data */
                if (conn->recv_len + data_len <= conn->recv_capacity) {
                    for (size_t i = 0; i < data_len; i++) {
                        conn->recv_buf[conn->recv_len++] = data[i];
                    }
                    conn->ack = seq + data_len;
                    /* Send ACK */
                    tcp_send_packet(conn, TCP_ACK, NULL, 0);
                }
            } else if (flags & TCP_ACK) {
                /* ACK for our data */
                /* Update send window, remove acknowledged data from send buffer */
            }
            break;
            
        case TCP_FIN_WAIT_1:
            if (flags & TCP_ACK) {
                conn->state = TCP_FIN_WAIT_2;
                printk(KERN_DEBUG "TCP: Entering FIN_WAIT_2\n");
            }
            if (flags & TCP_FIN) {
                conn->ack = seq + 1;
                tcp_send_packet(conn, TCP_ACK, NULL, 0);
                if (conn->state == TCP_FIN_WAIT_2) {
                    conn->state = TCP_TIME_WAIT;
                    printk(KERN_DEBUG "TCP: Entering TIME_WAIT\n");
                } else {
                    conn->state = TCP_CLOSING;
                }
            }
            break;
            
        case TCP_FIN_WAIT_2:
            if (flags & TCP_FIN) {
                conn->ack = seq + 1;
                tcp_send_packet(conn, TCP_ACK, NULL, 0);
                conn->state = TCP_TIME_WAIT;
                printk(KERN_DEBUG "TCP: Entering TIME_WAIT\n");
            }
            break;
            
        case TCP_CLOSING:
            if (flags & TCP_ACK) {
                conn->state = TCP_TIME_WAIT;
            }
            break;
            
        case TCP_LAST_ACK:
            if (flags & TCP_ACK) {
                conn->state = TCP_CLOSED;
                tcp_free_connection(conn);
                printk(KERN_DEBUG "TCP: Connection closed\n");
            }
            break;
            
        case TCP_TIME_WAIT:
            /* Should wait 2*MSL then free - for now just free */
            tcp_free_connection(conn);
            break;
            
        default:
            break;
    }
}

/* ===================================================================== */
/* UDP Functions */
/* ===================================================================== */

int udp_send(uint32_t dest_ip, uint16_t src_port, uint16_t dest_port,
             const void *data, size_t len)
{
    if (num_interfaces == 0) return -1;
    
    size_t total_len = ETH_HLEN + sizeof(struct ip_hdr) + 
                       sizeof(struct udp_hdr) + len;
    uint8_t *packet = kmalloc(total_len);
    if (!packet) return -1;
    
    struct eth_hdr *eth = (struct eth_hdr *)packet;
    struct ip_hdr *ip = (struct ip_hdr *)(packet + ETH_HLEN);
    struct udp_hdr *udp = (struct udp_hdr *)(packet + ETH_HLEN + sizeof(struct ip_hdr));
    uint8_t *payload = packet + ETH_HLEN + sizeof(struct ip_hdr) + sizeof(struct udp_hdr);
    
    struct net_interface *iface = &interfaces[0];
    
    /* Ethernet */
    eth->type = htons(ETH_P_IP);
    for (int i = 0; i < ETH_ALEN; i++) {
        eth->src[i] = iface->mac[i];
    }
    
    /* IP */
    ip->version_ihl = 0x45;
    ip->tos = 0;
    ip->total_len = htons(sizeof(struct ip_hdr) + sizeof(struct udp_hdr) + len);
    ip->id = htons(1);
    ip->flags_frag = 0;
    ip->ttl = 64;
    ip->protocol = IP_PROTO_UDP;
    ip->src_ip = iface->ip;
    ip->dst_ip = dest_ip;
    ip->checksum = 0;
    ip->checksum = checksum(ip, sizeof(struct ip_hdr));
    
    /* UDP */
    udp->src_port = htons(src_port);
    udp->dst_port = htons(dest_port);
    udp->length = htons(sizeof(struct udp_hdr) + len);
    udp->checksum = 0;  /* Optional in UDP */
    
    /* Copy payload */
    for (size_t i = 0; i < len; i++) {
        payload[i] = ((uint8_t *)data)[i];
    }
    
    /* Send via driver */
    if (iface->send) {
        iface->send(iface, packet, total_len);
        iface->tx_packets++;
        iface->tx_bytes += total_len;
    }
    printk(KERN_DEBUG "UDP: Sent %zu bytes to port %u\n", len, dest_port);
    
    kfree(packet);
    return len;
}

/* ===================================================================== */
/* Network Initialization */
/* ===================================================================== */

void tcpip_init(void)
{
    printk(KERN_INFO "NET: Initializing network stack\n");
    
    /* Clear ARP cache */
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        arp_cache[i].valid = false;
    }
    
    /* Clear TCP connections */
    for (int i = 0; i < MAX_TCP_CONNECTIONS; i++) {
        tcp_connections[i].in_use = false;
    }
    
    /* Create loopback interface */
    struct net_interface *lo = &interfaces[num_interfaces++];
    lo->name[0] = 'l'; lo->name[1] = 'o'; lo->name[2] = '\0';
    lo->ip = 0x7F000001;  /* 127.0.0.1 */
    lo->netmask = 0xFF000000;
    lo->gateway = 0;
    lo->up = true;
    
    printk(KERN_INFO "NET: Loopback interface configured\n");
    printk(KERN_INFO "NET: TCP/IP stack initialized\n");
}

/* Interface configuration */
struct net_interface *net_add_interface(const char *name, uint8_t *mac, uint32_t ip, 
                      uint32_t netmask, uint32_t gateway)
{
    if (num_interfaces >= MAX_INTERFACES) return NULL;
    
    struct net_interface *iface = &interfaces[num_interfaces++];
    
    for (int i = 0; i < 15 && name[i]; i++) {
        iface->name[i] = name[i];
    }
    
    for (int i = 0; i < ETH_ALEN; i++) {
        iface->mac[i] = mac[i];
    }
    
    iface->ip = ip;
    iface->netmask = netmask;
    iface->gateway = gateway;
    iface->up = true;
    iface->send = NULL; /* Default */
    
    printk(KERN_INFO "NET: Added interface %s\n", name);
    
    return iface;
}
