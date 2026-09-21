#ifndef ROBO_NET_UTIL_H
#define ROBO_NET_UTIL_H

#include <stdint.h>

#define RoboNetMaxIf        4u
#define RoboNetMaxSock      32u
#define RoboNetMaxTcp       16u
#define RoboNetMaxUdp       16u
#define RoboNetMaxArp       32u
#define RoboNetMaxRoute     8u
#define RoboNetMaxPkt       32u
#define RoboNetMtu           1500u
#define RoboNetEthHdr       14u
#define RoboNetEthMin       60u
#define RoboNetEthMax       1514u
#define RoboNetPktCap       2048u
#define RoboNetHeadroom      64u
#define RoboNetUdpQ         8u
#define RoboNetTcpBuf       8192u
#define RoboNetAcceptQ      4u
#define RoboNetEphemeralMin 49152u
#define RoboNetEphemeralMax 65535u
#define RoboNetNameMax      16u

#define RoboEthIp  0x0800u
#define RoboEthArp 0x0806u

#define RoboIpprotoIcmp 1u
#define RoboIpprotoTcp  6u
#define RoboIpprotoUdp  17u

static inline uint16_t Bswap16(uint16_t x)
{
    return (uint16_t)((x << 8) | (x >> 8));
}

static inline uint32_t Bswap32(uint32_t x)
{
    return (x << 24) | ((x << 8) & 0x00FF0000u) |
           ((x >> 8) & 0x0000FF00u) | (x >> 24);
}

static inline uint16_t Htons(uint16_t x) { return Bswap16(x); }
static inline uint16_t Ntohs(uint16_t x) { return Bswap16(x); }
static inline uint32_t Htonl(uint32_t x) { return Bswap32(x); }
static inline uint32_t Ntohl(uint32_t x) { return Bswap32(x); }

static inline uint16_t Rd16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static inline uint32_t Rd32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

static inline void Wr16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

static inline void Wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

void NetMemcpy(void *dst, const void *src, unsigned n);
void NetMemset(void *dst, uint8_t v, unsigned n);
int NetMemcmp(const void *a, const void *b, unsigned n);
unsigned NetStrlen(const char *s);
void MacFmt(const uint8_t mac[6], char out[18]);
void Ipv4Fmt(uint32_t ip, char out[16]);

#endif
