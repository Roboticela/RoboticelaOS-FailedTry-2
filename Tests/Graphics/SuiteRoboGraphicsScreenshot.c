/*
 * RoboGraphics Goal 6.14 — screenshot / framebuffer export.
 */

#include "RoboGraphics/RoboGraphics.h"
#include "RoboMem/RoboMem.h"
#include "RoboTest.h"

#include <stdint.h>
#include <string.h>

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
    desc.access = RoboGfxSurfaceAccessReadWrite;
    return GfxSurfaceCreate(&desc, out);
}

static void ExpectColor(GfxColorRgba8 got,
                         uint8_t r,
                         uint8_t g,
                         uint8_t b,
                         uint8_t a)
{
    RoboCheckEqInt(got.r, r);
    RoboCheckEqInt(got.g, g);
    RoboCheckEqInt(got.b, b);
    RoboCheckEqInt(got.a, a);
}

static void TestShotFullRgba(void)
{
    GfxSurface *s = NULL;
    GfxScreenshot *shot = NULL;
    GfxCaptureOptions opts;
    GfxColorRgba8 c;

    RoboCheck(MakeSurface(1, 1, RoboGfxFormatRgba8, 0, &s) == RoboOk);
    RoboCheck(GfxSurfaceSetPixel(s, 0, 0,
                                          GfxRgba8(10, 20, 30, 40)) ==
               RoboOk);

    GfxCaptureOptionsInit(&opts);
    RoboCheck(GfxScreenshotCaptureSurface(s, &opts, &shot) == RoboOk);
    RoboCheckEqInt(GfxScreenshotWidth(shot), 1);
    RoboCheckEqInt(GfxScreenshotHeight(shot), 1);
    RoboCheckEqInt(GfxScreenshotFormat(shot), RoboGfxFormatRgba8);
    RoboCheck(GfxSurfaceGetPixel(GfxScreenshotSurface(shot), 0,
                                          0, &c) == RoboOk);
    ExpectColor(c, 10, 20, 30, 40);

    GfxScreenshotRelease(shot);
    GfxSurfaceRelease(s);
}

static void TestShotImmutability(void)
{
    GfxSurface *s = NULL;
    GfxScreenshot *shot = NULL;
    GfxCaptureOptions opts;
    GfxColorRgba8 c;

    RoboCheck(MakeSurface(2, 2, RoboGfxFormatRgba8, 0, &s) == RoboOk);
    RoboCheck(GfxSurfaceClear(s, GfxRgba8(1, 2, 3, 255)) ==
               RoboOk);
    RoboCheck(GfxSurfaceSetPixel(s, 1, 1,
                                          GfxRgba8(9, 8, 7, 6)) ==
               RoboOk);

    GfxCaptureOptionsInit(&opts);
    RoboCheck(GfxScreenshotCaptureSurface(s, &opts, &shot) == RoboOk);

    RoboCheck(GfxSurfaceClear(s, GfxRgba8(255, 0, 0, 255)) ==
               RoboOk);
    RoboCheck(GfxSurfaceGetPixel(GfxScreenshotSurface(shot), 1,
                                          1, &c) == RoboOk);
    ExpectColor(c, 9, 8, 7, 6);
    RoboCheck(GfxSurfaceGetPixel(GfxScreenshotSurface(shot), 0,
                                          0, &c) == RoboOk);
    ExpectColor(c, 1, 2, 3, 255);

    GfxScreenshotRelease(shot);
    GfxSurfaceRelease(s);
}

static void TestShotRegionPolicies(void)
{
    GfxSurface *s = NULL;
    GfxScreenshot *shot = NULL;
    GfxCaptureOptions opts;
    GfxColorRgba8 c;

    RoboCheck(MakeSurface(4, 4, RoboGfxFormatRgba8, 0, &s) == RoboOk);
    RoboCheck(GfxSurfaceClear(s, GfxRgba8(0, 0, 0, 255)) ==
               RoboOk);
    RoboCheck(GfxSurfaceSetPixel(s, 2, 1,
                                          GfxRgba8(11, 22, 33, 44)) ==
               RoboOk);

    /* Interior STRICT */
    GfxCaptureOptionsInit(&opts);
    opts.useFullExtent = 0;
    opts.regionPolicy = RoboGfxCaptureRegionStrict;
    opts.region = MakeGfxRect(1, 1, 2, 2);
    RoboCheck(GfxScreenshotCaptureSurface(s, &opts, &shot) == RoboOk);
    RoboCheckEqInt(GfxScreenshotWidth(shot), 2);
    RoboCheckEqInt(GfxScreenshotHeight(shot), 2);
    RoboCheck(GfxSurfaceGetPixel(GfxScreenshotSurface(shot), 1,
                                          0, &c) == RoboOk);
    ExpectColor(c, 11, 22, 33, 44);
    GfxScreenshotRelease(shot);

    /* Edge one-pixel */
    opts.region = MakeGfxRect(3, 3, 1, 1);
    RoboCheck(GfxScreenshotCaptureSurface(s, &opts, &shot) == RoboOk);
    RoboCheckEqInt(GfxScreenshotWidth(shot), 1);
    GfxScreenshotRelease(shot);

    /* Empty STRICT */
    opts.region = MakeGfxRect(0, 0, 0, 1);
    RoboCheck(GfxScreenshotCaptureSurface(s, &opts, &shot) ==
               RoboInvalidArgument);

    /* Outside STRICT */
    opts.region = MakeGfxRect(3, 3, 2, 2);
    RoboCheck(GfxScreenshotCaptureSurface(s, &opts, &shot) ==
               RoboInvalidArgument);

    /* Partial CLIP → intersection */
    opts.regionPolicy = RoboGfxCaptureRegionClip;
    opts.region = MakeGfxRect(2, 2, 8, 8);
    RoboCheck(GfxScreenshotCaptureSurface(s, &opts, &shot) == RoboOk);
    RoboCheckEqInt(GfxScreenshotWidth(shot), 2);
    RoboCheckEqInt(GfxScreenshotHeight(shot), 2);
    GfxScreenshotRelease(shot);

    /* Fully outside CLIP → empty fail */
    opts.region = MakeGfxRect(10, 10, 2, 2);
    RoboCheck(GfxScreenshotCaptureSurface(s, &opts, &shot) ==
               RoboInvalidArgument);

    /* Negative origin CLIP intersecting */
    opts.region = MakeGfxRect(-1, 0, 2, 1);
    RoboCheck(GfxScreenshotCaptureSurface(s, &opts, &shot) == RoboOk);
    RoboCheckEqInt(GfxScreenshotWidth(shot), 1);
    GfxScreenshotRelease(shot);

    GfxSurfaceRelease(s);
}

static void TestShotFramebufferAndStride(void)
{
    GfxFramebufferDesc fbDesc;
    GfxFramebuffer *fb = NULL;
    GfxSurface *s = NULL;
    GfxScreenshot *shot = NULL;
    GfxCaptureOptions opts;
    GfxColorRgba8 c;
    uint32_t x;
    uint32_t y;

    GfxFramebufferDescInit(&fbDesc);
    fbDesc.width = 5;
    fbDesc.height = 3;
    fbDesc.format = RoboGfxFormatRgb8;
    fbDesc.strideBytes = 5u * 3u + 7u;
    RoboCheck(GfxFramebufferCreate(&fbDesc, &fb) == RoboOk);

    RoboCheck(MakeSurface(5, 3, RoboGfxFormatRgb8, fbDesc.strideBytes,
                            &s) == RoboOk);
    /* Paint via surface then copy path from FB separately */
    for (y = 0u; y < 3u; y++) {
        for (x = 0u; x < 5u; x++) {
            RoboCheck(GfxSurfaceSetPixel(
                           s, x, y,
                           GfxRgb8((uint8_t)(x * 10u), (uint8_t)(y * 20u),
                                         99)) == RoboOk);
        }
    }
    /* Mirror into framebuffer bytes */
    {
        size_t bpp = 3u;
        size_t stride = GfxFramebufferStride(fb);
        uint8_t *dst = GfxFramebufferPixels(fb);
        const uint8_t *src = GfxSurfacePixelsConst(s);
        size_t srcStride = GfxSurfaceStride(s);
        for (y = 0u; y < 3u; y++) {
            memcpy(dst + y * stride, src + y * srcStride, 5u * bpp);
        }
    }

    GfxCaptureOptionsInit(&opts);
    RoboCheck(GfxScreenshotCaptureFramebuffer(fb, &opts, &shot) ==
               RoboOk);
    RoboCheck(GfxSurfaceGetPixel(GfxScreenshotSurface(shot), 4,
                                          2, &c) == RoboOk);
    ExpectColor(c, 40, 40, 99, 255);
    GfxScreenshotRelease(shot);

    opts.useFullExtent = 0;
    opts.regionPolicy = RoboGfxCaptureRegionStrict;
    opts.region = MakeGfxRect(1, 1, 2, 1);
    RoboCheck(GfxScreenshotCapture(
                   RoboGfxCaptureFramebuffer, NULL, fb, &opts, &shot) ==
               RoboOk);
    RoboCheckEqInt(GfxScreenshotWidth(shot), 2);
    RoboCheck(GfxSurfaceGetPixel(GfxScreenshotSurface(shot), 0,
                                          0, &c) == RoboOk);
    ExpectColor(c, 10, 20, 99, 255);
    GfxScreenshotRelease(shot);

    GfxSurfaceRelease(s);
    GfxFramebufferRelease(fb);
}

static void TestShotBufferSelection(void)
{
    GfxSurface *s = NULL;
    GfxScreenshot *shot = NULL;
    GfxCaptureOptions opts;

    RoboCheck(MakeSurface(2, 2, RoboGfxFormatRgba8, 0, &s) == RoboOk);
    GfxCaptureOptionsInit(&opts);

    RoboCheck(GfxScreenshotCapture(RoboGfxCaptureFrontBuffer, s,
                                           NULL, &opts, &shot) ==
               RoboNotSupported);
    RoboCheck(GfxScreenshotCapture(RoboGfxCaptureBackBuffer, s, NULL,
                                           &opts, &shot) ==
               RoboNotSupported);
    RoboCheck(GfxScreenshotCapture(RoboGfxCaptureSurface, NULL, NULL,
                                           &opts, &shot) ==
               RoboInvalidArgument);
    RoboCheck(GfxScreenshotCapture(RoboGfxCaptureSurface, s, NULL,
                                           &opts, &shot) == RoboOk);
    GfxScreenshotRelease(shot);
    GfxSurfaceRelease(s);
}

static void TestShotExportDeterministic(void)
{
    GfxSurface *s = NULL;
    GfxScreenshot *shot = NULL;
    GfxCaptureOptions caps;
    GfxEncodeOptions enc;
    GfxEncodedBuffer a;
    GfxEncodedBuffer b;
    GfxEncodedBuffer c;
    GfxImage *img = NULL;

    RoboCheck(MakeSurface(3, 2, RoboGfxFormatRgba8, 0, &s) == RoboOk);
    RoboCheck(GfxSurfaceSetPixel(s, 0, 0,
                                          GfxRgba8(255, 0, 0, 128)) ==
               RoboOk);
    RoboCheck(GfxSurfaceSetPixel(s, 1, 0,
                                          GfxRgba8(0, 255, 0, 255)) ==
               RoboOk);
    RoboCheck(GfxSurfaceSetPixel(s, 2, 1,
                                          GfxRgba8(0, 0, 255, 0)) ==
               RoboOk);

    GfxCaptureOptionsInit(&caps);
    RoboCheck(GfxScreenshotCaptureSurface(s, &caps, &shot) == RoboOk);

    GfxEncodeOptionsInit(&enc);
    RoboCheck(GfxScreenshotExport(shot, &enc, &a) == RoboOk);
    RoboCheck(GfxScreenshotExport(shot, &enc, &b) == RoboOk);
    RoboCheck(a.size == b.size);
    RoboCheck(memcmp(a.data, b.data, a.size) == 0);

    enc.container = RoboGfxEncodeBmp;
    RoboCheck(GfxScreenshotExport(shot, &enc, &c) == RoboOk);
    RoboCheck(c.size >= 14u);
    RoboCheck(c.data[0] == 'B' && c.data[1] == 'M');

    RoboCheck(GfxScreenshotToImage(shot, &img) == RoboOk);
    RoboCheckEqInt(GfxImageWidth(img), 3);

    RoboCheck(GfxSurfaceExportScreenshot(s, &caps, &enc, &a) ==
               RoboOk);
    RoboCheck(a.size > 0u);

    GfxEncodedBufferRelease(&a);
    GfxEncodedBufferRelease(&b);
    GfxEncodedBufferRelease(&c);
    GfxImageRelease(img);
    GfxScreenshotRelease(shot);
    GfxSurfaceRelease(s);
}

static void TestShotAlphaAndChannels(void)
{
    GfxSurface *s = NULL;
    GfxScreenshot *shot = NULL;
    GfxCaptureOptions opts;
    GfxEncodeOptions enc;
    GfxEncodedBuffer buf;
    GfxColorRgba8 c;

    RoboCheck(MakeSurface(2, 1, RoboGfxFormatBgra8, 0, &s) == RoboOk);
    RoboCheck(GfxSurfaceSetPixel(s, 0, 0,
                                          GfxRgba8(1, 2, 3, 4)) ==
               RoboOk);
    RoboCheck(GfxSurfaceSetPixel(s, 1, 0,
                                          GfxRgba8(255, 255, 255, 0)) ==
               RoboOk);

    GfxCaptureOptionsInit(&opts);
    RoboCheck(GfxScreenshotCaptureSurface(s, &opts, &shot) == RoboOk);
    RoboCheckEqInt(GfxScreenshotFormat(shot), RoboGfxFormatBgra8);
    RoboCheck(GfxSurfaceGetPixel(GfxScreenshotSurface(shot), 0,
                                          0, &c) == RoboOk);
    ExpectColor(c, 1, 2, 3, 4);

    GfxEncodeOptionsInit(&enc);
    enc.layout = RoboGfxEncodeLayoutRgb8;
    RoboCheck(GfxScreenshotExport(shot, &enc, &buf) ==
               RoboInvalidArgument);
    enc.backgroundSet = 1;
    enc.background = GfxRgb8(0, 0, 0);
    RoboCheck(GfxScreenshotExport(shot, &enc, &buf) == RoboOk);

    GfxEncodedBufferRelease(&buf);
    GfxScreenshotRelease(shot);
    GfxSurfaceRelease(s);
}

static void TestShotSafety(void)
{
    GfxScreenshot *shot = NULL;
    GfxCaptureOptions opts;
    GfxEncodeOptions enc;
    GfxEncodedBuffer buf;

    GfxCaptureOptionsInit(&opts);
    GfxEncodeOptionsInit(&enc);

    RoboCheck(GfxScreenshotCaptureSurface(NULL, &opts, &shot) ==
               RoboInvalidArgument);
    RoboCheck(GfxScreenshotCaptureFramebuffer(NULL, &opts, &shot) ==
               RoboInvalidArgument);
    RoboCheck(GfxScreenshotExport(NULL, &enc, &buf) ==
               RoboInvalidArgument);
    RoboCheck(GfxSurfaceExportScreenshot(NULL, &opts, &enc, &buf) ==
               RoboInvalidArgument);

    RoboCheckEqInt(GfxScreenshotWidth(NULL), 0);
    RoboCheck(GfxScreenshotSurface(NULL) == NULL);
}

void TestRegisterGraphicsScreenshot(void)
{
    TestRegister("graphics.shot.full_rgba", TestShotFullRgba);
    TestRegister("graphics.shot.immutable", TestShotImmutability);
    TestRegister("graphics.shot.region", TestShotRegionPolicies);
    TestRegister("graphics.shot.framebuffer_stride",
                       TestShotFramebufferAndStride);
    TestRegister("graphics.shot.buffer_select",
                       TestShotBufferSelection);
    TestRegister("graphics.shot.export", TestShotExportDeterministic);
    TestRegister("graphics.shot.alpha_channels",
                       TestShotAlphaAndChannels);
    TestRegister("graphics.shot.safety", TestShotSafety);
}
