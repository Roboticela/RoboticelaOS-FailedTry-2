#include <robocompositor/compose.h>

static int64_t i64_min(int64_t a, int64_t b)
{
    return a < b ? a : b;
}

static int64_t i64_max(int64_t a, int64_t b)
{
    return a > b ? a : b;
}

int robo_damage_is_empty(const RoboDamageRect *d)
{
    return !d || d->empty || !d->w || !d->h;
}

void robo_damage_clear(RoboDamageRect *d)
{
    if (!d)
        return;
    d->x = 0;
    d->y = 0;
    d->w = 0;
    d->h = 0;
    d->empty = 1;
}

void robo_damage_add(RoboDamageRect *d, int32_t x, int32_t y,
                     uint32_t w, uint32_t h)
{
    RoboDamageRect o;
    if (!d || !w || !h)
        return;
    o.x = x;
    o.y = y;
    o.w = w;
    o.h = h;
    o.empty = 0;
    robo_damage_merge(d, &o);
}

void robo_damage_merge(RoboDamageRect *d, const RoboDamageRect *o)
{
    int64_t x0, y0, x1, y1;
    if (!d || !o || o->empty || !o->w || !o->h)
        return;
    if (d->empty || !d->w || !d->h) {
        *d = *o;
        d->empty = 0;
        return;
    }
    x0 = i64_min((int64_t)d->x, (int64_t)o->x);
    y0 = i64_min((int64_t)d->y, (int64_t)o->y);
    x1 = i64_max((int64_t)d->x + (int64_t)d->w, (int64_t)o->x + (int64_t)o->w);
    y1 = i64_max((int64_t)d->y + (int64_t)d->h, (int64_t)o->y + (int64_t)o->h);
    if (x1 <= x0 || y1 <= y0) {
        robo_damage_clear(d);
        return;
    }
    if (x1 - x0 > (int64_t)ROBO_COMPOSE_MAX_W)
        x1 = x0 + (int64_t)ROBO_COMPOSE_MAX_W;
    if (y1 - y0 > (int64_t)ROBO_COMPOSE_MAX_H)
        y1 = y0 + (int64_t)ROBO_COMPOSE_MAX_H;
    d->x = (int32_t)x0;
    d->y = (int32_t)y0;
    d->w = (uint32_t)(x1 - x0);
    d->h = (uint32_t)(y1 - y0);
    d->empty = 0;
}

int robo_damage_contains(const RoboDamageRect *d, int32_t x, int32_t y)
{
    int64_t xx, yy;
    if (robo_damage_is_empty(d))
        return 0;
    xx = x;
    yy = y;
    return xx >= (int64_t)d->x && yy >= (int64_t)d->y &&
           xx < (int64_t)d->x + (int64_t)d->w &&
           yy < (int64_t)d->y + (int64_t)d->h;
}

void robo_damage_clip(RoboDamageRect *d, uint32_t dw, uint32_t dh)
{
    int64_t x0, y0, x1, y1;
    if (!d || d->empty)
        return;
    x0 = i64_max((int64_t)d->x, 0);
    y0 = i64_max((int64_t)d->y, 0);
    x1 = i64_min((int64_t)d->x + (int64_t)d->w, (int64_t)dw);
    y1 = i64_min((int64_t)d->y + (int64_t)d->h, (int64_t)dh);
    if (x1 <= x0 || y1 <= y0) {
        robo_damage_clear(d);
        return;
    }
    d->x = (int32_t)x0;
    d->y = (int32_t)y0;
    d->w = (uint32_t)(x1 - x0);
    d->h = (uint32_t)(y1 - y0);
}

uint32_t robo_compose_pack(uint32_t pixfmt, uint32_t argb)
{
    unsigned a = (argb >> 24) & 255u;
    unsigned r = (argb >> 16) & 255u;
    unsigned g = (argb >> 8) & 255u;
    unsigned b = argb & 255u;
    uint32_t rgb;
    if (pixfmt == ROBO_COMPOSE_PIX_RGBX)
        rgb = b | (g << 8) | (r << 16);
    else
        rgb = r | (g << 8) | (b << 16);
    return rgb | (a << 24);
}

uint32_t robo_compose_unpack(uint32_t pixfmt, uint32_t p)
{
    unsigned a = (p >> 24) & 255u;
    unsigned r, g, b;
    if (pixfmt == ROBO_COMPOSE_PIX_RGBX) {
        b = p & 255u;
        g = (p >> 8) & 255u;
        r = (p >> 16) & 255u;
    } else {
        r = p & 255u;
        g = (p >> 8) & 255u;
        b = (p >> 16) & 255u;
    }
    return (a << 24) | (r << 16) | (g << 8) | b;
}

uint32_t robo_compose_blend(uint32_t pixfmt, uint32_t dst, uint32_t src,
                            uint32_t opacity)
{
    uint32_t s, d, out;
    unsigned sa, ea, ia;
    unsigned sr, sg, sb, dr, dg, db, r, g, b;
    if (!opacity)
        return dst;
    s = robo_compose_unpack(pixfmt, src);
    sa = (s >> 24) & 255u;
    ea = (sa * (opacity > 255u ? 255u : opacity)) / 255u;
    if (!ea)
        return dst;
    if (ea == 255u)
        return robo_compose_pack(pixfmt, 0xFF000000u | (s & 0x00FFFFFFu));
    d = robo_compose_unpack(pixfmt, dst);
    ia = 255u - ea;
    sr = (s >> 16) & 255u;
    sg = (s >> 8) & 255u;
    sb = s & 255u;
    dr = (d >> 16) & 255u;
    dg = (d >> 8) & 255u;
    db = d & 255u;
    r = (sr * ea + dr * ia) / 255u;
    g = (sg * ea + dg * ia) / 255u;
    b = (sb * ea + db * ia) / 255u;
    out = 0xFF000000u | (r << 16) | (g << 8) | b;
    return robo_compose_pack(pixfmt, out);
}

void robo_compose_scene_reset(RoboComposeScene *s)
{
    unsigned i;
    if (!s)
        return;
    for (i = 0; i < ROBO_COMPOSE_MAX_LAYERS; i++) {
        s->layers[i].used = 0;
        s->zorder[i] = 0;
    }
    s->nlayers = 0;
    robo_damage_clear(&s->damage);
    s->last_blits = 0;
}

int robo_compose_init(RoboComposeScene *s, uint32_t *back, uint32_t *front,
                      uint32_t w, uint32_t h, uint32_t stride, uint32_t pixfmt)
{
    uint32_t bg;
    uint32_t i, n;
    if (!s || !back || !w || !h || !stride)
        return 0;
    if (w > ROBO_COMPOSE_MAX_W || h > ROBO_COMPOSE_MAX_H)
        return 0;
    if (stride < w)
        return 0;
    robo_compose_scene_reset(s);
    s->width = w;
    s->height = h;
    s->stride = stride;
    s->pixfmt = pixfmt;
    s->back = back;
    s->front = front ? front : back;
    s->bg_argb = 0xFF1D1D1F;
    s->frame = 0;
    bg = robo_compose_pack(pixfmt, s->bg_argb);
    n = stride * h;
    for (i = 0; i < n; i++) {
        s->back[i] = bg;
        if (s->front != s->back)
            s->front[i] = bg;
    }
    robo_damage_add(&s->damage, 0, 0, w, h);
    return 1;
}

static void z_remove_id(RoboComposeScene *s, uint32_t id)
{
    unsigned i, j;
    for (i = 0; i < s->nlayers; i++) {
        if (s->zorder[i] == id) {
            for (j = i; j + 1 < s->nlayers; j++)
                s->zorder[j] = s->zorder[j + 1];
            s->nlayers--;
            return;
        }
    }
}

RoboComposeLayer *robo_compose_find(RoboComposeScene *s, uint32_t id)
{
    unsigned i;
    if (!s || !id)
        return 0;
    for (i = 0; i < ROBO_COMPOSE_MAX_LAYERS; i++) {
        if (s->layers[i].used && s->layers[i].window_id == id)
            return &s->layers[i];
    }
    return 0;
}

static void damage_layer(RoboComposeScene *s, const RoboComposeLayer *l)
{
    if (!s || !l || !l->visible)
        return;
    robo_damage_add(&s->damage, l->x, l->y, l->w, l->h);
}

int robo_compose_insert(RoboComposeScene *s, uint32_t id, uint64_t pid,
                        int32_t x, int32_t y, uint32_t w, uint32_t h,
                        uint32_t stride, uint32_t *pixels, uint32_t opacity)
{
    unsigned i;
    RoboComposeLayer *l;
    if (!s || !id || !w || !h)
        return -1;
    l = robo_compose_find(s, id);
    if (!l) {
        for (i = 0; i < ROBO_COMPOSE_MAX_LAYERS; i++) {
            if (!s->layers[i].used) {
                l = &s->layers[i];
                break;
            }
        }
    }
    if (!l)
        return -1;
    if (!l->used) {
        if (s->nlayers >= ROBO_COMPOSE_MAX_LAYERS)
            return -1;
        s->zorder[s->nlayers++] = id;
    } else {
        damage_layer(s, l);
    }
    l->used = 1;
    l->window_id = id;
    l->owner_pid = pid;
    l->x = x;
    l->y = y;
    l->w = w;
    l->h = h;
    l->stride = stride ? stride : w;
    l->pixfmt = s->pixfmt;
    l->pixels = pixels;
    l->opacity = (uint8_t)(opacity > 255u ? 255u : opacity);
    l->visible = 1;
    damage_layer(s, l);
    return 0;
}

int robo_compose_remove(RoboComposeScene *s, uint32_t id)
{
    RoboComposeLayer *l = robo_compose_find(s, id);
    if (!l)
        return -1;
    damage_layer(s, l);
    z_remove_id(s, id);
    l->used = 0;
    l->pixels = 0;
    return 0;
}

int robo_compose_raise(RoboComposeScene *s, uint32_t id)
{
    RoboComposeLayer *l;
    if (!s)
        return -1;
    l = robo_compose_find(s, id);
    if (!l)
        return -1;
    z_remove_id(s, id);
    if (s->nlayers >= ROBO_COMPOSE_MAX_LAYERS)
        return -1;
    s->zorder[s->nlayers++] = id;
    damage_layer(s, l);
    return 0;
}

int robo_compose_lower(RoboComposeScene *s, uint32_t id)
{
    unsigned i;
    RoboComposeLayer *l;
    if (!s)
        return -1;
    l = robo_compose_find(s, id);
    if (!l)
        return -1;
    z_remove_id(s, id);
    for (i = s->nlayers; i > 0; i--)
        s->zorder[i] = s->zorder[i - 1];
    s->zorder[0] = id;
    s->nlayers++;
    damage_layer(s, l);
    return 0;
}

int robo_compose_set_z(RoboComposeScene *s, uint32_t id, uint32_t z)
{
    unsigned i;
    RoboComposeLayer *l;
    if (!s)
        return -1;
    l = robo_compose_find(s, id);
    if (!l)
        return -1;
    z_remove_id(s, id);
    if (z > s->nlayers)
        z = s->nlayers;
    for (i = s->nlayers; i > z; i--)
        s->zorder[i] = s->zorder[i - 1];
    s->zorder[z] = id;
    s->nlayers++;
    damage_layer(s, l);
    return 0;
}

static void blit_layer(RoboComposeScene *s, const RoboComposeLayer *l,
                       const RoboDamageRect *d)
{
    int64_t x0, y0, x1, y1, dx0, dy0, dx1, dy1;
    int64_t xx, yy;
    uint32_t opacity;
    if (!l || !l->used || !l->visible || !l->pixels || !l->w || !l->h)
        return;
    opacity = l->opacity;
    if (!opacity)
        return;
    dx0 = l->x;
    dy0 = l->y;
    dx1 = dx0 + (int64_t)l->w;
    dy1 = dy0 + (int64_t)l->h;
    x0 = i64_max(dx0, (int64_t)d->x);
    y0 = i64_max(dy0, (int64_t)d->y);
    x1 = i64_min(dx1, (int64_t)d->x + (int64_t)d->w);
    y1 = i64_min(dy1, (int64_t)d->y + (int64_t)d->h);
    x0 = i64_max(x0, 0);
    y0 = i64_max(y0, 0);
    x1 = i64_min(x1, (int64_t)s->width);
    y1 = i64_min(y1, (int64_t)s->height);
    if (x1 <= x0 || y1 <= y0)
        return;
    for (yy = y0; yy < y1; yy++) {
        uint32_t sy = (uint32_t)(yy - dy0);
        uint32_t *dst = s->back + (uint32_t)yy * s->stride;
        const uint32_t *src = l->pixels + sy * l->stride;
        for (xx = x0; xx < x1; xx++) {
            uint32_t sx = (uint32_t)(xx - dx0);
            if (sx >= l->w || sy >= l->h)
                continue;
            dst[xx] = robo_compose_blend(s->pixfmt, dst[xx], src[sx], opacity);
            s->last_blits++;
        }
    }
}

int robo_compose_frame(RoboComposeScene *s)
{
    RoboDamageRect d;
    uint32_t bg;
    int64_t x, y;
    unsigned i;
    if (!s || !s->back)
        return 0;
    d = s->damage;
    robo_damage_clip(&d, s->width, s->height);
    if (robo_damage_is_empty(&d))
        return 1;
    s->last_blits = 0;
    bg = robo_compose_pack(s->pixfmt, s->bg_argb);
    for (y = d.y; y < (int64_t)d.y + (int64_t)d.h; y++) {
        uint32_t *row = s->back + (uint32_t)y * s->stride;
        for (x = d.x; x < (int64_t)d.x + (int64_t)d.w; x++)
            row[x] = bg;
    }
    for (i = 0; i < s->nlayers; i++) {
        RoboComposeLayer *l = robo_compose_find(s, s->zorder[i]);
        if (l)
            blit_layer(s, l, &d);
    }
    if (s->front && s->front != s->back) {
        for (y = d.y; y < (int64_t)d.y + (int64_t)d.h; y++) {
            uint32_t *src = s->back + (uint32_t)y * s->stride;
            uint32_t *dst = s->front + (uint32_t)y * s->stride;
            for (x = d.x; x < (int64_t)d.x + (int64_t)d.w; x++)
                dst[x] = src[x];
        }
    }
    s->frame++;
    return 1;
}

uint32_t robo_compose_sample(const RoboComposeScene *s, int32_t x, int32_t y)
{
    const uint32_t *buf;
    if (!s || x < 0 || y < 0)
        return 0;
    if ((uint32_t)x >= s->width || (uint32_t)y >= s->height)
        return 0;
    buf = s->front ? s->front : s->back;
    if (!buf)
        return 0;
    return buf[(uint32_t)y * s->stride + (uint32_t)x];
}
