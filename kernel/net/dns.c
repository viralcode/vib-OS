/*
 * vib-OS Kernel - DNS Resolver
 * 
 * Simple DNS client for name resolution.
 */

#include "types.h"
#include "printk.h"
#include "mm/kmalloc.h"

/* ===================================================================== */
/* DNS Constants */
/* ===================================================================== */

#define DNS_PORT            53
#define DNS_MAX_NAME_LEN    255
#define DNS_HEADER_SIZE     12

#define DNS_TYPE_A          1       /* IPv4 address */
#define DNS_TYPE_AAAA       28      /* IPv6 address */
#define DNS_TYPE_CNAME      5       /* Canonical name */
#define DNS_TYPE_MX         15      /* Mail exchange */
#define DNS_TYPE_TXT        16      /* Text record */

#define DNS_CLASS_IN        1       /* Internet */

#define DNS_FLAG_QR         0x8000  /* Query/Response */
#define DNS_FLAG_AA         0x0400  /* Authoritative */
#define DNS_FLAG_TC         0x0200  /* Truncated */
#define DNS_FLAG_RD         0x0100  /* Recursion Desired */
#define DNS_FLAG_RA         0x0080  /* Recursion Available */
#define DNS_RCODE_MASK      0x000F

/* ===================================================================== */
/* DNS Structures */
/* ===================================================================== */

struct dns_header {
    uint16_t id;
    uint16_t flags;
    uint16_t qdcount;   /* Question count */
    uint16_t ancount;   /* Answer count */
    uint16_t nscount;   /* Authority count */
    uint16_t arcount;   /* Additional count */
} __attribute__((packed));

struct dns_question {
    /* Name comes before this (variable length) */
    uint16_t qtype;
    uint16_t qclass;
} __attribute__((packed));

struct dns_answer {
    /* Name comes before this (variable length or pointer) */
    uint16_t type;
    uint16_t class;
    uint32_t ttl;
    uint16_t rdlength;
    /* rdata follows */
} __attribute__((packed));

/* ===================================================================== */
/* DNS Cache */
/* ===================================================================== */

#define DNS_CACHE_SIZE  64

struct dns_cache_entry {
    char name[DNS_MAX_NAME_LEN];
    uint32_t ip;
    uint32_t ttl;
    uint64_t timestamp;
    bool valid;
};

static struct dns_cache_entry dns_cache[DNS_CACHE_SIZE];
static uint32_t dns_servers[4] = { 0x08080808, 0x08080404, 0, 0 }; /* Google DNS */
static int num_dns_servers = 2;
static uint16_t dns_query_id = 1;

/* ===================================================================== */
/* Helper Functions */
/* ===================================================================== */

static inline uint16_t htons(uint16_t x) {
    return ((x & 0xFF) << 8) | ((x >> 8) & 0xFF);
}

static inline uint16_t ntohs(uint16_t x) {
    return htons(x);
}

/* Encode domain name to DNS format (labels) */
static int dns_encode_name(const char *name, uint8_t *buf, size_t buf_len)
{
    size_t pos = 0;
    size_t len = 0;
    size_t label_start = 0;
    
    while (name[len]) {
        if (name[len] == '.') {
            if (pos + 1 + (len - label_start) >= buf_len) return -1;
            buf[pos++] = len - label_start;
            for (size_t i = label_start; i < len; i++) {
                buf[pos++] = name[i];
            }
            label_start = len + 1;
        }
        len++;
    }
    
    /* Last label */
    if (len > label_start) {
        if (pos + 1 + (len - label_start) >= buf_len) return -1;
        buf[pos++] = len - label_start;
        for (size_t i = label_start; i < len; i++) {
            buf[pos++] = name[i];
        }
    }
    
    /* Null terminator */
    buf[pos++] = 0;
    
    return pos;
}

/* Decode DNS name from response */
static int dns_decode_name(const uint8_t *response, size_t response_len,
                            size_t offset, char *name, size_t name_len)
{
    size_t pos = 0;
    size_t read_len = 0;
    bool jumped = false;
    
    while (offset < response_len) {
        uint8_t len = response[offset];
        
        if (len == 0) {
            if (!jumped) read_len++;
            break;
        }
        
        /* Compression pointer */
        if ((len & 0xC0) == 0xC0) {
            if (offset + 1 >= response_len) return -1;
            uint16_t ptr = ((len & 0x3F) << 8) | response[offset + 1];
            if (!jumped) read_len += 2;
            jumped = true;
            offset = ptr;
            continue;
        }
        
        if (!jumped) read_len += 1 + len;
        offset++;
        
        if (pos > 0 && pos < name_len - 1) {
            name[pos++] = '.';
        }
        
        for (int i = 0; i < len && offset < response_len; i++, offset++) {
            if (pos < name_len - 1) {
                name[pos++] = response[offset];
            }
        }
    }
    
    name[pos] = '\0';
    return jumped ? read_len : read_len;
}

/* ===================================================================== */
/* DNS Functions */
/* ===================================================================== */

/* Build DNS query packet */
static int dns_build_query(const char *name, uint16_t type, uint8_t *buf, size_t buf_len)
{
    if (buf_len < DNS_HEADER_SIZE + DNS_MAX_NAME_LEN + 4) return -1;
    
    struct dns_header *hdr = (struct dns_header *)buf;
    hdr->id = htons(dns_query_id++);
    hdr->flags = htons(DNS_FLAG_RD);  /* Recursion desired */
    hdr->qdcount = htons(1);
    hdr->ancount = 0;
    hdr->nscount = 0;
    hdr->arcount = 0;
    
    /* Encode question name */
    int name_len = dns_encode_name(name, buf + DNS_HEADER_SIZE, buf_len - DNS_HEADER_SIZE);
    if (name_len < 0) return -1;
    
    /* Question type and class */
    size_t pos = DNS_HEADER_SIZE + name_len;
    buf[pos++] = (type >> 8) & 0xFF;
    buf[pos++] = type & 0xFF;
    buf[pos++] = 0;
    buf[pos++] = 1;  /* IN class */
    
    return pos;
}

/* Parse DNS response */
static int dns_parse_response(const uint8_t *response, size_t len, uint32_t *ip_out)
{
    if (len < DNS_HEADER_SIZE) return -1;
    
    struct dns_header *hdr = (struct dns_header *)response;
    
    /* Check response code */
    uint16_t flags = ntohs(hdr->flags);
    if (!(flags & DNS_FLAG_QR)) return -1;  /* Not a response */
    if ((flags & DNS_RCODE_MASK) != 0) return -1;  /* Error */
    
    uint16_t qdcount = ntohs(hdr->qdcount);
    uint16_t ancount = ntohs(hdr->ancount);
    
    if (ancount == 0) return -1;  /* No answers */
    
    /* Skip questions */
    size_t pos = DNS_HEADER_SIZE;
    for (int i = 0; i < qdcount; i++) {
        while (pos < len && response[pos] != 0) {
            if ((response[pos] & 0xC0) == 0xC0) {
                pos += 2;
                break;
            }
            pos += 1 + response[pos];
        }
        if (response[pos] == 0) pos++;
        pos += 4;  /* qtype + qclass */
    }
    
    /* Parse answers */
    for (int i = 0; i < ancount && pos < len; i++) {
        /* Skip name */
        while (pos < len && response[pos] != 0) {
            if ((response[pos] & 0xC0) == 0xC0) {
                pos += 2;
                goto got_name;
            }
            pos += 1 + response[pos];
        }
        if (response[pos] == 0) pos++;
    got_name:
        
        if (pos + 10 > len) break;
        
        uint16_t type = (response[pos] << 8) | response[pos + 1];
        pos += 2;
        pos += 2;  /* class */
        pos += 4;  /* ttl */
        uint16_t rdlength = (response[pos] << 8) | response[pos + 1];
        pos += 2;
        
        if (type == DNS_TYPE_A && rdlength == 4) {
            *ip_out = (response[pos] << 24) | (response[pos + 1] << 16) |
                      (response[pos + 2] << 8) | response[pos + 3];
            return 0;
        }
        
        pos += rdlength;
    }
    
    return -1;
}

/* Cache lookup */
static struct dns_cache_entry *dns_cache_lookup(const char *name)
{
    for (int i = 0; i < DNS_CACHE_SIZE; i++) {
        if (dns_cache[i].valid) {
            /* Compare names */
            bool match = true;
            for (int j = 0; name[j] || dns_cache[i].name[j]; j++) {
                if (name[j] != dns_cache[i].name[j]) {
                    match = false;
                    break;
                }
            }
            if (match) return &dns_cache[i];
        }
    }
    return NULL;
}

/* Add to cache */
static void dns_cache_add(const char *name, uint32_t ip, uint32_t ttl)
{
    int slot = -1;
    
    /* Find empty or oldest slot */
    for (int i = 0; i < DNS_CACHE_SIZE; i++) {
        if (!dns_cache[i].valid) {
            slot = i;
            break;
        }
    }
    
    if (slot < 0) slot = 0;  /* Evict first */
    
    dns_cache[slot].valid = true;
    dns_cache[slot].ip = ip;
    dns_cache[slot].ttl = ttl;
    dns_cache[slot].timestamp = 0;  /* TODO: get_time() */
    
    for (int i = 0; i < DNS_MAX_NAME_LEN - 1 && name[i]; i++) {
        dns_cache[slot].name[i] = name[i];
        dns_cache[slot].name[i + 1] = '\0';
    }
}

/* ===================================================================== */
/* Public API */
/* ===================================================================== */

int dns_resolve(const char *hostname, uint32_t *ip_out)
{
    printk(KERN_DEBUG "DNS: Resolving %s\n", hostname);
    
    /* Check cache first */
    struct dns_cache_entry *cached = dns_cache_lookup(hostname);
    if (cached) {
        *ip_out = cached->ip;
        printk(KERN_DEBUG "DNS: Found in cache\n");
        return 0;
    }
    
    /* Build query */
    uint8_t query[512];
    int query_len = dns_build_query(hostname, DNS_TYPE_A, query, sizeof(query));
    if (query_len < 0) {
        printk(KERN_ERR "DNS: Failed to build query\n");
        return -1;
    }
    
    /* TODO: Send UDP query to DNS server */
    /* TODO: Wait for response */
    /* TODO: Parse response */
    
    /* For now, return localhost for testing */
    printk(KERN_DEBUG "DNS: Query built (%d bytes), would send to server\n", query_len);
    
    return -1;  /* Not implemented yet */
}

int dns_set_server(int index, uint32_t ip)
{
    if (index < 0 || index >= 4) return -1;
    dns_servers[index] = ip;
    if (index >= num_dns_servers) {
        num_dns_servers = index + 1;
    }
    return 0;
}

void dns_init(void)
{
    printk(KERN_INFO "DNS: Initializing resolver\n");
    
    for (int i = 0; i < DNS_CACHE_SIZE; i++) {
        dns_cache[i].valid = false;
    }
    
    printk(KERN_INFO "DNS: Using servers 8.8.8.8, 8.8.4.4\n");
}
