#ifndef ROBO_ED25519_H
#define ROBO_ED25519_H

#include <stdint.h>
#include <stddef.h>

int robo_ed25519_keypair(uint8_t pk[32], uint8_t sk[64], const uint8_t seed[32]);
int robo_ed25519_sign(uint8_t sig[64], const uint8_t *msg, size_t len,
                      const uint8_t sk[64]);
int robo_ed25519_verify(const uint8_t sig[64], const uint8_t *msg, size_t len,
                        const uint8_t pk[32]);
int robo_ed25519_selftest(void);

#endif
