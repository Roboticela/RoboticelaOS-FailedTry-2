#ifndef ROBO_NET_UTIL_H
#define ROBO_NET_UTIL_H

#include <stdint.h>

#define ROBO_NET_MAX_IF        4u
#define ROBO_NET_MAX_SOCK      32u
#define ROBO_NET_MAX_TCP       16u
#define ROBO_NET_MAX_UDP       16u
#define ROBO_NET_MAX_ARP       32u
#define ROBO_NET_MAX_ROUTE     8u
#define ROBO_NET_MAX_PKT       32u
#define ROBO_NET_MTU           1500u
#define ROBO_NET_ETH_HDR       14u
#define ROBO_NET_ETH_MIN       60u
#define ROBO_NET_ETH_MAX       1514u
#define ROBO_NET_PKT_CAP       2048u
#define ROBO_NET_HEADROOM      64u
#define ROBO_NET_UDP_Q         8u
#define ROBO_NET_TCP_BUF       8192u
#define ROBO_NET_ACCEPT_Q      4u
#define ROBO_NET_EPHEMERAL_MIN 49152u
#define ROBO_NET_EPHEMERAL_MAX 65535u
#define ROBO_NET_NAME_MAX      16u

#define ROBO_ETH_IP  0x0800u
#define ROBO_ETH_ARP 0x0806u

#define ROBO_IPPROTO_ICMP 1u
#define ROBO_IPPROTO_TCP  6u
#define ROBO_IPPROTO_UDP  17u

static inline uint16_t robo_bswap16(uint16_t x)
{
    return (uint16_t)((x << 8) | (x >> 8));
}

static inline uint32_t robo_bswap32(uint32_t x)
{
    return (x << 24) | ((x << 8) & 0x00FF0000u) |
           ((x >> 8) & 0x0000FF00u) | (x >> 24);
}

static inline uint16_t robo_htons(uint16_t x) { return robo_bswap16(x); }
static inline uint16_t robo_ntohs(uint16_t x) { return robo_bswap16(x); }
static inline uint32_t robo_htonl(uint32_t x) { return robo_bswap32(x); }
static inline uint32_t robo_ntohl(uint32_t x) { return robo_bswap32(x); }

static inline uint16_t robo_rd16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static inline uint32_t robo_rd32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

static inline void robo_wr16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

static inline void robo_wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

void robo_net_memcpy(void *dst, const void *src, unsigned n);
void robo_net_memset(void *dst, uint8_t v, unsigned n);
int robo_net_memcmp(const void *a, const void *b, unsigned n);
unsigned robo_net_strlen(const char *s);
void robo_mac_fmt(const uint8_t mac[6], char out[18]);
void robo_ipv4_fmt(uint32_t ip, char out[16]);

#endif
