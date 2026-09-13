#include "RoboFont/RoboFontSource.h"

#include "RoboMem/RoboMem.h"

#include <string.h>

struct robo_font_source {
    uint32_t refs;
    robo_font_source_kind_t kind;
    uint8_t *owned;      /* non-NULL only for COPY */
    const uint8_t *view; /* points to owned or borrower buffer */
    size_t size;
};

robo_result_t robo_font_source_create_memory_copy(const void *bytes,
                                                  size_t size,
                                                  robo_font_source_t **out)
{
    robo_font_source_t *s;
    uint8_t *buf;

    if (!out || !bytes || size == 0u) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out = NULL;
    s = (robo_font_source_t *)robo_calloc(1, sizeof(*s));
    if (!s) {
        return ROBO_OUT_OF_MEMORY;
    }
    buf = (uint8_t *)robo_malloc(size);
    if (!buf) {
        robo_free(s);
        return ROBO_OUT_OF_MEMORY;
    }
    memcpy(buf, bytes, size);
    s->refs = 1u;
    s->kind = ROBO_FONT_SOURCE_MEMORY_COPY;
    s->owned = buf;
    s->view = buf;
    s->size = size;
    *out = s;
    return ROBO_OK;
}

robo_result_t robo_font_source_create_memory_borrow(const void *bytes,
                                                    size_t size,
                                                    robo_font_source_t **out)
{
    robo_font_source_t *s;

    if (!out || !bytes || size == 0u) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out = NULL;
    s = (robo_font_source_t *)robo_calloc(1, sizeof(*s));
    if (!s) {
        return ROBO_OUT_OF_MEMORY;
    }
    s->refs = 1u;
    s->kind = ROBO_FONT_SOURCE_MEMORY_BORROW;
    s->owned = NULL;
    s->view = (const uint8_t *)bytes;
    s->size = size;
    *out = s;
    return ROBO_OK;
}

void robo_font_source_retain(robo_font_source_t *src)
{
    if (!src) {
        return;
    }
    src->refs++;
}

void robo_font_source_release(robo_font_source_t *src)
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
        robo_free(src->owned);
        src->owned = NULL;
    }
    src->view = NULL;
    src->size = 0;
    robo_free(src);
}

robo_font_source_kind_t robo_font_source_kind(const robo_font_source_t *src)
{
    return src ? src->kind : (robo_font_source_kind_t)0;
}

size_t robo_font_source_size(const robo_font_source_t *src)
{
    return src ? src->size : 0u;
}

const uint8_t *robo_font_source_bytes(const robo_font_source_t *src)
{
    return src ? src->view : NULL;
}
