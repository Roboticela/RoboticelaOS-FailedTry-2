#ifndef ROBO_RDRV_H
#define ROBO_RDRV_H

#include <stdint.h>
#include <stddef.h>

#define RoboRdrvVersion     1u
#define RoboArchX8664      1u
#define RoboArchArm64       2u

#define RoboRdrvFlagTest   1u

#define RoboRdrvKeyIdLen  16
#define RoboRdrvHashLen    32
#define RoboRdrvSigLen     64
#define RoboRdrvMaxKeys    4

typedef struct RoboRdrvHeader {
    uint8_t magic[4];
    uint16_t version;
    uint16_t headerSize;
    uint32_t flags;
    uint32_t abiMajor;
    uint32_t abiMinor;
    uint32_t arch;
    uint32_t pkgVersion;
    uint32_t payloadOff;
    uint32_t payloadLen;
    uint8_t keyId[RoboRdrvKeyIdLen];
    uint8_t hash[RoboRdrvHashLen];
    uint8_t sig[RoboRdrvSigLen];
} RoboRdrvHeader;

typedef struct RoboRdrvInfo {
    uint32_t flags;
    uint32_t abiMajor;
    uint32_t abiMinor;
    uint32_t arch;
    uint32_t pkgVersion;
    uint32_t payloadOff;
    uint32_t payloadLen;
    uint8_t keyId[RoboRdrvKeyIdLen];
    uint8_t hash[RoboRdrvHashLen];
    int signedOk;
    int testPackage;
} RoboRdrvInfo;

int RdrvTrustAdd(const uint8_t keyId[16], const uint8_t pk[32]);
void RdrvTrustClear(void);
int RdrvBuild(void *out, size_t cap, const void *payload, uint32_t plen,
                    uint32_t flags, uint32_t abiMaj, uint32_t abiMin,
                    uint32_t arch, uint32_t ver, const uint8_t keyId[16],
                    const uint8_t sk[64], int sign);
int RdrvVerify(const void *pkg, size_t len, RoboRdrvInfo *out,
                     int testMode);
int RdrvSelftest(void);

#endif
