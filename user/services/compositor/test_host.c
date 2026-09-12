/*
 * Headless CPU compositor tests. Linked on the host against compose.c.
 * Pixel values are packed BGRX (pixfmt 0) with A in the high byte.
 */
#include <stdio.h>
#include <stdint.h>
#include <robocompositor/compose.h>

static int g_fail;
static uint32_t g_back[64 * 48];
static uint32_t g_front[64 * 48];
static uint32_t g_a[32 * 32];
static uint32_t g_b[32 * 32];
static uint32_t g_c[32 * 32];
static uint32_t g_tmp[32 * 32];

static void fail(const char *s)
{
    printf("[FAIL] %s\n", s);
    g_fail = 1;
}

static void pass(const char *s)
{
    printf("[PASS] %s\n", s);
}

static uint32_t px(uint32_t argb)
{
    return robo_compose_pack(ROBO_COMPOSE_PIX_BGRX, argb);
}

static void fill(uint32_t *buf, uint32_t w, uint32_t h, uint32_t argb)
{
    uint32_t i, n = w * h, p = px(argb);
    for (i = 0; i < n; i++)
        buf[i] = p;
}

static int expect_px(RoboComposeScene *s, int x, int y, uint32_t argb,
                     const char *what)
{
    uint32_t got = robo_compose_sample(s, x, y);
    uint32_t want = px(argb);
    if (got != want) {
        printf("[FAIL] %s at %d,%d got %08x want %08x\n",
               what, x, y, got, want);
        g_fail = 1;
        return 0;
    }
    return 1;
}

static void compose_all(RoboComposeScene *s)
{
    robo_compose_frame(s);
}

int main(void)
{
    RoboComposeScene sc;
    RoboDamageRect d;
    uint32_t bg = 0xFF1D1D1F;
    unsigned i;

    printf("========================================\n");
    printf("     ROBOCOMPOSITOR CPU TESTS\n");
    printf("========================================\n");

    if (!robo_compose_init(&sc, g_back, g_front, 64, 48, 64,
                           ROBO_COMPOSE_PIX_BGRX)) {
        fail("init");
        return 1;
    }
    compose_all(&sc);
    robo_damage_clear(&sc.damage);

    /* 1 single window */
    fill(g_a, 16, 12, 0xFFFF0000);
    robo_compose_insert(&sc, 1, 1, 4, 4, 16, 12, 16, g_a, 255);
    compose_all(&sc);
    if (expect_px(&sc, 0, 0, bg, "bg") &&
        expect_px(&sc, 4, 4, 0xFFFF0000, "win A") &&
        expect_px(&sc, 19, 15, 0xFFFF0000, "win A br"))
        pass("single window");
    robo_damage_clear(&sc.damage);

    /* 2/3 multiple + occlusion */
    fill(g_b, 16, 12, 0xFF00FF00);
    fill(g_c, 16, 12, 0xFF0000FF);
    robo_compose_insert(&sc, 2, 1, 12, 8, 16, 12, 16, g_b, 255);
    robo_compose_insert(&sc, 3, 1, 20, 12, 16, 12, 16, g_c, 255);
    compose_all(&sc);
    if (expect_px(&sc, 5, 5, 0xFFFF0000, "A exposed") &&
        expect_px(&sc, 14, 10, 0xFF00FF00, "B over A") &&
        expect_px(&sc, 22, 14, 0xFF0000FF, "C over B"))
        pass("z-order occlusion");
    robo_damage_clear(&sc.damage);

    /* 4 transparency 50% white over black patch */
    {
        RoboComposeScene t;
        uint32_t tb[32 * 32], tf[32 * 32], src[8 * 8];
        uint32_t want;
        robo_compose_init(&t, tb, tf, 32, 32, 32, ROBO_COMPOSE_PIX_BGRX);
        t.bg_argb = 0xFF000000;
        robo_damage_add(&t.damage, 0, 0, 32, 32);
        compose_all(&t);
        robo_damage_clear(&t.damage);
        fill(src, 8, 8, 0x80FFFFFF);
        robo_compose_insert(&t, 9, 1, 2, 2, 8, 8, 8, src, 255);
        compose_all(&t);
        want = robo_compose_blend(ROBO_COMPOSE_PIX_BGRX,
                                  robo_compose_pack(ROBO_COMPOSE_PIX_BGRX,
                                                    0xFF000000),
                                  robo_compose_pack(ROBO_COMPOSE_PIX_BGRX,
                                                    0x80FFFFFF),
                                  255);
        if (robo_compose_sample(&t, 4, 4) == want)
            pass("transparency");
        else
            fail("transparency");
    }

    /* 5 fully transparent */
    {
        uint32_t before = robo_compose_sample(&sc, 1, 1);
        fill(g_tmp, 4, 4, 0x00FF00FF);
        robo_compose_insert(&sc, 4, 1, 1, 1, 4, 4, 4, g_tmp, 255);
        compose_all(&sc);
        if (robo_compose_sample(&sc, 1, 1) == before)
            pass("alpha 0");
        else
            fail("alpha 0");
        robo_compose_remove(&sc, 4);
        robo_damage_clear(&sc.damage);
    }

    /* 6 fully opaque */
    fill(g_tmp, 4, 4, 0xFFFFFFFF);
    robo_compose_insert(&sc, 5, 1, 0, 0, 4, 4, 4, g_tmp, 255);
    compose_all(&sc);
    if (expect_px(&sc, 0, 0, 0xFFFFFFFF, "opaque"))
        pass("alpha 255");
    robo_compose_remove(&sc, 5);
    compose_all(&sc);
    robo_damage_clear(&sc.damage);

    /* 7 global opacity */
    {
        RoboComposeScene t;
        uint32_t tb[16 * 16], tf[16 * 16], src[4 * 4];
        uint32_t want;
        robo_compose_init(&t, tb, tf, 16, 16, 16, ROBO_COMPOSE_PIX_BGRX);
        t.bg_argb = 0xFF000000;
        robo_damage_add(&t.damage, 0, 0, 16, 16);
        compose_all(&t);
        fill(src, 4, 4, 0xFFFFFFFF);
        robo_compose_insert(&t, 1, 1, 0, 0, 4, 4, 4, src, 128);
        compose_all(&t);
        want = robo_compose_blend(ROBO_COMPOSE_PIX_BGRX,
                                  robo_compose_pack(ROBO_COMPOSE_PIX_BGRX,
                                                    0xFF000000),
                                  robo_compose_pack(ROBO_COMPOSE_PIX_BGRX,
                                                    0xFFFFFFFF),
                                  128);
        if (robo_compose_sample(&t, 1, 1) == want)
            pass("global opacity");
        else
            fail("global opacity");
    }

    /* 8 negative position */
    fill(g_tmp, 8, 8, 0xFFFFFF00);
    robo_compose_insert(&sc, 6, 1, -4, -4, 8, 8, 8, g_tmp, 255);
    compose_all(&sc);
    if (expect_px(&sc, 0, 0, 0xFFFFFF00, "neg clip") &&
        expect_px(&sc, 3, 3, 0xFFFFFF00, "neg inside"))
        pass("negative position");
    robo_compose_remove(&sc, 6);
    compose_all(&sc);
    robo_damage_clear(&sc.damage);

    /* 9 oversized */
    fill(g_tmp, 32, 32, 0xFF00FFFF);
    robo_compose_insert(&sc, 7, 1, 50, 30, 32, 32, 32, g_tmp, 255);
    compose_all(&sc);
    if (expect_px(&sc, 63, 47, 0xFF00FFFF, "oversize clip") &&
        robo_compose_sample(&sc, 64, 0) == 0)
        pass("oversized window");
    robo_compose_remove(&sc, 7);
    compose_all(&sc);
    robo_damage_clear(&sc.damage);

    /* 10 movement */
    fill(g_tmp, 8, 8, 0xFFFF00FF);
    robo_compose_insert(&sc, 8, 1, 40, 2, 8, 8, 8, g_tmp, 255);
    compose_all(&sc);
    robo_damage_clear(&sc.damage);
    {
        RoboComposeLayer *l = robo_compose_find(&sc, 8);
        robo_damage_add(&sc.damage, l->x, l->y, l->w, l->h);
        l->x = 52;
        l->y = 2;
        robo_damage_add(&sc.damage, l->x, l->y, l->w, l->h);
        compose_all(&sc);
        if (expect_px(&sc, 40, 2, bg, "old pos") &&
            expect_px(&sc, 52, 2, 0xFFFF00FF, "new pos"))
            pass("movement");
    }
    robo_compose_remove(&sc, 8);
    compose_all(&sc);
    robo_damage_clear(&sc.damage);

    /* 11 resize */
    fill(g_tmp, 16, 16, 0xFF808080);
    robo_compose_insert(&sc, 10, 1, 2, 2, 8, 8, 16, g_tmp, 255);
    compose_all(&sc);
    {
        RoboComposeLayer *l = robo_compose_find(&sc, 10);
        robo_damage_add(&sc.damage, l->x, l->y, l->w, l->h);
        l->w = 16;
        l->h = 16;
        robo_damage_add(&sc.damage, l->x, l->y, l->w, l->h);
        compose_all(&sc);
        if (expect_px(&sc, 17, 17, 0xFF808080, "resized"))
            pass("resize");
    }
    robo_compose_remove(&sc, 10);
    compose_all(&sc);
    robo_damage_clear(&sc.damage);

    /* 12 hide */
    fill(g_tmp, 6, 6, 0xFF112233);
    robo_compose_insert(&sc, 11, 1, 1, 36, 6, 6, 6, g_tmp, 255);
    compose_all(&sc);
    {
        RoboComposeLayer *l = robo_compose_find(&sc, 11);
        l->visible = 0;
        robo_damage_add(&sc.damage, l->x, l->y, l->w, l->h);
        compose_all(&sc);
        if (expect_px(&sc, 1, 36, bg, "hidden"))
            pass("hide");
    }
    robo_compose_remove(&sc, 11);
    robo_damage_clear(&sc.damage);

    /* 13 destroy */
    fill(g_tmp, 6, 6, 0xFF445566);
    robo_compose_insert(&sc, 12, 1, 30, 2, 6, 6, 6, g_tmp, 255);
    compose_all(&sc);
    robo_compose_remove(&sc, 12);
    compose_all(&sc);
    if (expect_px(&sc, 30, 2, bg, "destroyed"))
        pass("destroy");
    robo_damage_clear(&sc.damage);

    /* 14 z-order change */
    fill(g_a, 10, 10, 0xFFAA0000);
    fill(g_b, 10, 10, 0xFF00AA00);
    robo_compose_insert(&sc, 20, 1, 40, 20, 10, 10, 10, g_a, 255);
    robo_compose_insert(&sc, 21, 1, 44, 24, 10, 10, 10, g_b, 255);
    compose_all(&sc);
    expect_px(&sc, 45, 25, 0xFF00AA00, "B on top");
    robo_compose_raise(&sc, 20);
    compose_all(&sc);
    if (expect_px(&sc, 45, 25, 0xFFAA0000, "A raised"))
        pass("z-order change");
    robo_compose_remove(&sc, 20);
    robo_compose_remove(&sc, 21);
    compose_all(&sc);
    robo_damage_clear(&sc.damage);

    /* 15 damage tracking */
    robo_damage_clear(&d);
    if (!robo_damage_is_empty(&d))
        fail("damage empty");
    robo_damage_add(&d, 4, 4, 8, 8);
    if (!robo_damage_contains(&d, 5, 5) || robo_damage_contains(&d, 0, 0))
        fail("damage contains");
    robo_damage_add(&d, 20, 20, 4, 4);
    if (d.w < 20)
        fail("damage merge");
    {
        uint32_t marker = robo_compose_sample(&sc, 0, 40);
        unsigned blits;
        fill(g_tmp, 4, 4, 0xFF010101);
        robo_compose_insert(&sc, 30, 1, 40, 40, 4, 4, 4, g_tmp, 255);
        blits = sc.last_blits;
        compose_all(&sc);
        if (robo_compose_sample(&sc, 0, 40) != marker)
            fail("damage isolation");
        else if (sc.last_blits == 0 && blits == 0)
            fail("damage blits");
        else
            pass("damage tracking");
        robo_compose_remove(&sc, 30);
        compose_all(&sc);
    }

    /* 16 stress */
    {
        uint32_t tmp[4];
        int ok = 1;
        fill(tmp, 2, 2, 0xFF333333);
        for (i = 0; i < 48; i++) {
            if (robo_compose_insert(&sc, 100 + i, 1,
                                    (int32_t)((i % 16) * 3),
                                    (int32_t)((i / 16) * 3),
                                    2, 2, 2, tmp, 255) != 0) {
                ok = 0;
                break;
            }
        }
        compose_all(&sc);
        for (i = 0; i < 48; i++)
            robo_compose_remove(&sc, 100 + i);
        compose_all(&sc);
        if (ok && robo_compose_insert(&sc, 200, 1, 0, 0, 2, 2, 2, tmp, 255) == 0)
            pass("stress");
        else
            fail("stress");
        robo_compose_remove(&sc, 200);
    }

    /* blend unit vectors */
    {
        uint32_t black = px(0xFF000000);
        uint32_t white = px(0xFFFFFFFF);
        uint32_t none = robo_compose_blend(0, black, white, 0);
        uint32_t full = robo_compose_blend(0, black, white, 255);
        uint32_t skip = robo_compose_blend(0, black, px(0x00FFFFFF), 255);
        if (none == black && full == white && skip == black)
            pass("blend arithmetic");
        else
            fail("blend arithmetic");
    }

    printf("========================================\n");
    if (!g_fail) {
        printf("[PASS] COMPOSITOR CPU\n");
        printf("========================================\n");
        return 0;
    }
    printf("[FAIL] COMPOSITOR CPU\n");
    printf("========================================\n");
    return 1;
}
