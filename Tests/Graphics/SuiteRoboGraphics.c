/*
 * RoboGraphics foundation suite (Goal 6.1).
 * Headless — no desktop / window required.
 */

#include "RoboGraphics/RoboGraphics.h"
#include "RoboMem/RoboMemDebug.h"
#include "RoboTest.h"

#include <string.h>

static void TestGfxVersion(void)
{
    /* 0.17.0 is additive in the same sense 0.16.0 was: the decoder landed without
       touching a single frozen raster/bitmap entry point, and 0.17.0 adds the scaled
       image draw (`GfxRasterDrawImage`) the paint layer needs -- one new
       declaration, no changed meaning -- so the freeze below still holds. */
    RoboCheckStrEq(GraphicsVersionString(), "0.17.0");
    RoboCheckEqInt(RoboGraphicsApiFrozen, 1);
    RoboCheckEqInt(GraphicsInit(), RoboOk);
    RoboCheckEqInt(GraphicsInit(), RoboOk);
    GraphicsShutdown();
}

static void TestGfxGeometry(void)
{
    GfxPoint p;
    GfxSize s;
    GfxRect r;
    GfxPointF pf;
    GfxAffine a, b, c;

    p = MakeGfxPoint(3, 4);
    RoboCheckEqInt(p.x, 3);
    RoboCheckEqInt(p.y, 4);
    s = MakeGfxSize(10, 20);
    RoboCheck(!GfxSizeIsEmpty(s));
    RoboCheck(GfxSizeIsValid(s));
    RoboCheck(GfxSizeIsEmpty(MakeGfxSize(0, 5)));

    r = MakeGfxRect(10, 20, 30, 40);
    RoboCheckEqInt(GfxRectRight(r), 40);
    RoboCheckEqInt(GfxRectBottom(r), 60);
    RoboCheck(GfxRectContainsPoint(r, MakeGfxPoint(10, 20)));
    RoboCheck(GfxRectContainsPoint(r, MakeGfxPoint(39, 59)));
    RoboCheck(!GfxRectContainsPoint(r, MakeGfxPoint(40, 20))); /* exclusive */
    RoboCheck(!GfxRectContainsPoint(r, MakeGfxPoint(10, 60)));

    pf = MakeGfxPointF(1.6f, -1.6f);
    p = GfxPointFFloor(pf);
    RoboCheckEqInt(p.x, 1);
    RoboCheckEqInt(p.y, -2);
    p = GfxPointFRound(MakeGfxPointF(1.5f, 2.4f));
    RoboCheckEqInt(p.x, 2);
    RoboCheckEqInt(p.y, 2);

    GfxAffineTranslate(&a, 10.0f, 20.0f);
    GfxAffineScale(&b, 2.0f, 3.0f);
    RoboCheckEqInt(GfxAffineMultiply(&a, &b, &c), RoboOk);
    pf = GfxAffineMapPoint(&c, MakeGfxPointF(1.0f, 1.0f));
    RoboCheck(pf.x > 11.9f && pf.x < 12.1f);
    RoboCheck(pf.y > 22.9f && pf.y < 23.1f);
}

static void TestGfxColor(void)
{
    GfxColorRgba8 c8;
    GfxColorRgbaF cf;
    uint32_t packed;

    c8 = GfxRgb8(10, 20, 30);
    RoboCheck(GfxColorRgba8IsOpaque(c8));
    RoboCheck(!GfxColorRgba8IsTransparent(c8));
    RoboCheckEqInt(GfxColorRgba8ToF(c8, &cf), RoboOk);
    RoboCheckEqInt(GfxColorRgbaFTo8(cf, &c8), RoboOk);
    RoboCheckEqInt(c8.r, 10);
    packed = GfxColorRgba8PackArgb(GfxRgba8(1, 2, 3, 4));
    c8 = GfxColorRgba8UnpackArgb(packed);
    RoboCheckEqInt(c8.r, 1);
    RoboCheckEqInt(c8.g, 2);
    RoboCheckEqInt(c8.b, 3);
    RoboCheckEqInt(c8.a, 4);
    RoboCheckEqInt(GfxColorRgba8ToF(c8, NULL), RoboInvalidArgument);
}

static void TestGfxPixelFormat(void)
{
    GfxPixelFormatInfo info;
    size_t stride = 0u;

    RoboCheckEqInt(GetGfxPixelFormatInfo(RoboGfxFormatRgba8, &info), RoboOk);
    RoboCheckEqInt(info.bytesPerPixel, 4);
    RoboCheckEqInt(info.hasAlpha, 1);
    RoboCheckStrEq(info.name, "RGBA8");
    RoboCheck(GfxPixelFormatIsKnown(RoboGfxFormatBgra8));
    RoboCheck(!GfxPixelFormatIsKnown(RoboGfxFormatUnknown));
    RoboCheckEqInt(GfxPixelFormatMinStride(RoboGfxFormatRgba8, 16u, &stride),
                      RoboOk);
    RoboCheckEqInt((int)stride, 64);
    RoboCheckEqInt(GfxPixelFormatMinStride(RoboGfxFormatRgba8, 0u, &stride),
                      RoboInvalidArgument);
}

static void TestGfxSurfaceOwned(void)
{
    GfxSurfaceDesc desc;
    GfxSurface *s = NULL;
    uint8_t *px;

    GfxSurfaceDescInit(&desc);
    desc.width = 4;
    desc.height = 2;
    desc.format = RoboGfxFormatRgba8;
    RoboCheckEqInt(GfxSurfaceCreate(&desc, &s), RoboOk);
    RoboCheck(s != NULL);
    RoboCheckEqInt((int)GfxSurfaceWidth(s), 4);
    RoboCheckEqInt((int)GfxSurfaceHeight(s), 2);
    RoboCheckEqInt((int)MakeGfxSurfaceOwnership(s), (int)RoboGfxSurfaceOwned);
    RoboCheckEqInt((int)GfxSurfaceStride(s), 16);
    RoboCheckEqInt(GfxSurfaceClearRgba8(s, GfxRgba8(1, 2, 3, 255)),
                      RoboOk);
    px = GfxSurfacePixels(s);
    RoboCheck(px != NULL);
    RoboCheckEqInt(px[0], 1);
    RoboCheckEqInt(px[1], 2);
    RoboCheckEqInt(px[2], 3);
    RoboCheckEqInt(px[3], 255);
    GfxSurfaceRetain(s);
    GfxSurfaceRelease(s);
    GfxSurfaceRelease(s);
}

static void TestGfxSurfaceBorrowed(void)
{
    uint8_t buf[16];
    GfxSurfaceDesc desc;
    GfxSurface *s = NULL;

    memset(buf, 0xAB, sizeof(buf));
    GfxSurfaceDescInit(&desc);
    desc.width = 2;
    desc.height = 2;
    desc.format = RoboGfxFormatRgba8;
    desc.strideBytes = 8;
    RoboCheckEqInt(GfxSurfaceCreateBorrowed(&desc, buf, &s), RoboOk);
    RoboCheckEqInt((int)MakeGfxSurfaceOwnership(s), (int)RoboGfxSurfaceBorrowed);
    RoboCheck(GfxSurfacePixels(s) == buf);
    GfxSurfaceRelease(s);
    RoboCheckEqInt(buf[0], 0xAB); /* buffer still caller-owned */
}

static void TestGfxSurfaceErrors(void)
{
    GfxSurfaceDesc desc;
    GfxSurface *s = NULL;

    GfxSurfaceDescInit(&desc);
    desc.width = 0;
    desc.height = 1;
    RoboCheckEqInt(GfxSurfaceCreate(&desc, &s), RoboInvalidArgument);
    desc.width = 1;
    desc.format = RoboGfxFormatRgbaF32; /* architectural only for CPU create */
    RoboCheckEqInt(GfxSurfaceCreate(&desc, &s), RoboNotSupported);
    RoboCheckEqInt(GfxSurfaceCreate(NULL, &s), RoboInvalidArgument);
}

static void TestGfxImageFrame(void)
{
    GfxSurfaceDesc desc;
    GfxSurface *s = NULL;
    GfxImage *img = NULL;
    GfxImageDesc idesc;
    GfxFrame frame;

    GfxSurfaceDescInit(&desc);
    desc.width = 2;
    desc.height = 2;
    RoboCheckEqInt(GfxSurfaceCreate(&desc, &s), RoboOk);
    RoboCheckEqInt(GfxImageCreateFromSurface(s, &img), RoboOk);
    RoboCheckEqInt((int)MakeGfxImageKind(img), (int)RoboGfxImageKindRaster);
    RoboCheck(GfxImageSurface(img) == s);

    GfxFrameInit(&frame);
    RoboCheckEqInt(GfxFrameBegin(&frame, s), RoboOk);
    RoboCheck(frame.id != RoboGfxFrameIdNone);
    RoboCheckEqInt((int)frame.width, 2);
    RoboCheckEqInt(GfxFrameEnd(&frame), RoboOk);
    GfxFrameReset(&frame);
    RoboCheck(frame.target == NULL);

    GfxImageRelease(img);
    GfxSurfaceRelease(s);

    GfxImageDescInit(&idesc);
    idesc.width = 8;
    idesc.height = 8;
    RoboCheckEqInt(GfxImageCreateEmpty(&idesc, &img), RoboOk);
    RoboCheck(GfxImageSurface(img) == NULL);
    GfxImageRelease(img);
}

static void TestGfxBackendCaps(void)
{
    GfxBackendInfo info;
    GfxCapabilities caps;
    size_t i;
    int foundCpu = 0;

    RoboCheck(GfxBackendCount() >= 1u);
    for (i = 0u; i < GfxBackendCount(); i++) {
        RoboCheckEqInt(GfxBackendAt(i, &info), RoboOk);
        if (info.id == RoboGfxBackendCpuRaster) {
            foundCpu = 1;
            RoboCheckEqInt(info.available, 1);
        }
    }
    RoboCheck(foundCpu);
    RoboCheckEqInt(GfxBackendFind(RoboGfxBackendGpu, &info), RoboOk);
    RoboCheckEqInt(info.available, 0);

    RoboCheckEqInt(GfxQueryCapabilities(&caps), RoboOk);
    RoboCheckEqInt(caps.cpuRasterAvailable, 1);
    RoboCheckEqInt(caps.gpuBackendAvailable, 0);
    RoboCheck(GfxCapabilitiesSupportsCpuFormat(&caps, RoboGfxFormatRgba8));
    RoboCheck(!GfxCapabilitiesSupportsCpuFormat(&caps, RoboGfxFormatRgbaF32));
}

static void TestGfxSmoke(void)
{
    GfxSurfaceDesc desc;
    GfxSurface *s = NULL;
    GfxFrame frame;
    uint8_t *row0;
    const uint8_t expect[4] = {0x11, 0x22, 0x33, 0xFF};

    RoboCheckEqInt(GraphicsInit(), RoboOk);
    GfxSurfaceDescInit(&desc);
    desc.width = 2;
    desc.height = 2;
    desc.format = RoboGfxFormatRgba8;
    RoboCheckEqInt(GfxSurfaceCreate(&desc, &s), RoboOk);
    RoboCheckEqInt(GfxSurfaceClearRgba8(s, GfxRgba8(0x11, 0x22, 0x33, 0xFF)),
                      RoboOk);
    row0 = GfxSurfaceRow(s, 0);
    RoboCheck(row0 != NULL);
    RoboCheck(memcmp(row0, expect, 4) == 0);

    GfxFrameInit(&frame);
    RoboCheckEqInt(GfxFrameBegin(&frame, s), RoboOk);
    RoboCheckEqInt(GfxFrameEnd(&frame), RoboOk);
    RoboCheckEqInt(GfxPresentNull(s), RoboOk);

    GfxSurfaceRelease(s);
    GraphicsShutdown();
}

static void TestGfxChromiumIndependence(void)
{
    /* Compile-time: only RoboGraphics / RoboBase / RoboMem types in call path. */
    RoboCheckStrEq(GraphicsVersionString(), "0.17.0");
}

void TestRegisterGraphics(void)
{
    TestRegister("graphics.version", TestGfxVersion);
    TestRegister("graphics.geometry", TestGfxGeometry);
    TestRegister("graphics.color", TestGfxColor);
    TestRegister("graphics.format", TestGfxPixelFormat);
    TestRegister("graphics.surface.owned", TestGfxSurfaceOwned);
    TestRegister("graphics.surface.borrowed", TestGfxSurfaceBorrowed);
    TestRegister("graphics.surface.errors", TestGfxSurfaceErrors);
    TestRegister("graphics.image.frame", TestGfxImageFrame);
    TestRegister("graphics.backend.caps", TestGfxBackendCaps);
    TestRegister("graphics.smoke", TestGfxSmoke);
    TestRegister("graphics.chromium", TestGfxChromiumIndependence);
}
