#include "roboboot.h"

#include <string.h>

RoboRbStatus ImageLoadMem(const void *data, size_t len, Image *out)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t w, h, fmt;
    size_t need, i;
    if (!out) return RbErr;
    Zero(out, sizeof(*out));
    if (!p || len < 16 || memcmp(p, "RBIM", 4) != 0) return RbErrFormat;
    memcpy(&w, p + 4, 4);
    memcpy(&h, p + 8, 4);
    memcpy(&fmt, p + 12, 4);
    if (fmt != 0 || w == 0 || h == 0 || w > 4096 || h > 4096) return RbErrFormat;
    need = (size_t)w * (size_t)h * 4u;
    if (len < 16 + need) return RbErrFormat;
    out->px = (uint32_t *)Alloc(need);
    if (!out->px) return RbErrNomem;
    out->w = (int)w;
    out->h = (int)h;
    for (i = 0; i < (size_t)w * (size_t)h; i++) {
        const uint8_t *c = p + 16 + i * 4;
        out->px[i] = Rgba(c[0], c[1], c[2], c[3]);
    }
    return RbOk;
}

RoboRbStatus ImageResize(const Image *src, int w, int h, Image *dst)
{
    int x, y;
    if (!src || !src->px || !dst || w < 1 || h < 1) return RbErr;
    if (w > 4096 || h > 4096) return RbErr;
    if (src == dst) return RbErr;
    ImageFree(dst);
    dst->px = (uint32_t *)Alloc((size_t)w * (size_t)h * 4u);
    if (!dst->px) return RbErrNomem;
    dst->w = w;
    dst->h = h;
    if (src->w == w && src->h == h) {
        memcpy(dst->px, src->px, (size_t)w * (size_t)h * 4u);
        return RbOk;
    }
    /* Box-filter downscale (and 1:1-ish upscale) keeps edges sharp. Bilinear
       shrinking is what made the splash logo and picker pics look soft. */
    for (y = 0; y < h; y++) {
        int y0 = (int)(((int64_t)y * src->h) / h);
        int y1 = (int)(((int64_t)(y + 1) * src->h) / h);
        uint32_t *drow = dst->px + y * w;
        if (y1 <= y0) y1 = y0 + 1;
        if (y0 < 0) y0 = 0;
        if (y1 > src->h) y1 = src->h;
        if (y0 >= src->h) y0 = src->h - 1;
        if (y1 <= y0) y1 = y0 + 1;
        for (x = 0; x < w; x++) {
            int x0 = (int)(((int64_t)x * src->w) / w);
            int x1 = (int)(((int64_t)(x + 1) * src->w) / w);
            int sy, sx;
            uint64_t sa = 0, sr = 0, sg = 0, sb = 0;
            if (x1 <= x0) x1 = x0 + 1;
            if (x0 < 0) x0 = 0;
            if (x1 > src->w) x1 = src->w;
            if (x0 >= src->w) x0 = src->w - 1;
            if (x1 <= x0) x1 = x0 + 1;
            for (sy = y0; sy < y1; sy++) {
                const uint32_t *srow = src->px + sy * src->w;
                for (sx = x0; sx < x1; sx++) {
                    uint32_t p = srow[sx];
                    unsigned a = (p >> 24) & 255u;
                    unsigned r = (p >> 16) & 255u;
                    unsigned g = (p >> 8) & 255u;
                    unsigned b = p & 255u;
                    sa += a;
                    sr += (uint64_t)r * a;
                    sg += (uint64_t)g * a;
                    sb += (uint64_t)b * a;
                }
            }
            {
                uint64_t n = (uint64_t)(x1 - x0) * (uint64_t)(y1 - y0);
                unsigned a, r, g, b;
                if (!n) n = 1;
                a = (unsigned)((sa + n / 2) / n);
                if (sa) {
                    r = (unsigned)((sr + sa / 2) / sa);
                    g = (unsigned)((sg + sa / 2) / sa);
                    b = (unsigned)((sb + sa / 2) / sa);
                } else {
                    r = g = b = 0;
                }
                if (r > 255) r = 255;
                if (g > 255) g = 255;
                if (b > 255) b = 255;
                if (a > 255) a = 255;
                drow[x] = (a << 24) | (r << 16) | (g << 8) | b;
            }
        }
    }
    return RbOk;
}

void ImageFree(Image *im)
{
    if (!im) return;
    RoboRbFree(im->px);
    Zero(im, sizeof(*im));
}

RoboRbStatus FontLoadMem(const void *data, size_t len, Font *out)
{
    const uint8_t *p = (const uint8_t *)data;
    uint16_t px, first, last, aw, ah, count;
    size_t i, atlasSz, header;
    if (!out) return RbErr;
    Zero(out, sizeof(*out));
    if (!p || len < 16 || memcmp(p, "RBFN", 4) != 0) return RbErrFormat;
    memcpy(&px, p + 4, 2);
    memcpy(&first, p + 6, 2);
    memcpy(&last, p + 8, 2);
    memcpy(&aw, p + 10, 2);
    memcpy(&ah, p + 12, 2);
    memcpy(&count, p + 14, 2);
    if (count > 128 || last < first || count != (uint16_t)(last - first + 1)) return RbErrFormat;
    header = 16 + (size_t)count * 14u;
    atlasSz = (size_t)aw * (size_t)ah;
    if (len < header + atlasSz) return RbErrFormat;
    out->px = px;
    out->first = first;
    out->last = last;
    out->count = count;
    out->atlasW = aw;
    out->atlasH = ah;
    for (i = 0; i < count; i++) {
        const uint8_t *g = p + 16 + i * 14;
        memcpy(&out->glyphs[i].x, g + 0, 2);
        memcpy(&out->glyphs[i].y, g + 2, 2);
        memcpy(&out->glyphs[i].w, g + 4, 2);
        memcpy(&out->glyphs[i].h, g + 6, 2);
        memcpy(&out->glyphs[i].advance, g + 8, 2);
        memcpy(&out->glyphs[i].offx, g + 10, 2);
        memcpy(&out->glyphs[i].offy, g + 12, 2);
    }
    out->atlas = (uint8_t *)Alloc(atlasSz);
    if (!out->atlas) return RbErrNomem;
    memcpy(out->atlas, p + header, atlasSz);
    return RbOk;
}

void FontFree(Font *f)
{
    if (!f) return;
    RoboRbFree(f->atlas);
    Zero(f, sizeof(*f));
}
