#include "roboboot.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t rotr(uint32_t x, uint32_t n)
{
    return (x >> n) | (x << (32u - n));
}

static void sha256_compress(uint32_t h[8], const uint8_t block[64])
{
    static const uint32_t k[64] = {
        0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
        0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
        0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
        0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
        0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
        0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
        0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
        0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u
    };
    uint32_t w[64];
    int t;
    uint32_t a, b, c, d, e, f, g, hh;

    for (t = 0; t < 16; t++) {
        w[t] = ((uint32_t)block[t * 4] << 24) | ((uint32_t)block[t * 4 + 1] << 16) |
               ((uint32_t)block[t * 4 + 2] << 8) | (uint32_t)block[t * 4 + 3];
    }
    for (t = 16; t < 64; t++) {
        uint32_t s0 = rotr(w[t - 15], 7) ^ rotr(w[t - 15], 18) ^ (w[t - 15] >> 3);
        uint32_t s1 = rotr(w[t - 2], 17) ^ rotr(w[t - 2], 19) ^ (w[t - 2] >> 10);
        w[t] = w[t - 16] + s0 + w[t - 7] + s1;
    }
    a = h[0]; b = h[1]; c = h[2]; d = h[3];
    e = h[4]; f = h[5]; g = h[6]; hh = h[7];
    for (t = 0; t < 64; t++) {
        uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t temp1 = hh + S1 + ch + k[t] + w[t];
        uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = S0 + maj;
        hh = g; g = f; f = e; e = d + temp1;
        d = c; c = b; b = a; a = temp1 + temp2;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d;
    h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
}

rb_status rb_sha256(const void *data, size_t len, uint8_t out[32])
{
    uint32_t h[8] = {
        0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
        0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u
    };
    const uint8_t *p = (const uint8_t *)data;
    uint64_t bitlen = (uint64_t)len * 8ull;
    uint8_t block[64];
    size_t n = len;
    int i;

    while (n >= 64) {
        sha256_compress(h, p);
        p += 64;
        n -= 64;
    }
    memset(block, 0, 64);
    if (n) memcpy(block, p, n);
    block[n] = 0x80;
    if (n >= 56) {
        sha256_compress(h, block);
        memset(block, 0, 64);
    }
    for (i = 0; i < 8; i++)
        block[63 - i] = (uint8_t)(bitlen >> (8 * i));
    sha256_compress(h, block);

    for (i = 0; i < 8; i++) {
        out[i * 4] = (uint8_t)(h[i] >> 24);
        out[i * 4 + 1] = (uint8_t)(h[i] >> 16);
        out[i * 4 + 2] = (uint8_t)(h[i] >> 8);
        out[i * 4 + 3] = (uint8_t)h[i];
    }
    return RB_OK;
}

void rb_hex(const uint8_t *in, size_t n, char *out, size_t cap)
{
    static const char *d = "0123456789abcdef";
    size_t i;
    if (!out || cap == 0) return;
    for (i = 0; i < n && (i * 2 + 1) < cap; i++) {
        out[i * 2] = d[in[i] >> 4];
        out[i * 2 + 1] = d[in[i] & 15];
    }
    if (i * 2 < cap) out[i * 2] = 0;
    else out[cap - 1] = 0;
}

static int hexval(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

int rb_unhex(const char *in, uint8_t *out, size_t cap)
{
    int n = 0;
    if (!in) return -1;
    while (in[0] && in[1]) {
        int a = hexval(in[0]);
        int b = hexval(in[1]);
        if (a < 0 || b < 0) break;
        if ((size_t)n >= cap) return -1;
        out[n++] = (uint8_t)((a << 4) | b);
        in += 2;
    }
    return n;
}

rb_status rb_lock_hash(const char *password, const uint8_t *salt, size_t salt_len,
                      int rounds, uint8_t out[32])
{
    uint8_t buf[160];
    size_t plen = password ? (size_t)rb_strlen(password) : 0;
    int i;
    if (rounds < 1) rounds = 1;
    if (salt_len > 64) salt_len = 64;
    if (plen > 64) plen = 64;
    memset(buf, 0, sizeof(buf));
    memcpy(buf, salt, salt_len);
    memcpy(buf + salt_len, password ? password : "", plen);
    rb_sha256(buf, salt_len + plen, out);
    for (i = 1; i < rounds; i++) {
        uint8_t mix[96];
        memset(mix, 0, sizeof(mix));
        memcpy(mix, out, 32);
        memcpy(mix + 32, salt, salt_len);
        mix[32 + salt_len] = (uint8_t)(i & 255);
        mix[33 + salt_len] = (uint8_t)((i >> 8) & 255);
        rb_sha256(mix, 34 + salt_len, out);
    }
    return RB_OK;
}

int rb_lock_verify(const char *spec, const char *password)
{
    char tmp[192];
    char *p1, *p2;
    int rounds;
    uint8_t salt[32], expect[32], got[32];
    int nsalt, nhash;
    if (!spec || !spec[0]) return 1;
    rb_strcpy(tmp, sizeof(tmp), spec);
    p1 = strchr(tmp, ':');
    if (!p1) return 0;
    *p1 = 0;
    p2 = strchr(p1 + 1, ':');
    if (!p2) return 0;
    *p2 = 0;
    rounds = atoi(tmp);
    nsalt = rb_unhex(p1 + 1, salt, sizeof(salt));
    nhash = rb_unhex(p2 + 1, expect, sizeof(expect));
    if (nsalt <= 0 || nhash != 32) return 0;
    rb_lock_hash(password, salt, (size_t)nsalt, rounds, got);
    return memcmp(got, expect, 32) == 0;
}
