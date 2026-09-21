#ifndef ROBO_NET_CHECKSUM_H
#define ROBO_NET_CHECKSUM_H

#include <stdint.h>

uint16_t Checksum(const void *data, unsigned len);
uint16_t ChecksumFinish(uint32_t sum);
uint32_t ChecksumAdd(uint32_t sum, const void *data, unsigned len);
uint16_t ChecksumPseudo(uint32_t src, uint32_t dst, uint8_t proto,
                              const void *data, unsigned len);

#endif
