#ifndef ROBO_RDRV_H
#define ROBO_RDRV_H

#include <stdint.h>
#include <stddef.h>

#define ROBO_RDRV_VERSION     1u
#define ROBO_ARCH_X86_64      1u
#define ROBO_ARCH_ARM64       2u

#define ROBO_RDRV_FLAG_TEST   1u

#define ROBO_RDRV_KEY_ID_LEN  16
#define ROBO_RDRV_HASH_LEN    32
#define ROBO_RDRV_SIG_LEN     64
#define ROBO_RDRV_MAX_KEYS    4

typedef struct RoboRdrvHeader {
    uint8_t magic[4];
    uint16_t version;
    uint16_t header_size;
    uint32_t flags;
    uint32_t abi_major;
    uint32_t abi_minor;
    uint32_t arch;
    uint32_t pkg_version;
    uint32_t payload_off;
    uint32_t payload_len;
    uint8_t key_id[ROBO_RDRV_KEY_ID_LEN];
    uint8_t hash[ROBO_RDRV_HASH_LEN];
    uint8_t sig[ROBO_RDRV_SIG_LEN];
} RoboRdrvHeader;

typedef struct RoboRdrvInfo {
    uint32_t flags;
    uint32_t abi_major;
    uint32_t abi_minor;
    uint32_t arch;
    uint32_t pkg_version;
    uint32_t payload_off;
    uint32_t payload_len;
    uint8_t key_id[ROBO_RDRV_KEY_ID_LEN];
    uint8_t hash[ROBO_RDRV_HASH_LEN];
    int signed_ok;
    int test_package;
} RoboRdrvInfo;

int robo_rdrv_trust_add(const uint8_t key_id[16], const uint8_t pk[32]);
void robo_rdrv_trust_clear(void);
int robo_rdrv_build(void *out, size_t cap, const void *payload, uint32_t plen,
                    uint32_t flags, uint32_t abi_maj, uint32_t abi_min,
                    uint32_t arch, uint32_t ver, const uint8_t key_id[16],
                    const uint8_t sk[64], int sign);
int robo_rdrv_verify(const void *pkg, size_t len, RoboRdrvInfo *out,
                     int test_mode);
int robo_rdrv_selftest(void);

#endif
