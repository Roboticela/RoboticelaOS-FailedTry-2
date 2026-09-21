#include "net/core/checksum.h"

uint32_t ChecksumAdd(uint32_t sum, const void *data, unsigned len)
{
    const uint8_t *p = (const uint8_t *)data;
    while (len > 1u) {
        sum += ((uint32_t)p[0] << 8) | p[1];
        p += 2;
        len -= 2u;
    }
    if (len)
        sum += (uint32_t)p[0] << 8;
    return sum;
}

uint16_t ChecksumFinish(uint32_t sum)
{
    while (sum >> 16)
        sum = (sum & 0xFFFFu) + (sum >> 16);
    return (uint16_t)~sum;
}

uint16_t Checksum(const void *data, unsigned len)
{
    return ChecksumFinish(ChecksumAdd(0, data, len));
}

uint16_t ChecksumPseudo(uint32_t src, uint32_t dst, uint8_t proto,
                              const void *data, unsigned len)
{
    uint8_t ph[12];
    uint32_t sum;
    ph[0] = (uint8_t)(src >> 24);
    ph[1] = (uint8_t)(src >> 16);
    ph[2] = (uint8_t)(src >> 8);
    ph[3] = (uint8_t)src;
    ph[4] = (uint8_t)(dst >> 24);
    ph[5] = (uint8_t)(dst >> 16);
    ph[6] = (uint8_t)(dst >> 8);
    ph[7] = (uint8_t)dst;
    ph[8] = 0;
    ph[9] = proto;
    ph[10] = (uint8_t)(len >> 8);
    ph[11] = (uint8_t)len;
    sum = ChecksumAdd(0, ph, 12);
    sum = ChecksumAdd(sum, data, len);
    return ChecksumFinish(sum);
}
