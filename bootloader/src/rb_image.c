#include "roboboot.h"

#include <string.h>

rb_status rb_image_load_mem(const void *data, size_t len, rb_image *out)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t w, h, fmt;
    size_t need, i;
    if (!out) return RB_ERR;
    rb_zero(out, sizeof(*out));
    if (!p || len < 16 || memcmp(p, "RBIM", 4) != 0) return RB_ERR_FORMAT;
    memcpy(&w, p + 4, 4);
    memcpy(&h, p + 8, 4);
    memcpy(&fmt, p + 12, 4);
    if (fmt != 0 || w == 0 || h == 0 || w > 4096 || h > 4096) return RB_ERR_FORMAT;
    need = (size_t)w * (size_t)h * 4u;
    if (len < 16 + need) return RB_ERR_FORMAT;
    out->px = (uint32_t *)rb_alloc(need);
    if (!out->px) return RB_ERR_NOMEM;
    out->w = (int)w;
    out->h = (int)h;
    for (i = 0; i < (size_t)w * (size_t)h; i++) {
        const uint8_t *c = p + 16 + i * 4;
        out->px[i] = rb_rgba(c[0], c[1], c[2], c[3]);
    }
    return RB_OK;
}

rb_status rb_image_resize(const rb_image *src, int w, int h, rb_image *dst)
{
    int x, y;
    if (!src || !src->px || !dst || w < 1 || h < 1) return RB_ERR;
    if (w > 4096 || h > 4096) return RB_ERR;
    if (src == dst) return RB_ERR;
    rb_image_free(dst);
    dst->px = (uint32_t *)rb_alloc((size_t)w * (size_t)h * 4u);
    if (!dst->px) return RB_ERR_NOMEM;
    dst->w = w;
    dst->h = h;
    if (src->w == w && src->h == h) {
        memcpy(dst->px, src->px, (size_t)w * (size_t)h * 4u);
        return RB_OK;
    }
    for (y = 0; y < h; y++) {
        int sy = (int)(((int64_t)y * src->h) / h);
        const uint32_t *srow;
        uint32_t *drow;
        if (sy < 0) sy = 0;
        if (sy >= src->h) sy = src->h - 1;
        srow = src->px + sy * src->w;
        drow = dst->px + y * w;
        for (x = 0; x < w; x++) {
            int sx = (int)(((int64_t)x * src->w) / w);
            if (sx < 0) sx = 0;
            if (sx >= src->w) sx = src->w - 1;
            drow[x] = srow[sx];
        }
    }
    return RB_OK;
}

void rb_image_free(rb_image *im)
{
    if (!im) return;
    rb_free(im->px);
    rb_zero(im, sizeof(*im));
}

rb_status rb_font_load_mem(const void *data, size_t len, rb_font *out)
{
    const uint8_t *p = (const uint8_t *)data;
    uint16_t px, first, last, aw, ah, count;
    size_t i, atlas_sz, header;
    if (!out) return RB_ERR;
    rb_zero(out, sizeof(*out));
    if (!p || len < 16 || memcmp(p, "RBFN", 4) != 0) return RB_ERR_FORMAT;
    memcpy(&px, p + 4, 2);
    memcpy(&first, p + 6, 2);
    memcpy(&last, p + 8, 2);
    memcpy(&aw, p + 10, 2);
    memcpy(&ah, p + 12, 2);
    memcpy(&count, p + 14, 2);
    if (count > 128 || last < first || count != (uint16_t)(last - first + 1)) return RB_ERR_FORMAT;
    header = 16 + (size_t)count * 14u;
    atlas_sz = (size_t)aw * (size_t)ah;
    if (len < header + atlas_sz) return RB_ERR_FORMAT;
    out->px = px;
    out->first = first;
    out->last = last;
    out->count = count;
    out->atlas_w = aw;
    out->atlas_h = ah;
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
    out->atlas = (uint8_t *)rb_alloc(atlas_sz);
    if (!out->atlas) return RB_ERR_NOMEM;
    memcpy(out->atlas, p + header, atlas_sz);
    return RB_OK;
}

void rb_font_free(rb_font *f)
{
    if (!f) return;
    rb_free(f->atlas);
    rb_zero(f, sizeof(*f));
}
