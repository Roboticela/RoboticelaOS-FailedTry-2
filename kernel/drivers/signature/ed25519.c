/*
 * Ed25519 (RFC 8032) from TweetNaCl field arithmetic.
 * Public domain algorithm; integer-only for -mgeneral-regs-only.
 */
#include "drivers/signature/ed25519.h"
#include "drivers/signature/sha512.h"

typedef int64_t gf[16];

static const gf gf0 = {0};
static const gf gf1 = {1};
static const gf D = {
    0x78a3, 0x1359, 0x4dca, 0x75eb, 0xd8ab, 0x4141, 0x0a4d, 0x0070,
    0xe898, 0x7779, 0x4079, 0x8cc7, 0xfe73, 0x2b6f, 0x6cee, 0x5203
};
static const gf D2 = {
    0xf159, 0x26b2, 0x9b94, 0xebd6, 0xb156, 0x8283, 0x149a, 0x00e0,
    0xd130, 0xeef3, 0x80f2, 0x198e, 0xfce7, 0x56df, 0xd9dc, 0x2406
};
static const gf X = {
    0xd51a, 0x8f25, 0x2d60, 0xc956, 0xa7b2, 0x9525, 0xc760, 0x692c,
    0xdc5c, 0xfdd6, 0xe231, 0xc0a4, 0x53fe, 0xcd6e, 0x36d3, 0x2169
};
static const gf Y = {
    0x6658, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666,
    0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666
};
static const gf I = {
    0xa0b0, 0x4a0e, 0x1b27, 0xc4ee, 0xe478, 0xad2f, 0x1806, 0x2f43,
    0xd7a7, 0x3dfb, 0x0099, 0x2b4d, 0xdf0b, 0x4fc1, 0x2480, 0x2b83
};

static void set25519(gf r, const gf a)
{
    int i;
    for (i = 0; i < 16; i++)
        r[i] = a[i];
}

static void car25519(gf o)
{
    int i;
    int64_t c;
    for (i = 0; i < 16; i++) {
        o[i] += (1LL << 16);
        c = o[i] >> 16;
        o[(i + 1) * (i < 15)] += c - 1 + 37 * (c - 1) * (i == 15);
        o[i] -= c << 16;
    }
}

static void sel25519(gf p, gf q, int b)
{
    int64_t t, i, c = ~(b - 1);
    for (i = 0; i < 16; i++) {
        t = c & (p[i] ^ q[i]);
        p[i] ^= t;
        q[i] ^= t;
    }
}

static void pack25519(uint8_t *o, const gf n)
{
    int i, j, b;
    gf m, t;
    set25519(t, n);
    car25519(t);
    car25519(t);
    car25519(t);
    for (j = 0; j < 2; j++) {
        m[0] = t[0] - 0xffed;
        for (i = 1; i < 15; i++) {
            m[i] = t[i] - 0xffff - ((m[i - 1] >> 16) & 1);
            m[i - 1] &= 0xffff;
        }
        m[15] = t[15] - 0x7fff - ((m[14] >> 16) & 1);
        b = (m[15] >> 16) & 1;
        m[14] &= 0xffff;
        sel25519(t, m, 1 - b);
    }
    for (i = 0; i < 16; i++) {
        o[2 * i] = (uint8_t)(t[i] & 0xff);
        o[2 * i + 1] = (uint8_t)(t[i] >> 8);
    }
}

static int neq25519(const gf a, const gf b)
{
    uint8_t c[32], d[32];
    int i, x = 0;
    pack25519(c, a);
    pack25519(d, b);
    for (i = 0; i < 32; i++)
        x |= c[i] ^ d[i];
    return x;
}

static uint8_t par25519(const gf a)
{
    uint8_t d[32];
    pack25519(d, a);
    return d[0] & 1;
}

static void unpack25519(gf o, const uint8_t *n)
{
    int i;
    for (i = 0; i < 16; i++)
        o[i] = n[2 * i] + ((int64_t)n[2 * i + 1] << 8);
    o[15] &= 0x7fff;
}

static void A(gf o, const gf a, const gf b)
{
    int i;
    for (i = 0; i < 16; i++)
        o[i] = a[i] + b[i];
}

static void Z(gf o, const gf a, const gf b)
{
    int i;
    for (i = 0; i < 16; i++)
        o[i] = a[i] - b[i];
}

static void M(gf o, const gf a, const gf b)
{
    int64_t t[31];
    int i, j;
    for (i = 0; i < 31; i++)
        t[i] = 0;
    for (i = 0; i < 16; i++)
        for (j = 0; j < 16; j++)
            t[i + j] += a[i] * b[j];
    for (i = 0; i < 15; i++)
        t[i] += 38 * t[i + 16];
    for (i = 0; i < 16; i++)
        o[i] = t[i];
    car25519(o);
    car25519(o);
}

static void S(gf o, const gf a)
{
    M(o, a, a);
}

static void inv25519(gf o, const gf i)
{
    gf c;
    int a;
    set25519(c, i);
    for (a = 253; a >= 0; a--) {
        S(c, c);
        if (a != 2 && a != 4)
            M(c, c, i);
    }
    set25519(o, c);
}

static void pow2523(gf o, const gf i)
{
    gf c;
    int a;
    set25519(c, i);
    for (a = 250; a >= 0; a--) {
        S(c, c);
        if (a != 1)
            M(c, c, i);
    }
    set25519(o, c);
}

static void add(gf p[4], gf q[4])
{
    gf a, b, c, d, t, e, f, g, h;
    Z(a, p[1], p[0]);
    Z(t, q[1], q[0]);
    M(a, a, t);
    A(b, p[0], p[1]);
    A(t, q[0], q[1]);
    M(b, b, t);
    M(c, p[3], q[3]);
    M(c, c, D2);
    M(d, p[2], q[2]);
    A(d, d, d);
    Z(e, b, a);
    Z(f, d, c);
    A(g, d, c);
    A(h, b, a);
    M(p[0], e, f);
    M(p[1], h, g);
    M(p[2], g, f);
    M(p[3], e, h);
}

static void cswap(gf p[4], gf q[4], uint8_t b)
{
    int i;
    for (i = 0; i < 4; i++)
        sel25519(p[i], q[i], b);
}

static void pack(uint8_t *r, gf p[4])
{
    gf tx, ty, zi;
    inv25519(zi, p[2]);
    M(tx, p[0], zi);
    M(ty, p[1], zi);
    pack25519(r, ty);
    r[31] ^= (uint8_t)(par25519(tx) << 7);
}

static void scalarmult(gf p[4], gf q[4], const uint8_t *s)
{
    int i;
    set25519(p[0], gf0);
    set25519(p[1], gf1);
    set25519(p[2], gf1);
    set25519(p[3], gf0);
    for (i = 255; i >= 0; --i) {
        uint8_t b = (s[i / 8] >> (i & 7)) & 1;
        cswap(p, q, b);
        add(q, p);
        add(p, p);
        cswap(p, q, b);
    }
}

static void scalarbase(gf p[4], const uint8_t *s)
{
    gf q[4];
    set25519(q[0], X);
    set25519(q[1], Y);
    set25519(q[2], gf1);
    M(q[3], X, Y);
    scalarmult(p, q, s);
}

static const uint64_t L[32] = {
    0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58, 0xd6, 0x9c, 0xf7, 0xa2,
    0xde, 0xf9, 0xde, 0x14, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x10
};

static void modL(uint8_t *r, int64_t x[64])
{
    int64_t carry;
    int i, j;
    for (i = 63; i >= 32; --i) {
        carry = 0;
        for (j = i - 32; j < i - 12; ++j) {
            x[j] += carry - 16 * x[i] * (int64_t)L[j - (i - 32)];
            carry = (x[j] + 128) >> 8;
            x[j] -= carry << 8;
        }
        x[j] += carry;
        x[i] = 0;
    }
    carry = 0;
    for (j = 0; j < 32; j++) {
        x[j] += carry - (x[31] >> 4) * (int64_t)L[j];
        carry = x[j] >> 8;
        x[j] &= 255;
    }
    for (j = 0; j < 32; j++)
        x[j] -= carry * (int64_t)L[j];
    for (i = 0; i < 32; i++) {
        x[i + 1] += x[i] >> 8;
        r[i] = (uint8_t)(x[i] & 255);
    }
}

static void reduce(uint8_t *r)
{
    int64_t x[64];
    int i;
    for (i = 0; i < 64; i++)
        x[i] = (int64_t)((uint64_t)r[i]);
    for (i = 0; i < 64; i++)
        r[i] = 0;
    modL(r, x);
}

static int unpackneg(gf r[4], const uint8_t p[32])
{
    gf t, chk, num, den, den2, den4, den6;
    set25519(r[2], gf1);
    unpack25519(r[1], p);
    S(num, r[1]);
    M(den, num, D);
    Z(num, num, r[2]);
    A(den, r[2], den);
    S(den2, den);
    S(den4, den2);
    M(den6, den4, den2);
    M(t, den6, num);
    M(t, t, den);
    pow2523(t, t);
    M(t, t, num);
    M(t, t, den);
    M(t, t, den);
    M(r[0], t, den);
    S(chk, r[0]);
    M(chk, chk, den);
    if (neq25519(chk, num))
        M(r[0], r[0], I);
    S(chk, r[0]);
    M(chk, chk, den);
    if (neq25519(chk, num))
        return -1;
    if (par25519(r[0]) == (p[31] >> 7))
        Z(r[0], gf0, r[0]);
    M(r[3], r[0], r[1]);
    return 0;
}

static int verify32(const uint8_t *x, const uint8_t *y)
{
    unsigned i;
    uint8_t d = 0;
    for (i = 0; i < 32; i++)
        d |= (uint8_t)(x[i] ^ y[i]);
    return (1 & ((d - 1) >> 8)) - 1;
}

int robo_ed25519_keypair(uint8_t pk[32], uint8_t sk[64], const uint8_t seed[32])
{
    uint8_t d[64];
    gf p[4];
    unsigned i;
    if (!pk || !sk || !seed)
        return -1;
    robo_sha512(seed, 32, d);
    d[0] &= 248;
    d[31] &= 127;
    d[31] |= 64;
    scalarbase(p, d);
    pack(pk, p);
    for (i = 0; i < 32; i++)
        sk[i] = seed[i];
    for (i = 0; i < 32; i++)
        sk[32 + i] = pk[i];
    return 0;
}

int robo_ed25519_sign(uint8_t sig[64], const uint8_t *msg, size_t len,
                      const uint8_t sk[64])
{
    uint8_t d[64], h[64], r[64], sm[64];
    gf p[4];
    int64_t x[64];
    size_t i, j;
    uint8_t buf[256];

    if (!sig || !sk || (len && !msg))
        return -1;
    robo_sha512(sk, 32, d);
    d[0] &= 248;
    d[31] &= 127;
    d[31] |= 64;

    if (len + 64 > sizeof(buf))
        return -1;
    for (i = 0; i < 32; i++)
        buf[32 + i] = d[32 + i];
    for (i = 0; i < len; i++)
        buf[64 + i] = msg[i];
    robo_sha512(buf + 32, len + 32, r);
    reduce(r);
    scalarbase(p, r);
    pack(sm, p);
    for (i = 0; i < 32; i++)
        buf[i] = sm[i];
    for (i = 0; i < 32; i++)
        buf[32 + i] = sk[32 + i];
    robo_sha512(buf, len + 64, h);
    reduce(h);
    for (i = 0; i < 64; i++)
        x[i] = 0;
    for (i = 0; i < 32; i++)
        x[i] = (int64_t)((uint64_t)r[i]);
    for (i = 0; i < 32; i++)
        for (j = 0; j < 32; j++)
            x[i + j] += (int64_t)((uint64_t)h[i]) * (int64_t)((uint64_t)d[j]);
    modL(sm + 32, x);
    for (i = 0; i < 64; i++)
        sig[i] = sm[i];
    return 0;
}

int robo_ed25519_verify(const uint8_t sig[64], const uint8_t *msg, size_t len,
                        const uint8_t pk[32])
{
    uint8_t t[32], h[64];
    gf p[4], q[4];
    size_t i;
    uint8_t tmp[256];

    if (!sig || !pk || (len && !msg))
        return -1;
    if (unpackneg(q, pk))
        return -1;
    if (len + 64 > sizeof(tmp))
        return -1;
    for (i = 0; i < 32; i++)
        tmp[i] = sig[i];
    for (i = 0; i < 32; i++)
        tmp[32 + i] = pk[i];
    for (i = 0; i < len; i++)
        tmp[64 + i] = msg[i];
    robo_sha512(tmp, len + 64, h);
    reduce(h);
    scalarmult(p, q, h);
    scalarbase(q, sig + 32);
    add(p, q);
    pack(t, p);
    if (verify32(sig, t))
        return -1;
    return 0;
}

int robo_ed25519_selftest(void)
{
    static const uint8_t seed[32] = {
        0x9d,0x61,0xb1,0x9d,0xef,0xfd,0x5a,0x60,0xba,0x84,0x4a,0xf4,0x92,0xec,
        0x2c,0xc4,0x44,0x49,0xc5,0x69,0x7b,0x32,0x69,0x19,0x70,0x3b,0xac,0x03,
        0x1c,0xae,0x7f,0x60
    };
    static const uint8_t want_pk[32] = {
        0xd7,0x5a,0x98,0x01,0x82,0xb1,0x0a,0xb7,0xd5,0x4b,0xfe,0xd3,0xc9,0x64,
        0x07,0x3a,0x0e,0xe1,0x72,0xf3,0xda,0xa6,0x23,0x25,0xaf,0x02,0x1a,0x68,
        0xf7,0x07,0x51,0x1a
    };
    static const uint8_t want_sig[64] = {
        0xe5,0x56,0x43,0x00,0xc3,0x60,0xac,0x72,0x90,0x86,0xe2,0xcc,0x80,0x6e,
        0x82,0x8a,0x84,0x87,0x7f,0x1e,0xb8,0xe5,0xd9,0x74,0xd8,0x73,0xe0,0x65,
        0x22,0x49,0x01,0x55,0x5f,0xb8,0x82,0x15,0x90,0xa3,0x3b,0xac,0xc6,0x1e,
        0x39,0x70,0x1c,0xf9,0xb4,0x6b,0xd2,0x5b,0xf5,0xf0,0x59,0x5b,0xbe,0x24,
        0x65,0x51,0x41,0x43,0x8e,0x7a,0x10,0x0b
    };
    uint8_t pk[32], sk[64], sig[64];
    unsigned i;
    uint8_t bad[64];

    if (robo_ed25519_keypair(pk, sk, seed) != 0)
        return 0;
    for (i = 0; i < 32; i++) {
        if (pk[i] != want_pk[i])
            return 0;
    }
    if (robo_ed25519_sign(sig, 0, 0, sk) != 0)
        return 0;
    for (i = 0; i < 64; i++) {
        if (sig[i] != want_sig[i])
            return 0;
    }
    if (robo_ed25519_verify(want_sig, 0, 0, want_pk) != 0)
        return 0;
    for (i = 0; i < 64; i++)
        bad[i] = want_sig[i];
    bad[0] ^= 1;
    if (robo_ed25519_verify(bad, 0, 0, want_pk) == 0)
        return 0;
    return 1;
}
