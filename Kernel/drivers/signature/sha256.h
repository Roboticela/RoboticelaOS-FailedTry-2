#ifndef ROBO_SHA256_H
#define ROBO_SHA256_H

#include <stdint.h>
#include <stddef.h>

void RoboSha256(const void *data, size_t len, uint8_t out[32]);
int Sha256Selftest(void);

#endif
