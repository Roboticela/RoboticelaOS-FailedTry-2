#ifndef ROBO_SHA512_H
#define ROBO_SHA512_H

#include <stdint.h>
#include <stddef.h>

void robo_sha512(const void *data, size_t len, uint8_t out[64]);
int robo_sha512_selftest(void);

#endif
