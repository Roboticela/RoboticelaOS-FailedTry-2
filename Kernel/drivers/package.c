#include "drivers/package.h"
#include "drivers/driver.h"
#include "drivers/signature/sha256.h"
#include "drivers/signature/ed25519.h"
#include "syscall/syscall_errors.h"

#ifdef DRIVER_HOST_TEST
#include <stdio.h>
#define kprintf printf
#else
#include "kio.h"
#endif

static const uint8_t MAGIC[4] = { 'R', 'D', 'R', 'V' };

typedef struct TrustKey {
    uint8_t id[16];
    uint8_t pk[32];
    int used;
} TrustKey;

static TrustKey gKeys[RoboRdrvMaxKeys];

static int same16(const uint8_t *a, const uint8_t *b)
{
    unsigned i;
    uint8_t x = 0;
    for (i = 0; i < 16; i++)
        x |= (uint8_t)(a[i] ^ b[i]);
    return x == 0;
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static void wr16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static void CopyN(uint8_t *d, const uint8_t *s, unsigned n)
{
    unsigned i;
    for (i = 0; i < n; i++)
        d[i] = s[i];
}

static void ZeroN(uint8_t *d, unsigned n)
{
    unsigned i;
    for (i = 0; i < n; i++)
        d[i] = 0;
}

void RdrvTrustClear(void)
{
    unsigned i;
    for (i = 0; i < RoboRdrvMaxKeys; i++)
        gKeys[i].used = 0;
}

int RdrvTrustAdd(const uint8_t keyId[16], const uint8_t pk[32])
{
    unsigned i;
    if (!keyId || !pk)
        return -RoboEinval;
    for (i = 0; i < RoboRdrvMaxKeys; i++) {
        if (!gKeys[i].used) {
            CopyN(gKeys[i].id, keyId, 16);
            CopyN(gKeys[i].pk, pk, 32);
            gKeys[i].used = 1;
            return 0;
        }
    }
    return -RoboEnomem;
}

static const uint8_t *FindKey(const uint8_t id[16])
{
    unsigned i;
    for (i = 0; i < RoboRdrvMaxKeys; i++) {
        if (gKeys[i].used && same16(gKeys[i].id, id))
            return gKeys[i].pk;
    }
    return 0;
}

static int HdrParse(const uint8_t *p, size_t len, RoboRdrvHeader *h)
{
    unsigned i;
    if (len < sizeof(RoboRdrvHeader))
        return -RoboEinval;
    for (i = 0; i < 4; i++)
        h->magic[i] = p[i];
    h->version = rd16(p + 4);
    h->headerSize = rd16(p + 6);
    h->flags = rd32(p + 8);
    h->abiMajor = rd32(p + 12);
    h->abiMinor = rd32(p + 16);
    h->arch = rd32(p + 20);
    h->pkgVersion = rd32(p + 24);
    h->payloadOff = rd32(p + 28);
    h->payloadLen = rd32(p + 32);
    CopyN(h->keyId, p + 36, 16);
    CopyN(h->hash, p + 52, 32);
    CopyN(h->sig, p + 84, 64);
    return 0;
}

static void HdrWrite(uint8_t *p, const RoboRdrvHeader *h)
{
    unsigned i;
    for (i = 0; i < 4; i++)
        p[i] = h->magic[i];
    wr16(p + 4, h->version);
    wr16(p + 6, h->headerSize);
    wr32(p + 8, h->flags);
    wr32(p + 12, h->abiMajor);
    wr32(p + 16, h->abiMinor);
    wr32(p + 20, h->arch);
    wr32(p + 24, h->pkgVersion);
    wr32(p + 28, h->payloadOff);
    wr32(p + 32, h->payloadLen);
    CopyN(p + 36, h->keyId, 16);
    CopyN(p + 52, h->hash, 32);
    CopyN(p + 84, h->sig, 64);
}

static int HashPkg(const uint8_t *pkg, size_t len, const RoboRdrvHeader *h,
                    uint8_t out[32])
{
    uint8_t tmp[sizeof(RoboRdrvHeader)];
    uint8_t acc[32];
    unsigned i;
    const uint8_t *pay;

    if (h->headerSize != sizeof(RoboRdrvHeader))
        return -RoboEinval;
    if (h->payloadOff < h->headerSize)
        return -RoboEinval;
    if (h->payloadOff > len || h->payloadLen > len - h->payloadOff)
        return -RoboEinval;
    CopyN(tmp, pkg, sizeof(RoboRdrvHeader));
    ZeroN(tmp + 52, 32);
    ZeroN(tmp + 84, 64);
    RoboSha256(tmp, sizeof(RoboRdrvHeader), acc);
    pay = pkg + h->payloadOff;
    if (h->payloadLen) {
        uint8_t mix[64];
        uint8_t ph[32];
        RoboSha256(pay, h->payloadLen, ph);
        for (i = 0; i < 32; i++) {
            mix[i] = acc[i];
            mix[32 + i] = ph[i];
        }
        RoboSha256(mix, 64, out);
    } else {
        for (i = 0; i < 32; i++)
            out[i] = acc[i];
    }
    (void)len;
    return 0;
}

int RdrvBuild(void *out, size_t cap, const void *payload, uint32_t plen,
                    uint32_t flags, uint32_t abiMaj, uint32_t abiMin,
                    uint32_t arch, uint32_t ver, const uint8_t keyId[16],
                    const uint8_t sk[64], int sign)
{
    RoboRdrvHeader h;
    uint8_t *p = (uint8_t *)out;
    uint32_t total = (uint32_t)sizeof(RoboRdrvHeader) + plen;
    unsigned i;

    if (!out || cap < total)
        return -RoboEnomem;
    if (plen && !payload)
        return -RoboEinval;
    for (i = 0; i < 4; i++)
        h.magic[i] = MAGIC[i];
    h.version = RoboRdrvVersion;
    h.headerSize = (uint16_t)sizeof(RoboRdrvHeader);
    h.flags = flags;
    h.abiMajor = abiMaj;
    h.abiMinor = abiMin;
    h.arch = arch;
    h.pkgVersion = ver;
    h.payloadOff = (uint32_t)sizeof(RoboRdrvHeader);
    h.payloadLen = plen;
    if (keyId)
        CopyN(h.keyId, keyId, 16);
    else
        ZeroN(h.keyId, 16);
    ZeroN(h.hash, 32);
    ZeroN(h.sig, 64);
    HdrWrite(p, &h);
    if (plen)
        CopyN(p + h.payloadOff, (const uint8_t *)payload, plen);
    if (HashPkg(p, total, &h, h.hash) != 0)
        return -RoboEverify;
    if (sign) {
        if (!sk)
            return -RoboEinval;
        if (Ed25519Sign(h.sig, h.hash, 32, sk) != 0)
            return -RoboEbadsig;
    }
    HdrWrite(p, &h);
    return (int)total;
}

int RdrvVerify(const void *pkg, size_t len, RoboRdrvInfo *out, int testMode)
{
    const uint8_t *p = (const uint8_t *)pkg;
    RoboRdrvHeader h;
    uint8_t got[32];
    const uint8_t *pk;
    unsigned i;
    int testPkg;

    if (!pkg)
        return -RoboEinval;
    if (HdrParse(p, len, &h) != 0)
        return -RoboEinval;
    if (h.magic[0] != MAGIC[0] || h.magic[1] != MAGIC[1] ||
        h.magic[2] != MAGIC[2] || h.magic[3] != MAGIC[3])
        return -RoboEinval;
    if (h.version != RoboRdrvVersion)
        return -RoboEinval;
    if (h.headerSize != sizeof(RoboRdrvHeader))
        return -RoboEinval;
    if (h.headerSize > len)
        return -RoboEinval;
    if (h.payloadOff < h.headerSize || h.payloadOff > len)
        return -RoboEinval;
    if (h.payloadLen > len - h.payloadOff)
        return -RoboEinval;
    if (h.arch != RoboArchX8664)
        return -RoboEnotsup;
    if (!DriverAbiOk(h.abiMajor, h.abiMinor))
        return -RoboEbadabi;
    if (HashPkg(p, len, &h, got) != 0)
        return -RoboEverify;
    for (i = 0; i < 32; i++) {
        if (got[i] != h.hash[i])
            return -RoboEverify;
    }
    testPkg = (h.flags & RoboRdrvFlagTest) != 0;
    pk = FindKey(h.keyId);
    if (out) {
        out->flags = h.flags;
        out->abiMajor = h.abiMajor;
        out->abiMinor = h.abiMinor;
        out->arch = h.arch;
        out->pkgVersion = h.pkgVersion;
        out->payloadOff = h.payloadOff;
        out->payloadLen = h.payloadLen;
        CopyN(out->keyId, h.keyId, 16);
        CopyN(out->hash, h.hash, 32);
        out->signedOk = 0;
        out->testPackage = testPkg;
    }
    {
        unsigned z = 0;
        for (i = 0; i < 64; i++)
            z |= h.sig[i];
        if (!z) {
            if (testMode && testPkg)
                return 0;
            DriverNoteReject();
            return -RoboEbadsig;
        }
    }
    if (!pk) {
        DriverNoteReject();
        return -RoboEperm;
    }
    if (Ed25519Verify(h.sig, h.hash, 32, pk) != 0) {
        DriverNoteReject();
        return -RoboEbadsig;
    }
    if (out)
        out->signedOk = 1;
    return 0;
}

int RdrvSelftest(void)
{
    static const uint8_t seed[32] = {
        0x9d,0x61,0xb1,0x9d,0xef,0xfd,0x5a,0x60,0xba,0x84,0x4a,0xf4,0x92,0xec,
        0x2c,0xc4,0x44,0x49,0xc5,0x69,0x7b,0x32,0x69,0x19,0x70,0x3b,0xac,0x03,
        0x1c,0xae,0x7f,0x60
    };
    static const uint8_t kid[16] = {
        'r','o','b','o','-','t','e','s','t','-','k','e','y','-','0','1'
    };
    uint8_t pk[32], sk[64], pkg[256], tamp[256];
    uint8_t payload[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    RoboRdrvInfo info;
    int n, i, ok = 1;

    RdrvTrustClear();
    if (Ed25519Keypair(pk, sk, seed) != 0)
        return 0;
    if (RdrvTrustAdd(kid, pk) != 0)
        return 0;

    n = RdrvBuild(pkg, sizeof(pkg), payload, 8, 0, 1, 0,
                        RoboArchX8664, 0x010200u, kid, sk, 1);
    if (n <= 0 || RdrvVerify(pkg, (size_t)n, &info, 0) != 0 ||
        !info.signedOk)
        ok = 0;

    for (i = 0; i < n; i++)
        tamp[i] = pkg[i];
    tamp[sizeof(RoboRdrvHeader)] ^= 1;
    if (RdrvVerify(tamp, (size_t)n, 0, 0) == 0)
        ok = 0;

    n = RdrvBuild(pkg, sizeof(pkg), payload, 8, 0, 1, 0,
                        RoboArchX8664, 1, kid, 0, 0);
    if (n <= 0 || RdrvVerify(pkg, (size_t)n, 0, 0) == 0)
        ok = 0;

    n = RdrvBuild(pkg, sizeof(pkg), payload, 8, RoboRdrvFlagTest, 1, 0,
                        RoboArchX8664, 1, kid, 0, 0);
    if (n <= 0 || RdrvVerify(pkg, (size_t)n, 0, 0) == 0)
        ok = 0;
    if (n <= 0 || RdrvVerify(pkg, (size_t)n, 0, 1) != 0)
        ok = 0;

    n = RdrvBuild(pkg, sizeof(pkg), payload, 8, 0, 2, 0,
                        RoboArchX8664, 1, kid, sk, 1);
    if (n <= 0 || RdrvVerify(pkg, (size_t)n, 0, 0) == 0)
        ok = 0;

    n = RdrvBuild(pkg, sizeof(pkg), payload, 8, 0, 1, 0,
                        RoboArchArm64, 1, kid, sk, 1);
    if (n <= 0 || RdrvVerify(pkg, (size_t)n, 0, 0) == 0)
        ok = 0;

    RdrvTrustClear();
    n = RdrvBuild(pkg, sizeof(pkg), payload, 8, 0, 1, 0,
                        RoboArchX8664, 1, kid, sk, 1);
    if (n <= 0 || RdrvVerify(pkg, (size_t)n, 0, 0) == 0)
        ok = 0;

    (void)ok;
    return ok;
}
