#ifndef ROBO_NET_CHECKSUM_H
#define ROBO_NET_CHECKSUM_H

#include <stdint.h>

uint16_t robo_checksum(const void *data, unsigned len);
uint16_t robo_checksum_finish(uint32_t sum);
uint32_t robo_checksum_add(uint32_t sum, const void *data, unsigned len);
uint16_t robo_checksum_pseudo(uint32_t src, uint32_t dst, uint8_t proto,
                              const void *data, unsigned len);

#endif
