#include "net/core/net_util.h"

void robo_net_memcpy(void *dst, const void *src, unsigned n)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    while (n--)
        *d++ = *s++;
}

void robo_net_memset(void *dst, uint8_t v, unsigned n)
{
    uint8_t *d = (uint8_t *)dst;
    while (n--)
        *d++ = v;
}

int robo_net_memcmp(const void *a, const void *b, unsigned n)
{
    const uint8_t *x = (const uint8_t *)a;
    const uint8_t *y = (const uint8_t *)b;
    while (n--) {
        if (*x != *y)
            return (int)*x - (int)*y;
        x++;
        y++;
    }
    return 0;
}

unsigned robo_net_strlen(const char *s)
{
    unsigned n = 0;
    if (!s)
        return 0;
    while (s[n])
        n++;
    return n;
}

void robo_mac_fmt(const uint8_t mac[6], char out[18])
{
    static const char hex[] = "0123456789ABCDEF";
    unsigned i, j = 0;
    for (i = 0; i < 6; i++) {
        out[j++] = hex[mac[i] >> 4];
        out[j++] = hex[mac[i] & 15];
        if (i < 5)
            out[j++] = ':';
    }
    out[j] = 0;
}

void robo_ipv4_fmt(uint32_t ip, char out[16])
{
    unsigned i, n = 0;
    uint8_t oct[4];
    oct[0] = (uint8_t)(ip >> 24);
    oct[1] = (uint8_t)(ip >> 16);
    oct[2] = (uint8_t)(ip >> 8);
    oct[3] = (uint8_t)ip;
    for (i = 0; i < 4; i++) {
        unsigned v = oct[i];
        char tmp[3];
        unsigned tn = 0;
        if (i)
            out[n++] = '.';
        if (!v)
            tmp[tn++] = '0';
        else {
            while (v && tn < 3) {
                tmp[tn++] = (char)('0' + (v % 10u));
                v /= 10u;
            }
        }
        while (tn)
            out[n++] = tmp[--tn];
    }
    out[n] = 0;
}
