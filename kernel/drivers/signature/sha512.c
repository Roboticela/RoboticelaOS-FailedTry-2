#include "drivers/signature/sha512.h"

static uint64_t rotr64(uint64_t x, int n)
{
    return (x >> n) | (x << (64 - n));
}

static uint64_t load64(const uint8_t *p)
{
    return ((uint64_t)p[0] << 56) | ((uint64_t)p[1] << 48) |
           ((uint64_t)p[2] << 40) | ((uint64_t)p[3] << 32) |
           ((uint64_t)p[4] << 24) | ((uint64_t)p[5] << 16) |
           ((uint64_t)p[6] << 8) | (uint64_t)p[7];
}

static void store64(uint8_t *p, uint64_t v)
{
    p[0] = (uint8_t)(v >> 56);
    p[1] = (uint8_t)(v >> 48);
    p[2] = (uint8_t)(v >> 40);
    p[3] = (uint8_t)(v >> 32);
    p[4] = (uint8_t)(v >> 24);
    p[5] = (uint8_t)(v >> 16);
    p[6] = (uint8_t)(v >> 8);
    p[7] = (uint8_t)v;
}

static void compress(uint64_t h[8], const uint8_t block[128])
{
    static const uint64_t K[80] = {
        0x428a2f98d728ae22ull,0x7137449123ef65cdull,0xb5c0fbcfec4d3b2full,
        0xe9b5dba58189dbbcull,0x3956c25bf348b538ull,0x59f111f1b605d019ull,
        0x923f82a4af194f9bull,0xab1c5ed5da6d8118ull,0xd807aa98a3030242ull,
        0x12835b0145706fbeull,0x243185be4ee4b28cull,0x550c7dc3d5ffb4e2ull,
        0x72be5d74f27b896full,0x80deb1fe3b1696b1ull,0x9bdc06a725c71235ull,
        0xc19bf174cf692694ull,0xe49b69c19ef14ad2ull,0xefbe4786384f25e3ull,
        0x0fc19dc68b8cd5b5ull,0x240ca1cc77ac9c65ull,0x2de92c6f592b0275ull,
        0x4a7484aa6ea6e483ull,0x5cb0a9dcbd41fbd4ull,0x76f988da831153b5ull,
        0x983e5152ee66dfabull,0xa831c66d2db43210ull,0xb00327c898fb213full,
        0xbf597fc7beef0ee4ull,0xc6e00bf33da88fc2ull,0xd5a79147930aa725ull,
        0x06ca6351e003826full,0x142929670a0e6e70ull,0x27b70a8546d22ffcull,
        0x2e1b21385c26c926ull,0x4d2c6dfc5ac42aedull,0x53380d139d95b3dfull,
        0x650a73548baf63deull,0x766a0abb3c77b2a8ull,0x81c2c92e47edaee6ull,
        0x92722c851482353bull,0xa2bfe8a14cf10364ull,0xa81a664bbc423001ull,
        0xc24b8b70d0f89791ull,0xc76c51a30654be30ull,0xd192e819d6ef5218ull,
        0xd69906245565a910ull,0xf40e35855771202aull,0x106aa07032bbd1b8ull,
        0x19a4c116b8d2d0c8ull,0x1e376c085141ab53ull,0x2748774cdf8eeb99ull,
        0x34b0bcb5e19b48a8ull,0x391c0cb3c5c95a63ull,0x4ed8aa4ae3418acbull,
        0x5b9cca4f7763e373ull,0x682e6ff3d6b2b8a3ull,0x748f82ee5defb2fcull,
        0x78a5636f43172f60ull,0x84c87814a1f0ab72ull,0x8cc702081a6439ecull,
        0x90befffa23631e28ull,0xa4506cebde82bde9ull,0xbef9a3f7b2c67915ull,
        0xc67178f2e372532bull,0xca273eceea26619cull,0xd186b8c721c0c207ull,
        0xeada7dd6cde0eb1eull,0xf57d4f7fee6ed178ull,0x06f067aa72176fbaull,
        0x0a637dc5a2c898a6ull,0x113f9804bef90daeull,0x1b710b35131c471bull,
        0x28db77f523047d84ull,0x32caab7b40c72493ull,0x3c9ebe0a15c9bebcull,
        0x431d67c49c100d4cull,0x4cc5d4becb3e42b6ull,0x597f299cfc657e2aull,
        0x5fcb6fab3ad6faecull,0x6c44198c4a475817ull
    };
    uint64_t w[80], a, b, c, d, e, f, g, hh, t1, t2;
    unsigned t;

    for (t = 0; t < 16; t++)
        w[t] = load64(block + t * 8);
    for (t = 16; t < 80; t++) {
        uint64_t s0 = rotr64(w[t - 15], 1) ^ rotr64(w[t - 15], 8) ^ (w[t - 15] >> 7);
        uint64_t s1 = rotr64(w[t - 2], 19) ^ rotr64(w[t - 2], 61) ^ (w[t - 2] >> 6);
        w[t] = w[t - 16] + s0 + w[t - 7] + s1;
    }
    a = h[0]; b = h[1]; c = h[2]; d = h[3];
    e = h[4]; f = h[5]; g = h[6]; hh = h[7];
    for (t = 0; t < 80; t++) {
        t1 = hh + (rotr64(e, 14) ^ rotr64(e, 18) ^ rotr64(e, 41)) +
             ((e & f) ^ ((~e) & g)) + K[t] + w[t];
        t2 = (rotr64(a, 28) ^ rotr64(a, 34) ^ rotr64(a, 39)) +
             ((a & b) ^ (a & c) ^ (b & c));
        hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d;
    h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
}

void robo_sha512(const void *data, size_t len, uint8_t out[64])
{
    uint64_t h[8] = {
        0x6a09e667f3bcc908ull, 0xbb67ae8584caa73bull,
        0x3c6ef372fe94f82bull, 0xa54ff53a5f1d36f1ull,
        0x510e527fade682d1ull, 0x9b05688c2b3e6c1full,
        0x1f83d9abfb41bd6bull, 0x5be0cd19137e2179ull
    };
    const uint8_t *p = (const uint8_t *)data;
    uint8_t block[128];
    uint64_t bitlen_hi = 0, bitlen_lo = (uint64_t)len << 3;
    size_t i;
    unsigned used = 0;

    if ((uint64_t)len >> 61)
        bitlen_hi = (uint64_t)len >> 61;

    for (i = 0; i < len; i++) {
        block[used++] = p[i];
        if (used == 128) {
            compress(h, block);
            used = 0;
        }
    }
    block[used++] = 0x80;
    if (used > 112) {
        while (used < 128)
            block[used++] = 0;
        compress(h, block);
        used = 0;
    }
    while (used < 112)
        block[used++] = 0;
    store64(block + 112, bitlen_hi);
    store64(block + 120, bitlen_lo);
    compress(h, block);
    for (i = 0; i < 8; i++)
        store64(out + i * 8, h[i]);
}

int robo_sha512_selftest(void)
{
    static const char *abc = "abc";
    uint8_t d[64];
    static const uint8_t want[64] = {
        0xdd,0xaf,0x35,0xa1,0x93,0x61,0x7a,0xba,0xcc,0x41,0x73,0x49,0xae,0x20,
        0x41,0x31,0x12,0xe6,0xfa,0x4e,0x89,0xa9,0x7e,0xa2,0x0a,0x9e,0xee,0xe6,
        0x4b,0x55,0xd3,0x9a,0x21,0x92,0x99,0x2a,0x27,0x4f,0xc1,0xa8,0x36,0xba,
        0x3c,0x23,0xa3,0xfe,0xeb,0xbd,0x45,0x4d,0x44,0x23,0x64,0x3c,0xe8,0x0e,
        0x2a,0x9a,0xc9,0x4f,0xa5,0x4c,0xa4,0x9f
    };
    unsigned i;
    robo_sha512(abc, 3, d);
    for (i = 0; i < 64; i++) {
        if (d[i] != want[i])
            return 0;
    }
    return 1;
}
