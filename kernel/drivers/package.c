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

static TrustKey g_keys[ROBO_RDRV_MAX_KEYS];

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

static void copy_n(uint8_t *d, const uint8_t *s, unsigned n)
{
    unsigned i;
    for (i = 0; i < n; i++)
        d[i] = s[i];
}

static void zero_n(uint8_t *d, unsigned n)
{
    unsigned i;
    for (i = 0; i < n; i++)
        d[i] = 0;
}

void robo_rdrv_trust_clear(void)
{
    unsigned i;
    for (i = 0; i < ROBO_RDRV_MAX_KEYS; i++)
        g_keys[i].used = 0;
}

int robo_rdrv_trust_add(const uint8_t key_id[16], const uint8_t pk[32])
{
    unsigned i;
    if (!key_id || !pk)
        return -ROBO_EINVAL;
    for (i = 0; i < ROBO_RDRV_MAX_KEYS; i++) {
        if (!g_keys[i].used) {
            copy_n(g_keys[i].id, key_id, 16);
            copy_n(g_keys[i].pk, pk, 32);
            g_keys[i].used = 1;
            return 0;
        }
    }
    return -ROBO_ENOMEM;
}

static const uint8_t *find_key(const uint8_t id[16])
{
    unsigned i;
    for (i = 0; i < ROBO_RDRV_MAX_KEYS; i++) {
        if (g_keys[i].used && same16(g_keys[i].id, id))
            return g_keys[i].pk;
    }
    return 0;
}

static int hdr_parse(const uint8_t *p, size_t len, RoboRdrvHeader *h)
{
    unsigned i;
    if (len < sizeof(RoboRdrvHeader))
        return -ROBO_EINVAL;
    for (i = 0; i < 4; i++)
        h->magic[i] = p[i];
    h->version = rd16(p + 4);
    h->header_size = rd16(p + 6);
    h->flags = rd32(p + 8);
    h->abi_major = rd32(p + 12);
    h->abi_minor = rd32(p + 16);
    h->arch = rd32(p + 20);
    h->pkg_version = rd32(p + 24);
    h->payload_off = rd32(p + 28);
    h->payload_len = rd32(p + 32);
    copy_n(h->key_id, p + 36, 16);
    copy_n(h->hash, p + 52, 32);
    copy_n(h->sig, p + 84, 64);
    return 0;
}

static void hdr_write(uint8_t *p, const RoboRdrvHeader *h)
{
    unsigned i;
    for (i = 0; i < 4; i++)
        p[i] = h->magic[i];
    wr16(p + 4, h->version);
    wr16(p + 6, h->header_size);
    wr32(p + 8, h->flags);
    wr32(p + 12, h->abi_major);
    wr32(p + 16, h->abi_minor);
    wr32(p + 20, h->arch);
    wr32(p + 24, h->pkg_version);
    wr32(p + 28, h->payload_off);
    wr32(p + 32, h->payload_len);
    copy_n(p + 36, h->key_id, 16);
    copy_n(p + 52, h->hash, 32);
    copy_n(p + 84, h->sig, 64);
}

static int hash_pkg(const uint8_t *pkg, size_t len, const RoboRdrvHeader *h,
                    uint8_t out[32])
{
    uint8_t tmp[sizeof(RoboRdrvHeader)];
    uint8_t acc[32];
    unsigned i;
    const uint8_t *pay;

    if (h->header_size != sizeof(RoboRdrvHeader))
        return -ROBO_EINVAL;
    if (h->payload_off < h->header_size)
        return -ROBO_EINVAL;
    if (h->payload_off > len || h->payload_len > len - h->payload_off)
        return -ROBO_EINVAL;
    copy_n(tmp, pkg, sizeof(RoboRdrvHeader));
    zero_n(tmp + 52, 32);
    zero_n(tmp + 84, 64);
    robo_sha256(tmp, sizeof(RoboRdrvHeader), acc);
    pay = pkg + h->payload_off;
    if (h->payload_len) {
        uint8_t mix[64];
        uint8_t ph[32];
        robo_sha256(pay, h->payload_len, ph);
        for (i = 0; i < 32; i++) {
            mix[i] = acc[i];
            mix[32 + i] = ph[i];
        }
        robo_sha256(mix, 64, out);
    } else {
        for (i = 0; i < 32; i++)
            out[i] = acc[i];
    }
    (void)len;
    return 0;
}

int robo_rdrv_build(void *out, size_t cap, const void *payload, uint32_t plen,
                    uint32_t flags, uint32_t abi_maj, uint32_t abi_min,
                    uint32_t arch, uint32_t ver, const uint8_t key_id[16],
                    const uint8_t sk[64], int sign)
{
    RoboRdrvHeader h;
    uint8_t *p = (uint8_t *)out;
    uint32_t total = (uint32_t)sizeof(RoboRdrvHeader) + plen;
    unsigned i;

    if (!out || cap < total)
        return -ROBO_ENOMEM;
    if (plen && !payload)
        return -ROBO_EINVAL;
    for (i = 0; i < 4; i++)
        h.magic[i] = MAGIC[i];
    h.version = ROBO_RDRV_VERSION;
    h.header_size = (uint16_t)sizeof(RoboRdrvHeader);
    h.flags = flags;
    h.abi_major = abi_maj;
    h.abi_minor = abi_min;
    h.arch = arch;
    h.pkg_version = ver;
    h.payload_off = (uint32_t)sizeof(RoboRdrvHeader);
    h.payload_len = plen;
    if (key_id)
        copy_n(h.key_id, key_id, 16);
    else
        zero_n(h.key_id, 16);
    zero_n(h.hash, 32);
    zero_n(h.sig, 64);
    hdr_write(p, &h);
    if (plen)
        copy_n(p + h.payload_off, (const uint8_t *)payload, plen);
    if (hash_pkg(p, total, &h, h.hash) != 0)
        return -ROBO_EVERIFY;
    if (sign) {
        if (!sk)
            return -ROBO_EINVAL;
        if (robo_ed25519_sign(h.sig, h.hash, 32, sk) != 0)
            return -ROBO_EBADSIG;
    }
    hdr_write(p, &h);
    return (int)total;
}

int robo_rdrv_verify(const void *pkg, size_t len, RoboRdrvInfo *out, int test_mode)
{
    const uint8_t *p = (const uint8_t *)pkg;
    RoboRdrvHeader h;
    uint8_t got[32];
    const uint8_t *pk;
    unsigned i;
    int test_pkg;

    if (!pkg)
        return -ROBO_EINVAL;
    if (hdr_parse(p, len, &h) != 0)
        return -ROBO_EINVAL;
    if (h.magic[0] != MAGIC[0] || h.magic[1] != MAGIC[1] ||
        h.magic[2] != MAGIC[2] || h.magic[3] != MAGIC[3])
        return -ROBO_EINVAL;
    if (h.version != ROBO_RDRV_VERSION)
        return -ROBO_EINVAL;
    if (h.header_size != sizeof(RoboRdrvHeader))
        return -ROBO_EINVAL;
    if (h.header_size > len)
        return -ROBO_EINVAL;
    if (h.payload_off < h.header_size || h.payload_off > len)
        return -ROBO_EINVAL;
    if (h.payload_len > len - h.payload_off)
        return -ROBO_EINVAL;
    if (h.arch != ROBO_ARCH_X86_64)
        return -ROBO_ENOTSUP;
    if (!robo_driver_abi_ok(h.abi_major, h.abi_minor))
        return -ROBO_EBADABI;
    if (hash_pkg(p, len, &h, got) != 0)
        return -ROBO_EVERIFY;
    for (i = 0; i < 32; i++) {
        if (got[i] != h.hash[i])
            return -ROBO_EVERIFY;
    }
    test_pkg = (h.flags & ROBO_RDRV_FLAG_TEST) != 0;
    pk = find_key(h.key_id);
    if (out) {
        out->flags = h.flags;
        out->abi_major = h.abi_major;
        out->abi_minor = h.abi_minor;
        out->arch = h.arch;
        out->pkg_version = h.pkg_version;
        out->payload_off = h.payload_off;
        out->payload_len = h.payload_len;
        copy_n(out->key_id, h.key_id, 16);
        copy_n(out->hash, h.hash, 32);
        out->signed_ok = 0;
        out->test_package = test_pkg;
    }
    {
        unsigned z = 0;
        for (i = 0; i < 64; i++)
            z |= h.sig[i];
        if (!z) {
            if (test_mode && test_pkg)
                return 0;
            robo_driver_note_reject();
            return -ROBO_EBADSIG;
        }
    }
    if (!pk) {
        robo_driver_note_reject();
        return -ROBO_EPERM;
    }
    if (robo_ed25519_verify(h.sig, h.hash, 32, pk) != 0) {
        robo_driver_note_reject();
        return -ROBO_EBADSIG;
    }
    if (out)
        out->signed_ok = 1;
    return 0;
}

int robo_rdrv_selftest(void)
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

    robo_rdrv_trust_clear();
    if (robo_ed25519_keypair(pk, sk, seed) != 0)
        return 0;
    if (robo_rdrv_trust_add(kid, pk) != 0)
        return 0;

    n = robo_rdrv_build(pkg, sizeof(pkg), payload, 8, 0, 1, 0,
                        ROBO_ARCH_X86_64, 0x010200u, kid, sk, 1);
    if (n <= 0 || robo_rdrv_verify(pkg, (size_t)n, &info, 0) != 0 ||
        !info.signed_ok)
        ok = 0;

    for (i = 0; i < n; i++)
        tamp[i] = pkg[i];
    tamp[sizeof(RoboRdrvHeader)] ^= 1;
    if (robo_rdrv_verify(tamp, (size_t)n, 0, 0) == 0)
        ok = 0;

    n = robo_rdrv_build(pkg, sizeof(pkg), payload, 8, 0, 1, 0,
                        ROBO_ARCH_X86_64, 1, kid, 0, 0);
    if (n <= 0 || robo_rdrv_verify(pkg, (size_t)n, 0, 0) == 0)
        ok = 0;

    n = robo_rdrv_build(pkg, sizeof(pkg), payload, 8, ROBO_RDRV_FLAG_TEST, 1, 0,
                        ROBO_ARCH_X86_64, 1, kid, 0, 0);
    if (n <= 0 || robo_rdrv_verify(pkg, (size_t)n, 0, 0) == 0)
        ok = 0;
    if (n <= 0 || robo_rdrv_verify(pkg, (size_t)n, 0, 1) != 0)
        ok = 0;

    n = robo_rdrv_build(pkg, sizeof(pkg), payload, 8, 0, 2, 0,
                        ROBO_ARCH_X86_64, 1, kid, sk, 1);
    if (n <= 0 || robo_rdrv_verify(pkg, (size_t)n, 0, 0) == 0)
        ok = 0;

    n = robo_rdrv_build(pkg, sizeof(pkg), payload, 8, 0, 1, 0,
                        ROBO_ARCH_ARM64, 1, kid, sk, 1);
    if (n <= 0 || robo_rdrv_verify(pkg, (size_t)n, 0, 0) == 0)
        ok = 0;

    robo_rdrv_trust_clear();
    n = robo_rdrv_build(pkg, sizeof(pkg), payload, 8, 0, 1, 0,
                        ROBO_ARCH_X86_64, 1, kid, sk, 1);
    if (n <= 0 || robo_rdrv_verify(pkg, (size_t)n, 0, 0) == 0)
        ok = 0;

    (void)ok;
    return ok;
}
