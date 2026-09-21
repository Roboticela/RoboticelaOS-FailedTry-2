#ifndef ROBO_ED25519_H
#define ROBO_ED25519_H

#include <stdint.h>
#include <stddef.h>

int Ed25519Keypair(uint8_t pk[32], uint8_t sk[64], const uint8_t seed[32]);
int Ed25519Sign(uint8_t sig[64], const uint8_t *msg, size_t len,
                      const uint8_t sk[64]);
int Ed25519Verify(const uint8_t sig[64], const uint8_t *msg, size_t len,
                        const uint8_t pk[32]);
int Ed25519Selftest(void);

#endif
