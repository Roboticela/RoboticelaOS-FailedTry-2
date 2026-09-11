#include "roboboot.h"

#include <math.h>
#include <string.h>

void rb_gfx_clear(rb_surface *s, uint32_t c)
{
    int i, n;
    if (!s || !s->px) return;
    n = s->w * s->h;
    for (i = 0; i < n; i++) s->px[i] = c;
    rb_fb_damage_full(s);
}

void rb_fb_damage_full(rb_surface *s)
{
    if (!s) return;
    s->damage_x = 0;
    s->damage_y = 0;
    s->damage_w = 0;
    s->damage_h = 0;
}

void rb_gfx_dot(rb_surface *s, int x, int y, uint32_t c)
{
    if (!s || !s->px) return;
    if ((unsigned)x >= (unsigned)s->w || (unsigned)y >= (unsigned)s->h) return;
    s->px[y * s->w + x] = rb_blend(s->px[y * s->w + x], c);
}

void rb_gfx_fill_box(rb_surface *s, int x, int y, int w, int h, uint32_t c)
{
    int ix, iy;
    if (!s || !s->px || w <= 0 || h <= 0) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x >= s->w || y >= s->h) return;
    if (x + w > s->w) w = s->w - x;
    if (y + h > s->h) h = s->h - y;
    if (w <= 0 || h <= 0) return;
    for (iy = 0; iy < h; iy++) {
        uint32_t *row = s->px + (y + iy) * s->w + x;
        for (ix = 0; ix < w; ix++) row[ix] = c;
    }
}

static float sdf_round_rect(float px, float py, float x, float y, float w, float h, float r)
{
    float cx = x + w * 0.5f;
    float cy = y + h * 0.5f;
    float hx = w * 0.5f;
    float hy = h * 0.5f;
    float dx = fabsf(px - cx) - (hx - r);
    float dy = fabsf(py - cy) - (hy - r);
    float ax = dx > 0.f ? dx : 0.f;
    float ay = dy > 0.f ? dy : 0.f;
    float outside = sqrtf(ax * ax + ay * ay);
    float inside = fminf(fmaxf(dx, dy), 0.f);
    return outside + inside - r;
}

static void cover_blend(rb_surface *s, int x, int y, float cover, uint32_t c)
{
    unsigned a = (c >> 24) & 255u;
    unsigned na;
    if (cover <= 0.f) return;
    if (cover > 1.f) cover = 1.f;
    na = (unsigned)(a * cover + 0.5f);
    rb_gfx_dot(s, x, y, (c & 0x00ffffffu) | (na << 24));
}

void rb_gfx_fill_rect(rb_surface *s, float x, float y, float w, float h, uint32_t c)
{
    rb_gfx_round_rect(s, x, y, w, h, 0.f, c);
}

void rb_gfx_round_rect(rb_surface *s, float x, float y, float w, float h, float r, uint32_t c)
{
    int x0, y0, x1, y1, ix, iy;
    if (!s || w <= 0.f || h <= 0.f) return;
    if (r < 0.f) r = 0.f;
    if (r > w * 0.5f) r = w * 0.5f;
    if (r > h * 0.5f) r = h * 0.5f;
    x0 = (int)floorf(x - 1.f);
    y0 = (int)floorf(y - 1.f);
    x1 = (int)ceilf(x + w + 1.f);
    y1 = (int)ceilf(y + h + 1.f);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > s->w) x1 = s->w;
    if (y1 > s->h) y1 = s->h;
    for (iy = y0; iy < y1; iy++) {
        for (ix = x0; ix < x1; ix++) {
            float d = sdf_round_rect((float)ix + 0.5f, (float)iy + 0.5f, x, y, w, h, r);
            float cover = rb_clampf(0.5f - d, 0.f, 1.f);
            cover_blend(s, ix, iy, cover, c);
        }
    }
}

void rb_gfx_circle(rb_surface *s, float cx, float cy, float r, uint32_t c)
{
    rb_gfx_round_rect(s, cx - r, cy - r, r * 2.f, r * 2.f, r, c);
}

void rb_gfx_round_rect_stroke(rb_surface *s, float x, float y, float w, float h, float r, float t, uint32_t c)
{
    int x0, y0, x1, y1, ix, iy;
    if (!s || w <= 0.f || h <= 0.f || t <= 0.f) return;
    if (r < 0.f) r = 0.f;
    if (r > w * 0.5f) r = w * 0.5f;
    if (r > h * 0.5f) r = h * 0.5f;
    x0 = (int)floorf(x - t - 2.f);
    y0 = (int)floorf(y - t - 2.f);
    x1 = (int)ceilf(x + w + t + 2.f);
    y1 = (int)ceilf(y + h + t + 2.f);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > s->w) x1 = s->w;
    if (y1 > s->h) y1 = s->h;
    for (iy = y0; iy < y1; iy++) {
        for (ix = x0; ix < x1; ix++) {
            float d = fabsf(sdf_round_rect((float)ix + 0.5f, (float)iy + 0.5f, x, y, w, h, r));
            float cover = rb_clampf(0.5f - (d - t * 0.5f), 0.f, 1.f);
            cover_blend(s, ix, iy, cover, c);
        }
    }
}

void rb_gfx_ring(rb_surface *s, float cx, float cy, float r, float t, uint32_t c)
{
    int x0, y0, x1, y1, ix, iy;
    float inner = r - t;
    if (!s || r <= 0.f) return;
    if (inner < 0.f) inner = 0.f;
    x0 = (int)floorf(cx - r - 2.f);
    y0 = (int)floorf(cy - r - 2.f);
    x1 = (int)ceilf(cx + r + 2.f);
    y1 = (int)ceilf(cy + r + 2.f);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > s->w) x1 = s->w;
    if (y1 > s->h) y1 = s->h;
    for (iy = y0; iy < y1; iy++) {
        for (ix = x0; ix < x1; ix++) {
            float dx = (float)ix + 0.5f - cx;
            float dy = (float)iy + 0.5f - cy;
            float d = sqrtf(dx * dx + dy * dy);
            float cover = rb_clampf(0.5f - fabsf(d - (inner + r) * 0.5f) + t * 0.5f, 0.f, 1.f);
            cover_blend(s, ix, iy, cover, c);
        }
    }
}

void rb_gfx_blit(rb_surface *s, const rb_image *im, float x, float y, float w, float h, float alpha)
{
    int x0, y0, x1, y1, ix, iy, dw, dh, dx, dy;
    if (!s || !im || !im->px || w <= 0.f || h <= 0.f || alpha <= 0.f) return;
    if (alpha > 1.f) alpha = 1.f;
    dw = (int)(w + 0.5f);
    dh = (int)(h + 0.5f);
    dx = (int)floorf(x);
    dy = (int)floorf(y);
    if (alpha >= 0.999f && dw == im->w && dh == im->h) {
        int src_y0 = 0, src_x0 = 0;
        int cw = im->w, ch = im->h;
        if (dx < 0) { src_x0 = -dx; cw += dx; dx = 0; }
        if (dy < 0) { src_y0 = -dy; ch += dy; dy = 0; }
        if (dx + cw > s->w) cw = s->w - dx;
        if (dy + ch > s->h) ch = s->h - dy;
        if (cw <= 0 || ch <= 0) return;
        for (iy = 0; iy < ch; iy++) {
            uint32_t *dst = s->px + (dy + iy) * s->w + dx;
            const uint32_t *srcp = im->px + (src_y0 + iy) * im->w + src_x0;
            for (ix = 0; ix < cw; ix++) {
                uint32_t p = srcp[ix];
                unsigned a = (p >> 24) & 255u;
                if (a == 255)
                    dst[ix] = p;
                else if (a)
                    dst[ix] = rb_blend(dst[ix], p);
            }
        }
        return;
    }
    x0 = (int)floorf(x);
    y0 = (int)floorf(y);
    x1 = (int)ceilf(x + w);
    y1 = (int)ceilf(y + h);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > s->w) x1 = s->w;
    if (y1 > s->h) y1 = s->h;
    for (iy = y0; iy < y1; iy++) {
        float v = ((float)iy + 0.5f - y) / h;
        if (v < 0.f || v > 1.f) continue;
        float fy = v * (float)(im->h - 1);
        int y0i = (int)fy;
        int y1i = y0i + 1 < im->h ? y0i + 1 : y0i;
        float ty = fy - (float)y0i;
        for (ix = x0; ix < x1; ix++) {
            float u = ((float)ix + 0.5f - x) / w;
            if (u < 0.f || u > 1.f) continue;
            float fx = u * (float)(im->w - 1);
            int x0i = (int)fx;
            int x1i = x0i + 1 < im->w ? x0i + 1 : x0i;
            float tx = fx - (float)x0i;
            uint32_t p00 = im->px[y0i * im->w + x0i];
            uint32_t p10 = im->px[y0i * im->w + x1i];
            uint32_t p01 = im->px[y1i * im->w + x0i];
            uint32_t p11 = im->px[y1i * im->w + x1i];
            int ch;
            unsigned outc[4];
            for (ch = 0; ch < 4; ch++) {
                int shift = ch == 0 ? 24 : ch == 1 ? 16 : ch == 2 ? 8 : 0;
                float a = (float)((p00 >> shift) & 255u);
                float b = (float)((p10 >> shift) & 255u);
                float c = (float)((p01 >> shift) & 255u);
                float d = (float)((p11 >> shift) & 255u);
                float v0 = a + (b - a) * tx;
                float v1 = c + (d - c) * tx;
                outc[ch] = (unsigned)(v0 + (v1 - v0) * ty + 0.5f);
            }
            outc[0] = (unsigned)(outc[0] * alpha + 0.5f);
            rb_gfx_dot(s, ix, iy, (outc[0] << 24) | (outc[1] << 16) | (outc[2] << 8) | outc[3]);
        }
    }
}

int rb_gfx_text_width(const rb_font *f, const char *text)
{
    int w = 0;
    if (!f || !text || !f->atlas) return 0;
    while (*text) {
        unsigned c = (unsigned char)*text++;
        if (c < (unsigned)f->first || c > (unsigned)f->last) {
            w += f->px / 3;
            continue;
        }
        w += f->glyphs[c - f->first].advance;
    }
    return w;
}

void rb_gfx_text(rb_surface *s, const rb_font *f, float x, float y, const char *text, uint32_t color)
{
    float pen = x;
    if (!s || !f || !f->atlas || !text) return;
    while (*text) {
        unsigned ch = (unsigned char)*text++;
        const rb_glyph *g;
        int gx, gy, ix, iy;
        if (ch < (unsigned)f->first || ch > (unsigned)f->last) {
            pen += f->px / 3;
            continue;
        }
        g = &f->glyphs[ch - f->first];
        for (iy = 0; iy < g->h; iy++) {
            gy = g->y + iy;
            if (gy < 0 || gy >= f->atlas_h) continue;
            for (ix = 0; ix < g->w; ix++) {
                unsigned cov;
                gx = g->x + ix;
                if (gx < 0 || gx >= f->atlas_w) continue;
                cov = f->atlas[gy * f->atlas_w + gx];
                if (!cov) continue;
                {
                    unsigned a = (color >> 24) & 255u;
                    unsigned na = (a * cov + 127u) / 255u;
                    int dx = (int)(pen + (float)g->offx + (float)ix);
                    int dy = (int)(y + (float)g->offy + (float)iy);
                    rb_gfx_dot(s, dx, dy, (color & 0x00ffffffu) | (na << 24));
                }
            }
        }
        pen += g->advance;
    }
}

void rb_gfx_text_center(rb_surface *s, const rb_font *f, float cx, float y, const char *text, uint32_t c)
{
    int w = rb_gfx_text_width(f, text);
    rb_gfx_text(s, f, cx - w * 0.5f, y, text, c);
}
