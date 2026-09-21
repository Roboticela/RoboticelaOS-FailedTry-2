/*
 * Headless CPU compositor tests. Linked on the host against compose.c.
 * Pixel values are packed BGRX (pixfmt 0) with A in the high byte.
 */
#include <stdio.h>
#include <stdint.h>
#include <robocompositor/compose.h>

static int gFail;
static uint32_t gBack[64 * 48];
static uint32_t gFront[64 * 48];
static uint32_t gA[32 * 32];
static uint32_t gB[32 * 32];
static uint32_t gC[32 * 32];
static uint32_t gTmp[32 * 32];

static void fail(const char *s)
{
    printf("[FAIL] %s\n", s);
    gFail = 1;
}

static void pass(const char *s)
{
    printf("[PASS] %s\n", s);
}

static uint32_t px(uint32_t argb)
{
    return ComposePack(RoboComposePixBgrx, argb);
}

static void fill(uint32_t *buf, uint32_t w, uint32_t h, uint32_t argb)
{
    uint32_t i, n = w * h, p = px(argb);
    for (i = 0; i < n; i++)
        buf[i] = p;
}

static int ExpectPx(RoboComposeScene *s, int x, int y, uint32_t argb,
                     const char *what)
{
    uint32_t got = ComposeSample(s, x, y);
    uint32_t want = px(argb);
    if (got != want) {
        printf("[FAIL] %s at %d,%d got %08x want %08x\n",
               what, x, y, got, want);
        gFail = 1;
        return 0;
    }
    return 1;
}

static void ComposeAll(RoboComposeScene *s)
{
    ComposeFrame(s);
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

    if (!ComposeInit(&sc, gBack, gFront, 64, 48, 64,
                           RoboComposePixBgrx)) {
        fail("init");
        return 1;
    }
    ComposeAll(&sc);
    DamageClear(&sc.damage);

    /* 1 single window */
    fill(gA, 16, 12, 0xFFFF0000);
    ComposeInsert(&sc, 1, 1, 4, 4, 16, 12, 16, gA, 255);
    ComposeAll(&sc);
    if (ExpectPx(&sc, 0, 0, bg, "bg") &&
        ExpectPx(&sc, 4, 4, 0xFFFF0000, "win A") &&
        ExpectPx(&sc, 19, 15, 0xFFFF0000, "win A br"))
        pass("single window");
    DamageClear(&sc.damage);

    /* 2/3 multiple + occlusion */
    fill(gB, 16, 12, 0xFF00FF00);
    fill(gC, 16, 12, 0xFF0000FF);
    ComposeInsert(&sc, 2, 1, 12, 8, 16, 12, 16, gB, 255);
    ComposeInsert(&sc, 3, 1, 20, 12, 16, 12, 16, gC, 255);
    ComposeAll(&sc);
    if (ExpectPx(&sc, 5, 5, 0xFFFF0000, "A exposed") &&
        ExpectPx(&sc, 14, 10, 0xFF00FF00, "B over A") &&
        ExpectPx(&sc, 22, 14, 0xFF0000FF, "C over B"))
        pass("z-order occlusion");
    DamageClear(&sc.damage);

    /* 4 transparency 50% white over black patch */
    {
        RoboComposeScene t;
        uint32_t tb[32 * 32], tf[32 * 32], src[8 * 8];
        uint32_t want;
        ComposeInit(&t, tb, tf, 32, 32, 32, RoboComposePixBgrx);
        t.bgArgb = 0xFF000000;
        DamageAdd(&t.damage, 0, 0, 32, 32);
        ComposeAll(&t);
        DamageClear(&t.damage);
        fill(src, 8, 8, 0x80FFFFFF);
        ComposeInsert(&t, 9, 1, 2, 2, 8, 8, 8, src, 255);
        ComposeAll(&t);
        want = ComposeBlend(RoboComposePixBgrx,
                                  ComposePack(RoboComposePixBgrx,
                                                    0xFF000000),
                                  ComposePack(RoboComposePixBgrx,
                                                    0x80FFFFFF),
                                  255);
        if (ComposeSample(&t, 4, 4) == want)
            pass("transparency");
        else
            fail("transparency");
    }

    /* 5 fully transparent */
    {
        uint32_t before = ComposeSample(&sc, 1, 1);
        fill(gTmp, 4, 4, 0x00FF00FF);
        ComposeInsert(&sc, 4, 1, 1, 1, 4, 4, 4, gTmp, 255);
        ComposeAll(&sc);
        if (ComposeSample(&sc, 1, 1) == before)
            pass("alpha 0");
        else
            fail("alpha 0");
        ComposeRemove(&sc, 4);
        DamageClear(&sc.damage);
    }

    /* 6 fully opaque */
    fill(gTmp, 4, 4, 0xFFFFFFFF);
    ComposeInsert(&sc, 5, 1, 0, 0, 4, 4, 4, gTmp, 255);
    ComposeAll(&sc);
    if (ExpectPx(&sc, 0, 0, 0xFFFFFFFF, "opaque"))
        pass("alpha 255");
    ComposeRemove(&sc, 5);
    ComposeAll(&sc);
    DamageClear(&sc.damage);

    /* 7 global opacity */
    {
        RoboComposeScene t;
        uint32_t tb[16 * 16], tf[16 * 16], src[4 * 4];
        uint32_t want;
        ComposeInit(&t, tb, tf, 16, 16, 16, RoboComposePixBgrx);
        t.bgArgb = 0xFF000000;
        DamageAdd(&t.damage, 0, 0, 16, 16);
        ComposeAll(&t);
        fill(src, 4, 4, 0xFFFFFFFF);
        ComposeInsert(&t, 1, 1, 0, 0, 4, 4, 4, src, 128);
        ComposeAll(&t);
        want = ComposeBlend(RoboComposePixBgrx,
                                  ComposePack(RoboComposePixBgrx,
                                                    0xFF000000),
                                  ComposePack(RoboComposePixBgrx,
                                                    0xFFFFFFFF),
                                  128);
        if (ComposeSample(&t, 1, 1) == want)
            pass("global opacity");
        else
            fail("global opacity");
    }

    /* 8 negative position */
    fill(gTmp, 8, 8, 0xFFFFFF00);
    ComposeInsert(&sc, 6, 1, -4, -4, 8, 8, 8, gTmp, 255);
    ComposeAll(&sc);
    if (ExpectPx(&sc, 0, 0, 0xFFFFFF00, "neg clip") &&
        ExpectPx(&sc, 3, 3, 0xFFFFFF00, "neg inside"))
        pass("negative position");
    ComposeRemove(&sc, 6);
    ComposeAll(&sc);
    DamageClear(&sc.damage);

    /* 9 oversized */
    fill(gTmp, 32, 32, 0xFF00FFFF);
    ComposeInsert(&sc, 7, 1, 50, 30, 32, 32, 32, gTmp, 255);
    ComposeAll(&sc);
    if (ExpectPx(&sc, 63, 47, 0xFF00FFFF, "oversize clip") &&
        ComposeSample(&sc, 64, 0) == 0)
        pass("oversized window");
    ComposeRemove(&sc, 7);
    ComposeAll(&sc);
    DamageClear(&sc.damage);

    /* 10 movement */
    fill(gTmp, 8, 8, 0xFFFF00FF);
    ComposeInsert(&sc, 8, 1, 40, 2, 8, 8, 8, gTmp, 255);
    ComposeAll(&sc);
    DamageClear(&sc.damage);
    {
        RoboComposeLayer *l = ComposeFind(&sc, 8);
        DamageAdd(&sc.damage, l->x, l->y, l->w, l->h);
        l->x = 52;
        l->y = 2;
        DamageAdd(&sc.damage, l->x, l->y, l->w, l->h);
        ComposeAll(&sc);
        if (ExpectPx(&sc, 40, 2, bg, "old pos") &&
            ExpectPx(&sc, 52, 2, 0xFFFF00FF, "new pos"))
            pass("movement");
    }
    ComposeRemove(&sc, 8);
    ComposeAll(&sc);
    DamageClear(&sc.damage);

    /* 11 resize */
    fill(gTmp, 16, 16, 0xFF808080);
    ComposeInsert(&sc, 10, 1, 2, 2, 8, 8, 16, gTmp, 255);
    ComposeAll(&sc);
    {
        RoboComposeLayer *l = ComposeFind(&sc, 10);
        DamageAdd(&sc.damage, l->x, l->y, l->w, l->h);
        l->w = 16;
        l->h = 16;
        DamageAdd(&sc.damage, l->x, l->y, l->w, l->h);
        ComposeAll(&sc);
        if (ExpectPx(&sc, 17, 17, 0xFF808080, "resized"))
            pass("resize");
    }
    ComposeRemove(&sc, 10);
    ComposeAll(&sc);
    DamageClear(&sc.damage);

    /* 12 hide */
    fill(gTmp, 6, 6, 0xFF112233);
    ComposeInsert(&sc, 11, 1, 1, 36, 6, 6, 6, gTmp, 255);
    ComposeAll(&sc);
    {
        RoboComposeLayer *l = ComposeFind(&sc, 11);
        l->visible = 0;
        DamageAdd(&sc.damage, l->x, l->y, l->w, l->h);
        ComposeAll(&sc);
        if (ExpectPx(&sc, 1, 36, bg, "hidden"))
            pass("hide");
    }
    ComposeRemove(&sc, 11);
    DamageClear(&sc.damage);

    /* 13 destroy */
    fill(gTmp, 6, 6, 0xFF445566);
    ComposeInsert(&sc, 12, 1, 30, 2, 6, 6, 6, gTmp, 255);
    ComposeAll(&sc);
    ComposeRemove(&sc, 12);
    ComposeAll(&sc);
    if (ExpectPx(&sc, 30, 2, bg, "destroyed"))
        pass("destroy");
    DamageClear(&sc.damage);

    /* 14 z-order change */
    fill(gA, 10, 10, 0xFFAA0000);
    fill(gB, 10, 10, 0xFF00AA00);
    ComposeInsert(&sc, 20, 1, 40, 20, 10, 10, 10, gA, 255);
    ComposeInsert(&sc, 21, 1, 44, 24, 10, 10, 10, gB, 255);
    ComposeAll(&sc);
    ExpectPx(&sc, 45, 25, 0xFF00AA00, "B on top");
    ComposeRaise(&sc, 20);
    ComposeAll(&sc);
    if (ExpectPx(&sc, 45, 25, 0xFFAA0000, "A raised"))
        pass("z-order change");
    ComposeRemove(&sc, 20);
    ComposeRemove(&sc, 21);
    ComposeAll(&sc);
    DamageClear(&sc.damage);

    /* 15 damage tracking */
    DamageClear(&d);
    if (!DamageIsEmpty(&d))
        fail("damage empty");
    DamageAdd(&d, 4, 4, 8, 8);
    if (!DamageContains(&d, 5, 5) || DamageContains(&d, 0, 0))
        fail("damage contains");
    DamageAdd(&d, 20, 20, 4, 4);
    if (d.w < 20)
        fail("damage merge");
    {
        uint32_t marker = ComposeSample(&sc, 0, 40);
        unsigned blits;
        fill(gTmp, 4, 4, 0xFF010101);
        ComposeInsert(&sc, 30, 1, 40, 40, 4, 4, 4, gTmp, 255);
        blits = sc.lastBlits;
        ComposeAll(&sc);
        if (ComposeSample(&sc, 0, 40) != marker)
            fail("damage isolation");
        else if (sc.lastBlits == 0 && blits == 0)
            fail("damage blits");
        else
            pass("damage tracking");
        ComposeRemove(&sc, 30);
        ComposeAll(&sc);
    }

    /* 16 stress */
    {
        uint32_t tmp[4];
        int ok = 1;
        fill(tmp, 2, 2, 0xFF333333);
        for (i = 0; i < 48; i++) {
            if (ComposeInsert(&sc, 100 + i, 1,
                                    (int32_t)((i % 16) * 3),
                                    (int32_t)((i / 16) * 3),
                                    2, 2, 2, tmp, 255) != 0) {
                ok = 0;
                break;
            }
        }
        ComposeAll(&sc);
        for (i = 0; i < 48; i++)
            ComposeRemove(&sc, 100 + i);
        ComposeAll(&sc);
        if (ok && ComposeInsert(&sc, 200, 1, 0, 0, 2, 2, 2, tmp, 255) == 0)
            pass("stress");
        else
            fail("stress");
        ComposeRemove(&sc, 200);
    }

    /* blend unit vectors */
    {
        uint32_t black = px(0xFF000000);
        uint32_t white = px(0xFFFFFFFF);
        uint32_t none = ComposeBlend(0, black, white, 0);
        uint32_t full = ComposeBlend(0, black, white, 255);
        uint32_t skip = ComposeBlend(0, black, px(0x00FFFFFF), 255);
        if (none == black && full == white && skip == black)
            pass("blend arithmetic");
        else
            fail("blend arithmetic");
    }

    printf("========================================\n");
    if (!gFail) {
        printf("[PASS] COMPOSITOR CPU\n");
        printf("========================================\n");
        return 0;
    }
    printf("[FAIL] COMPOSITOR CPU\n");
    printf("========================================\n");
    return 1;
}
