#ifndef ROBO_SHA256_H
#define ROBO_SHA256_H

#include <stdint.h>
#include <stddef.h>

void robo_sha256(const void *data, size_t len, uint8_t out[32]);
int robo_sha256_selftest(void);

#endif
