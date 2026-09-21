#include "RoboFont/RoboFontSource.h"

#include "RoboMem/RoboMem.h"

#include <string.h>

struct FontSource {
    uint32_t refs;
    FontSourceKind kind;
    uint8_t *owned;      /* non-NULL only for COPY */
    const uint8_t *view; /* points to owned or borrower buffer */
    size_t size;
};

Result FontSourceCreateMemoryCopy(const void *bytes,
                                                  size_t size,
                                                  FontSource **out)
{
    FontSource *s;
    uint8_t *buf;

    if (!out || !bytes || size == 0u) {
        return RoboInvalidArgument;
    }
    *out = NULL;
    s = (FontSource *)RoboCalloc(1, sizeof(*s));
    if (!s) {
        return RoboOutOfMemory;
    }
    buf = (uint8_t *)Malloc(size);
    if (!buf) {
        RoboFree(s);
        return RoboOutOfMemory;
    }
    memcpy(buf, bytes, size);
    s->refs = 1u;
    s->kind = RoboFontSourceMemoryCopy;
    s->owned = buf;
    s->view = buf;
    s->size = size;
    *out = s;
    return RoboOk;
}

Result FontSourceCreateMemoryBorrow(const void *bytes,
                                                    size_t size,
                                                    FontSource **out)
{
    FontSource *s;

    if (!out || !bytes || size == 0u) {
        return RoboInvalidArgument;
    }
    *out = NULL;
    s = (FontSource *)RoboCalloc(1, sizeof(*s));
    if (!s) {
        return RoboOutOfMemory;
    }
    s->refs = 1u;
    s->kind = RoboFontSourceMemoryBorrow;
    s->owned = NULL;
    s->view = (const uint8_t *)bytes;
    s->size = size;
    *out = s;
    return RoboOk;
}

void FontSourceRetain(FontSource *src)
{
    if (!src) {
        return;
    }
    src->refs++;
}

void FontSourceRelease(FontSource *src)
{
    if (!src) {
        return;
    }
    if (src->refs == 0u) {
        return;
    }
    src->refs--;
    if (src->refs != 0u) {
        return;
    }
    if (src->owned) {
        RoboFree(src->owned);
        src->owned = NULL;
    }
    src->view = NULL;
    src->size = 0;
    RoboFree(src);
}

FontSourceKind MakeFontSourceKind(const FontSource *src)
{
    return src ? src->kind : (FontSourceKind)0;
}

size_t FontSourceSize(const FontSource *src)
{
    return src ? src->size : 0u;
}

const uint8_t *FontSourceBytes(const FontSource *src)
{
    return src ? src->view : NULL;
}
