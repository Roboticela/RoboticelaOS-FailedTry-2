/*
 * Cursor pack gallery (Phase 44.7) — `desktop.cursor.*`.
 *
 * Loads the shipped `System/Cursors/roboticela.rcur`, paints every Input shape
 * (and the extra pack states) into a grid, and writes `Build/Cursors-Gallery.png`.
 */

#include "RoboTest.h"

#include "RoboDesktop/RoboDesktop.h"
#include "RoboDesktop/RoboDesktopCursorPack.h"
#include "RoboGraphics/RoboGraphicsEncode.h"
#include "RoboGraphics/RoboGraphicsRaster.h"
#include "RoboGraphics/RoboGraphicsSurface.h"
#include "RoboInput/RoboInputCursor.h"
#include "RoboMem/RoboMem.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GALLERY_PATH "Build/Cursors-Gallery.png"
#define RCUR_PATH "System/Cursors/roboticela.rcur"

static unsigned char *ReadAll(const char *path, size_t *outSize)
{
    FILE *file;
    long size;
    unsigned char *bytes;

    if (outSize) {
        *outSize = 0u;
    }
    file = fopen(path, "rb");
    if (!file) {
        return NULL;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }
    size = ftell(file);
    if (size <= 0) {
        fclose(file);
        return NULL;
    }
    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    bytes = (unsigned char *)malloc((size_t)size);
    if (!bytes) {
        fclose(file);
        return NULL;
    }
    if (fread(bytes, 1u, (size_t)size, file) != (size_t)size) {
        free(bytes);
        fclose(file);
        return NULL;
    }
    fclose(file);
    if (outSize) {
        *outSize = (size_t)size;
    }
    return bytes;
}

static int WriteSurfacePng(const char *path, const GfxSurface *surface)
{
    GfxEncodeOptions opts;
    GfxEncodedBuffer buffer;
    FILE *file;
    Result status;

    GfxEncodeOptionsInit(&opts);
    opts.container = RoboGfxEncodePng;
    opts.layout = RoboGfxEncodeLayoutRgba8;
    memset(&buffer, 0, sizeof(buffer));
    status = GfxSurfaceEncode(surface, &opts, &buffer);
    if (status != RoboOk || !buffer.data || buffer.size == 0u) {
        GfxEncodedBufferRelease(&buffer);
        return 0;
    }
    file = fopen(path, "wb");
    if (!file) {
        GfxEncodedBufferRelease(&buffer);
        return 0;
    }
    if (fwrite(buffer.data, 1u, buffer.size, file) != buffer.size) {
        fclose(file);
        GfxEncodedBufferRelease(&buffer);
        return 0;
    }
    fclose(file);
    GfxEncodedBufferRelease(&buffer);
    return 1;
}

static void TestCursorPackLoad(void)
{
    size_t size = 0u;
    unsigned char *bytes;
    DesktopCursorPack *pack = NULL;
    DesktopCursorFrame frame;
    Result status;

    bytes = ReadAll(RCUR_PATH, &size);
    RoboCheckMsg(bytes != NULL, "System/Cursors/roboticela.rcur must exist — run PackRoboCursors.py");
    status = DesktopCursorPackCreateFromRcur(bytes, size, &pack);
    free(bytes);
    RoboCheck(status == RoboOk);
    RoboCheck(DesktopCursorPackIsLive(pack));
    RoboCheck(DesktopCursorPackEntryCount(pack) >= 12u);
    RoboCheck(DesktopCursorPackLogicalPt(pack) == 16u);

    status = DesktopCursorPackResolve(pack, RoboInputCursorDefault, RoboDesktopCursorThemeLight, 32u, 0u,
                                      &frame);
    RoboCheck(status == RoboOk);
    RoboCheck(frame.surface != NULL);
    RoboCheck(frame.pixelSize == 32u);

    status = DesktopCursorPackResolve(pack, RoboInputCursorWait, RoboDesktopCursorThemeDark, 32u, 0u,
                                      &frame);
    RoboCheck(status == RoboOk);
    RoboCheck(frame.frameCount == 12u);

    status = DesktopCursorPackResolveByName(pack, "pencil", RoboDesktopCursorThemeLight, 32u, 0u, &frame);
    RoboCheck(status == RoboOk);
    RoboCheck(frame.surface != NULL);

    status = DesktopCursorPackResolve(pack, RoboInputCursorNone, RoboDesktopCursorThemeLight, 32u, 0u,
                                      &frame);
    RoboCheck(status == RoboOk);
    RoboCheck(frame.surface == NULL);

    DesktopCursorPackDestroy(pack);
}

static void TestCursorGallery(void)
{
    static const InputCursorType shapes[] = {
        RoboInputCursorDefault,           RoboInputCursorPointer,
        RoboInputCursorText,              RoboInputCursorCrosshair,
        RoboInputCursorMove,              RoboInputCursorResizeHorizontal,
        RoboInputCursorResizeVertical,    RoboInputCursorResizeDiagonal,
        RoboInputCursorNotAllowed,        RoboInputCursorWait,
        RoboInputCursorProgress,          RoboInputCursorGrabbing,
    };
    static const char *extras[] = {
        "pencil", "link", "vertical-text", "resize-diag-nesw", "col-resize", "row-resize",
    };
    size_t size = 0u;
    unsigned char *bytes;
    DesktopCursorPack *pack = NULL;
    GfxSurface *surface = NULL;
    GfxRaster *raster = NULL;
    Desktop *desktop = NULL;
    DesktopConfig config;
    uint32_t i;
    int32_t col = 0;
    int32_t row = 0;
    const int32_t cell = 96;
    const int32_t cols = 6;
    const int32_t rows = 3;
    Result status;

    GfxSurfaceDesc desc;

    bytes = ReadAll(RCUR_PATH, &size);
    RoboCheck(bytes != NULL);
    status = DesktopCursorPackCreateFromRcur(bytes, size, &pack);
    free(bytes);
    RoboCheck(status == RoboOk);

    GfxSurfaceDescInit(&desc);
    desc.width = (uint32_t)(cell * cols);
    desc.height = (uint32_t)(cell * rows);
    desc.format = RoboGfxFormatRgba8;
    desc.access = RoboGfxSurfaceAccessReadWrite;
    status = GfxSurfaceCreate(&desc, &surface);
    RoboCheck(status == RoboOk);
    RoboCheck(GfxRasterCreate(surface, &raster) == RoboOk);
    (void)GfxRasterFillRect(raster, MakeGfxRect(0, 0, cell * cols, cell * rows),
                            GfxRgb8(245u, 245u, 247u));

    for (i = 0u; i < sizeof(shapes) / sizeof(shapes[0]); i++) {
        DesktopCursorFrame frame;
        GfxPoint tip = MakeGfxPoint(col * cell + cell / 2, row * cell + cell / 2);

        status = DesktopCursorPackResolve(pack, shapes[i], RoboDesktopCursorThemeLight, 32u, 0u, &frame);
        RoboCheck(status == RoboOk);
        RoboCheck(frame.surface != NULL);
        (void)DesktopCursorPackBlit(surface, tip, &frame);
        col++;
        if (col >= cols) {
            col = 0;
            row++;
        }
    }
    for (i = 0u; i < sizeof(extras) / sizeof(extras[0]); i++) {
        DesktopCursorFrame frame;
        GfxPoint tip = MakeGfxPoint(col * cell + cell / 2, row * cell + cell / 2);

        status = DesktopCursorPackResolveByName(pack, extras[i], RoboDesktopCursorThemeLight, 32u, 0u,
                                                 &frame);
        RoboCheck(status == RoboOk);
        (void)DesktopCursorPackBlit(surface, tip, &frame);
        col++;
        if (col >= cols) {
            col = 0;
            row++;
        }
    }

    RoboCheckMsg(WriteSurfacePng(GALLERY_PATH, surface), "wrote cursor gallery PNG");
    printf("cursor gallery: %s\n", GALLERY_PATH);

    /* Attach to a live desktop and confirm the shape API wires through. */
    DesktopConfigInit(&config);
    config.width = 320;
    config.height = 240;
    config.showCursor = 1;
    RoboCheck(DesktopCreate(&config, &desktop, NULL) == RoboOk);
    RoboCheck(DesktopAttachCursorPack(desktop, pack) == RoboOk);
    pack = NULL;
    DesktopSetCursorShape(desktop, RoboInputCursorCrosshair);
    DesktopSetCursorScale(desktop, 2.0f);
    RoboCheck(DesktopGetCursorShape(desktop) == RoboInputCursorCrosshair);
    RoboCheck(DesktopGetCursorPack(desktop) != NULL);
    DesktopDestroy(desktop);

    GfxRasterDestroy(raster);
    GfxSurfaceRelease(surface);
}

void TestRegisterDesktopCursorGallery(void)
{
    TestRegister("desktop.cursor.pack.load", TestCursorPackLoad);
    TestRegister("desktop.cursor.gallery", TestCursorGallery);
}
