/*
 * SuiteRoboWindowPresent — Goal 7.4 framebuffer presentation.
 *
 * Mock present backend copies pixels for integrity checks only.
 * It does not create or paint a real OS window.
 */

#include "RoboGraphics/RoboGraphicsBitmap.h"
#include "RoboGraphics/RoboGraphicsFramebuffer.h"
#include "RoboGraphics/RoboGraphicsSurface.h"
#include "RoboTest.h"
#include "RoboWindow/RoboWindow.h"

#include <string.h>

#define PRESENT_COPY_MAX 65536u

typedef struct presentMock {
    int initCount;
    int shutdownCount;
    int presentCount;
    int failPresent;
    int lastWidth;
    int lastHeight;
    int lastStride;
    GfxPixelFormat lastFormat;
    WindowId lastWindow;
    size_t lastCopyBytes;
    uint8_t lastPixels[PRESENT_COPY_MAX];
} PresentMock;

static PresentMock gPm;
static WindowBackendCaps gPmCaps;
static WindowBackendVtbl gPmVtbl;

static Result PmInit(void *ud)
{
    PresentMock *m = (PresentMock *)ud;
    if (!m) {
        return RoboInvalidArgument;
    }
    m->initCount++;
    return RoboOk;
}

static void PmShutdown(void *ud)
{
    PresentMock *m = (PresentMock *)ud;
    if (m) {
        m->shutdownCount++;
    }
}

static Result PmCreate(void *ud,
                               WindowId id,
                               const WindowConfig *cfg,
                               WindowNativeHandle *outNative)
{
    (void)ud;
    (void)cfg;
    return WindowNativeHandleAssign(outNative, (uintptr_t)id, 1u);
}

static Result PmDestroy(void *ud,
                                WindowId id,
                                WindowNativeHandle *native)
{
    (void)ud;
    (void)id;
    WindowNativeHandleInvalidate(native);
    return RoboOk;
}

static Result PmShow(void *ud,
                             WindowId id,
                             WindowNativeHandle *native)
{
    (void)ud;
    (void)id;
    (void)native;
    return RoboOk;
}

static Result PmHide(void *ud,
                             WindowId id,
                             WindowNativeHandle *native)
{
    (void)ud;
    (void)id;
    (void)native;
    return RoboOk;
}

static Result PmTitle(void *ud,
                              WindowId id,
                              WindowNativeHandle *native,
                              const char *utf8Title)
{
    (void)ud;
    (void)id;
    (void)native;
    (void)utf8Title;
    return RoboOk;
}

static Result PmSize(void *ud,
                             WindowId id,
                             WindowNativeHandle *native,
                             int32_t width,
                             int32_t height)
{
    (void)ud;
    (void)id;
    (void)native;
    (void)width;
    (void)height;
    return RoboOk;
}

static Result PmPos(void *ud,
                            WindowId id,
                            WindowNativeHandle *native,
                            int32_t x,
                            int32_t y)
{
    (void)ud;
    (void)id;
    (void)native;
    (void)x;
    (void)y;
    return RoboOk;
}

static Result PmFocus(void *ud,
                              WindowId id,
                              WindowNativeHandle *native)
{
    (void)ud;
    (void)id;
    (void)native;
    return RoboOk;
}

static Result PmPresent(void *ud,
                                WindowId id,
                                WindowNativeHandle *native,
                                GfxSurface *surface)
{
    PresentMock *m = (PresentMock *)ud;
    const uint8_t *src;
    size_t nbytes;

    (void)native;
    if (!m || !surface || !native) {
        return RoboInvalidArgument;
    }
    if (m->failPresent) {
        return RoboIoError;
    }
    if (!WindowNativeHandleIsValid(native)) {
        return RoboInvalidArgument;
    }

    src = GfxSurfacePixelsConst(surface);
    if (!src) {
        return RoboInvalidArgument;
    }
    nbytes = GfxSurfaceByteSize(surface);
    m->presentCount++;
    m->lastWindow = id;
    m->lastWidth = (int)GfxSurfaceWidth(surface);
    m->lastHeight = (int)GfxSurfaceHeight(surface);
    m->lastStride = (int)GfxSurfaceStride(surface);
    m->lastFormat = GfxSurfaceFormat(surface);
    m->lastCopyBytes = nbytes > PRESENT_COPY_MAX ? PRESENT_COPY_MAX : nbytes;
    memcpy(m->lastPixels, src, m->lastCopyBytes);
    return RoboOk;
}

static Result PmEvents(void *ud)
{
    (void)ud;
    return RoboOk;
}

static void PresentCapsDefault(void)
{
    memset(&gPmCaps, 0, sizeof(gPmCaps));
    gPmCaps.RoboCreateWindow = 1;
    gPmCaps.RoboDestroyWindow = 1;
    gPmCaps.showHide = 1;
    gPmCaps.resize = 1;
    gPmCaps.move = 1;
    gPmCaps.SetTitle = 1;
    gPmCaps.focus = 1;
    gPmCaps.present = 1;
    gPmCaps.ProcessEvents = 1;
    gPmCaps.presentFormatMask = RoboWindowPresentFmtCpuDefault;
    gPmCaps.presentMaxWidth = 0u;
    gPmCaps.presentMaxHeight = 0u;
}

static void PresentVtblBind(void)
{
    memset(&gPmVtbl, 0, sizeof(gPmVtbl));
    gPmVtbl.name = "present_mock";
    gPmVtbl.capabilities = &gPmCaps;
    gPmVtbl.initialize = PmInit;
    gPmVtbl.shutdown = PmShutdown;
    gPmVtbl.RoboCreateWindow = PmCreate;
    gPmVtbl.RoboDestroyWindow = PmDestroy;
    gPmVtbl.show = PmShow;
    gPmVtbl.hide = PmHide;
    gPmVtbl.SetTitle = PmTitle;
    gPmVtbl.SetSize = PmSize;
    gPmVtbl.SetPosition = PmPos;
    gPmVtbl.focus = PmFocus;
    gPmVtbl.PresentSurface = PmPresent;
    gPmVtbl.ProcessEvents = PmEvents;
}

static void fixture(void)
{
    memset(&gPm, 0, sizeof(gPm));
    PresentCapsDefault();
    PresentVtblBind();
    WindowShutdown();
    RoboCheckEqInt(WindowInit(), RoboOk);
    RoboCheckEqInt(WindowBackendRegister(&gPmVtbl, &gPm), RoboOk);
}

static void FixtureEnd(void)
{
    WindowShutdown();
}

static Result MakeSurface(uint32_t w,
                                  uint32_t h,
                                  GfxPixelFormat fmt,
                                  size_t stride,
                                  GfxSurface **out)
{
    GfxSurfaceDesc desc;

    GfxSurfaceDescInit(&desc);
    desc.width = w;
    desc.height = h;
    desc.format = fmt;
    desc.strideBytes = stride;
    return GfxSurfaceCreate(&desc, out);
}

static void FillSolid(GfxSurface *s, GfxColorRgba8 c)
{
    RoboCheckEqInt(GfxSurfaceClearRgba8(s, c), RoboOk);
}

static void FillChecker(GfxSurface *s)
{
    uint32_t x;
    uint32_t y;
    uint32_t w = GfxSurfaceWidth(s);
    uint32_t h = GfxSurfaceHeight(s);

    for (y = 0u; y < h; y++) {
        for (x = 0u; x < w; x++) {
            GfxColorRgba8 c =
                ((x + y) & 1u) ? GfxRgba8(255, 255, 255, 255)
                               : GfxRgba8(0, 0, 0, 255);
            RoboCheckEqInt(GfxSurfaceSetPixel(s, x, y, c), RoboOk);
        }
    }
}

static void FillGradient(GfxSurface *s)
{
    uint32_t x;
    uint32_t y;
    uint32_t w = GfxSurfaceWidth(s);
    uint32_t h = GfxSurfaceHeight(s);

    for (y = 0u; y < h; y++) {
        for (x = 0u; x < w; x++) {
            uint8_t v = (uint8_t)((x * 255u) / (w ? w : 1u));
            RoboCheckEqInt(
                GfxSurfaceSetPixel(s, x, y, GfxRgba8(v, 0, 0, 255)),
                RoboOk);
        }
    }
}

static void TestPresentBasic(void)
{
    WindowConfig cfg;
    WindowId id;
    GfxSurface *surf = NULL;
    WindowBackendCaps caps;

    fixture();
    RoboCheckEqInt(WindowBackendCapabilities(&caps), RoboOk);
    RoboCheck(caps.present);
    RoboCheck(WindowPresentFormatSupported(RoboGfxFormatRgba8));
    RoboCheck(!WindowPresentFormatSupported(RoboGfxFormatAlpha8));

    WindowConfigInit(&cfg);
    cfg.visible = 1;
    RoboCheckEqInt(WindowCreate(&cfg, &id), RoboOk);
    RoboCheckEqInt(MakeSurface(4, 4, RoboGfxFormatRgba8, 0u, &surf),
                      RoboOk);
    FillSolid(surf, GfxRgba8(10, 20, 30, 255));
    RoboCheckEqInt(WindowPresent(id, surf), RoboOk);
    RoboCheckEqInt(gPm.presentCount, 1);
    RoboCheckEqInt(gPm.lastWidth, 4);
    RoboCheckEqInt(gPm.lastHeight, 4);
    RoboCheckEqInt((int)gPm.lastFormat, (int)RoboGfxFormatRgba8);
    RoboCheckEqInt(gPm.lastPixels[0], 10);
    RoboCheckEqInt(gPm.lastPixels[1], 20);
    RoboCheckEqInt(gPm.lastPixels[2], 30);

    RoboCheckEqInt(WindowPresent(id, surf), RoboOk);
    RoboCheckEqInt(gPm.presentCount, 2);

    {
        GfxSurface *big = NULL;
        RoboCheckEqInt(MakeSurface(8, 2, RoboGfxFormatBgra8, 0u, &big),
                          RoboOk);
        FillSolid(big, GfxRgba8(1, 2, 3, 4));
        RoboCheckEqInt(WindowPresent(id, big), RoboOk);
        RoboCheckEqInt(gPm.lastWidth, 8);
        RoboCheckEqInt((int)gPm.lastFormat, (int)RoboGfxFormatBgra8);
        GfxSurfaceRelease(big);
    }

    GfxSurfaceRelease(surf);
    RoboCheckEqInt(WindowDestroy(id), RoboOk);
    FixtureEnd();
}

static void TestPresentValidation(void)
{
    WindowConfig cfg;
    WindowId id;
    GfxSurface *surf = NULL;
    GfxSurface *alpha = NULL;

    fixture();
    WindowConfigInit(&cfg);
    cfg.visible = 0;
    RoboCheckEqInt(WindowCreate(&cfg, &id), RoboOk);
    RoboCheckEqInt(WindowPresent(id, NULL), RoboInvalidArgument);
    RoboCheckEqInt(WindowPresent((WindowId)999u, NULL),
                      RoboNotFound);

    RoboCheckEqInt(MakeSurface(2, 2, RoboGfxFormatRgba8, 0u, &surf),
                      RoboOk);
    RoboCheckEqInt(MakeSurface(2, 2, RoboGfxFormatAlpha8, 0u, &alpha),
                      RoboOk);
    RoboCheckEqInt(WindowPresent(id, alpha), RoboNotSupported);

    gPmCaps.presentMaxWidth = 1u;
    RoboCheckEqInt(WindowPresent(id, surf), RoboNotSupported);
    gPmCaps.presentMaxWidth = 0u;

    RoboCheckEqInt(WindowSetPresentSurface(id, surf), RoboOk);
    GfxSurfaceRelease(surf);
    surf = NULL;
    /* Association cleared after destroy of borrowed pointer: clear explicitly. */
    RoboCheckEqInt(WindowSetPresentSurface(id, NULL), RoboOk);
    RoboCheckEqInt(WindowPresent(id, NULL), RoboInvalidArgument);

    RoboCheckEqInt(WindowDestroy(id), RoboOk);
    RoboCheckEqInt(MakeSurface(1, 1, RoboGfxFormatRgba8, 0u, &surf),
                      RoboOk);
    RoboCheckEqInt(WindowPresent(id, surf), RoboNotFound);
    GfxSurfaceRelease(surf);
    GfxSurfaceRelease(alpha);

    WindowBackendClear();
    RoboCheckEqInt(WindowCreate(&cfg, &id), RoboNotSupported);
    FixtureEnd();
}

static void TestPresentFrameData(void)
{
    WindowConfig cfg;
    WindowId id;
    GfxSurface *surf = NULL;
    uint8_t padded[64];
    GfxSurface *strided = NULL;
    GfxSurfaceDesc desc;

    fixture();
    WindowConfigInit(&cfg);
    RoboCheckEqInt(WindowCreate(&cfg, &id), RoboOk);

    /* Single pixel */
    RoboCheckEqInt(MakeSurface(1, 1, RoboGfxFormatRgba8, 0u, &surf),
                      RoboOk);
    FillSolid(surf, GfxRgba8(9, 8, 7, 6));
    RoboCheckEqInt(WindowPresent(id, surf), RoboOk);
    RoboCheckEqInt(gPm.lastPixels[0], 9);
    RoboCheckEqInt(gPm.lastPixels[3], 6);
    GfxSurfaceRelease(surf);

    /* Solid + checker + gradient */
    RoboCheckEqInt(MakeSurface(4, 4, RoboGfxFormatRgba8, 0u, &surf),
                      RoboOk);
    FillSolid(surf, GfxRgba8(100, 0, 0, 255));
    RoboCheckEqInt(WindowPresent(id, surf), RoboOk);
    RoboCheckEqInt(gPm.lastPixels[0], 100);
    FillChecker(surf);
    RoboCheckEqInt(WindowPresent(id, surf), RoboOk);
    RoboCheckEqInt(gPm.lastPixels[0], 0); /* (0,0) black */
    RoboCheckEqInt(gPm.lastPixels[4], 255); /* (1,0) white R */
    FillGradient(surf);
    RoboCheckEqInt(WindowPresent(id, surf), RoboOk);
    RoboCheckEqInt(gPm.lastPixels[0], 0);
    GfxSurfaceRelease(surf);

    /* Stride-sensitive: 2×2 RGB8 with stride 8 (>6) */
    memset(padded, 0xCD, sizeof(padded));
    padded[0] = 1;
    padded[1] = 2;
    padded[2] = 3;
    padded[8] = 4;
    padded[9] = 5;
    padded[10] = 6;
    padded[16] = 7;
    padded[17] = 8;
    padded[18] = 9;
    padded[24] = 10;
    padded[25] = 11;
    padded[26] = 12;
    GfxSurfaceDescInit(&desc);
    desc.width = 2;
    desc.height = 2;
    desc.format = RoboGfxFormatRgb8;
    desc.strideBytes = 8u;
    desc.access = RoboGfxSurfaceAccessRead;
    RoboCheckEqInt(GfxSurfaceCreateBorrowed(&desc, padded, &strided),
                      RoboOk);
    RoboCheckEqInt(WindowPresent(id, strided), RoboOk);
    RoboCheckEqInt(gPm.lastStride, 8);
    RoboCheckEqInt(gPm.lastPixels[0], 1);
    RoboCheckEqInt(gPm.lastPixels[8], 4);
    RoboCheckEqInt(gPm.lastPixels[10], 6);
    GfxSurfaceRelease(strided);

    RoboCheckEqInt(WindowDestroy(id), RoboOk);
    FixtureEnd();
}

static void TestPresentFramebufferApi(void)
{
    WindowConfig cfg;
    WindowId id;
    GfxFramebufferDesc fbDesc;
    GfxFramebuffer *fb = NULL;
    uint8_t *row = NULL;

    fixture();
    WindowConfigInit(&cfg);
    RoboCheckEqInt(WindowCreate(&cfg, &id), RoboOk);
    GfxFramebufferDescInit(&fbDesc);
    fbDesc.width = 3;
    fbDesc.height = 1;
    fbDesc.format = RoboGfxFormatRgba8;
    RoboCheckEqInt(GfxFramebufferCreate(&fbDesc, &fb), RoboOk);
    RoboCheckEqInt(GfxFramebufferRow(fb, 0u, &row), RoboOk);
    row[0] = 11;
    row[1] = 22;
    row[2] = 33;
    row[3] = 44;
    RoboCheckEqInt(WindowPresentFramebuffer(id, fb), RoboOk);
    RoboCheckEqInt(gPm.presentCount, 1);
    RoboCheckEqInt(gPm.lastWidth, 3);
    RoboCheckEqInt(gPm.lastPixels[0], 11);
    RoboCheckEqInt(gPm.lastPixels[3], 44);
    RoboCheckEqInt(WindowPresentFramebuffer(id, NULL),
                      RoboInvalidArgument);
    GfxFramebufferRelease(fb);
    RoboCheckEqInt(WindowDestroy(id), RoboOk);
    FixtureEnd();
}

static void TestPresentBackend(void)
{
    WindowConfig cfg;
    WindowId id;
    GfxSurface *surf = NULL;
    WindowBackendCaps caps;

    fixture();
    RoboCheckEqInt(gPm.initCount, 1);
    RoboCheck(strcmp(WindowBackendName(), "present_mock") == 0);
    RoboCheckEqInt(WindowBackendCapabilities(&caps), RoboOk);
    RoboCheck(caps.presentFormatMask & RoboWindowPresentFmtRgba8);

    WindowConfigInit(&cfg);
    RoboCheckEqInt(WindowCreate(&cfg, &id), RoboOk);
    RoboCheckEqInt(MakeSurface(2, 2, RoboGfxFormatRgba8, 0u, &surf),
                      RoboOk);
    gPm.failPresent = 1;
    RoboCheckEqInt(WindowPresent(id, surf), RoboIoError);
    gPm.failPresent = 0;
    RoboCheckEqInt(WindowPresent(id, surf), RoboOk);

    /* Unsupported when present capability cleared */
    gPmCaps.present = 0;
    RoboCheckEqInt(WindowPresent(id, surf), RoboNotSupported);
    gPmCaps.present = 1;

    GfxSurfaceRelease(surf);
    RoboCheckEqInt(WindowDestroy(id), RoboOk);
    FixtureEnd();
    RoboCheck(gPm.shutdownCount >= 1);
}

static void TestPresentLifecycle(void)
{
    WindowConfig cfg;
    WindowId id;
    GfxSurface *surf = NULL;

    fixture();
    WindowConfigInit(&cfg);
    cfg.visible = 0;
    RoboCheckEqInt(WindowCreate(&cfg, &id), RoboOk);
    RoboCheckEqInt(MakeSurface(2, 2, RoboGfxFormatRgba8, 0u, &surf),
                      RoboOk);
    FillSolid(surf, GfxRgba8(5, 5, 5, 255));
    RoboCheckEqInt(WindowPresent(id, surf), RoboOk);

    RoboCheckEqInt(WindowHide(id), RoboOk);
    RoboCheckEqInt(WindowPresent(id, surf), RoboOk);
    RoboCheckEqInt(gPm.presentCount, 2);

    RoboCheckEqInt(WindowDestroy(id), RoboOk);
    RoboCheckEqInt(WindowPresent(id, surf), RoboNotFound);
    RoboCheckEqInt(WindowDestroy(id), RoboNotFound);

    GfxSurfaceRelease(surf);
    FixtureEnd();
}

static void TestPresentThreading(void)
{
    WindowConfig cfg;
    WindowId id;
    GfxSurface *surf = NULL;

    fixture();
    RoboCheckEqInt(WindowThreadBindOwner(1u), RoboOk);
    RoboCheckEqInt(WindowThreadEnter(1u), RoboOk);
    WindowConfigInit(&cfg);
    RoboCheckEqInt(WindowCreate(&cfg, &id), RoboOk);
    RoboCheckEqInt(MakeSurface(1, 1, RoboGfxFormatRgba8, 0u, &surf),
                      RoboOk);
    RoboCheckEqInt(WindowPresent(id, surf), RoboOk);

    RoboCheckEqInt(WindowThreadEnter(2u), RoboOk);
    RoboCheckEqInt(WindowPresent(id, surf), RoboPermissionDenied);
    {
        GfxFramebuffer *fb = GfxSurfaceFramebuffer(surf);
        RoboCheck(fb != NULL);
        RoboCheckEqInt(WindowPresentFramebuffer(id, fb),
                          RoboPermissionDenied);
    }

    RoboCheckEqInt(WindowThreadEnter(1u), RoboOk);
    RoboCheckEqInt(WindowPresent(id, surf), RoboOk);
    RoboCheckEqInt(WindowThreadBindOwner(RoboWindowThreadNone),
                      RoboOk);
    GfxSurfaceRelease(surf);
    RoboCheckEqInt(WindowDestroy(id), RoboOk);
    FixtureEnd();
}

static void TestPresentAssociatedSurface(void)
{
    WindowConfig cfg;
    WindowId id;
    GfxSurface *a = NULL;
    GfxSurface *b = NULL;

    fixture();
    WindowConfigInit(&cfg);
    RoboCheckEqInt(WindowCreate(&cfg, &id), RoboOk);
    RoboCheckEqInt(MakeSurface(2, 1, RoboGfxFormatRgba8, 0u, &a), RoboOk);
    RoboCheckEqInt(MakeSurface(3, 1, RoboGfxFormatRgba8, 0u, &b), RoboOk);
    FillSolid(a, GfxRgba8(1, 0, 0, 255));
    FillSolid(b, GfxRgba8(0, 1, 0, 255));
    RoboCheckEqInt(WindowSetPresentSurface(id, a), RoboOk);
    RoboCheckEqInt(WindowPresent(id, NULL), RoboOk);
    RoboCheckEqInt(gPm.lastWidth, 2);
    RoboCheckEqInt(gPm.lastPixels[0], 1);
    /* Explicit override replaces associated frame for this call. */
    RoboCheckEqInt(WindowPresent(id, b), RoboOk);
    RoboCheckEqInt(gPm.lastWidth, 3);
    RoboCheckEqInt(gPm.lastPixels[1], 1);
    GfxSurfaceRelease(a);
    GfxSurfaceRelease(b);
    RoboCheckEqInt(WindowDestroy(id), RoboOk);
    FixtureEnd();
}

static void TestPresentValidateHelpers(void)
{
    GfxSurface *surf = NULL;
    WindowBackendCaps caps;

    memset(&caps, 0, sizeof(caps));
    caps.present = 1;
    caps.presentFormatMask = RoboWindowPresentFmtRgba8;
    RoboCheckEqInt(WindowPresentValidateSurface(NULL, &caps),
                      RoboInvalidArgument);
    RoboCheckEqInt(MakeSurface(2, 2, RoboGfxFormatRgba8, 0u, &surf),
                      RoboOk);
    RoboCheckEqInt(WindowPresentValidateSurface(surf, &caps), RoboOk);
    caps.presentFormatMask = RoboWindowPresentFmtBgra8;
    RoboCheckEqInt(WindowPresentValidateSurface(surf, &caps),
                      RoboNotSupported);
    GfxSurfaceRelease(surf);
}

void TestRegisterWindowPresent(void)
{
    TestRegister("window.present.basic", TestPresentBasic);
    TestRegister("window.present.validation", TestPresentValidation);
    TestRegister("window.present.frame_data", TestPresentFrameData);
    TestRegister("window.present.framebuffer",
                       TestPresentFramebufferApi);
    TestRegister("window.present.backend", TestPresentBackend);
    TestRegister("window.present.lifecycle", TestPresentLifecycle);
    TestRegister("window.present.threading", TestPresentThreading);
    TestRegister("window.present.associated",
                       TestPresentAssociatedSurface);
    TestRegister("window.present.validate", TestPresentValidateHelpers);
}
