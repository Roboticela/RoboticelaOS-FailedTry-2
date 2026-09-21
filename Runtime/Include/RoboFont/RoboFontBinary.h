/*
 * RoboFont big-endian binary reader (Goal 5.2).
 *
 * Bounds-checked reads from an immutable byte span. No host-endian casts,
 * no unaligned host loads. Reusable for later TrueType / OpenType / CFF tables.
 */

#ifndef ROBO_FONT_BINARY_H
#define ROBO_FONT_BINARY_H

#include "RoboBase/RoboError.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct FontSpan {
    const uint8_t *data;
    size_t size;
} FontSpan;

typedef struct FontReader {
    FontSpan span;
    size_t pos;
} FontReader;

void FontReaderInit(FontReader *r, const uint8_t *data, size_t size);
void FontReaderInitSpan(FontReader *r, FontSpan span);

size_t FontReaderRemaining(const FontReader *r);
size_t FontReaderPos(const FontReader *r);

/** Seek absolute; pos > size → RoboParseError. */
Result FontReaderSeek(FontReader *r, size_t pos);

/** Checked: if pos + n overflows or exceeds size → error. */
Result FontCheckedRange(size_t size, size_t offset, size_t length);

Result FontReadU8(FontReader *r, uint8_t *out);
Result FontReadU16be(FontReader *r, uint16_t *out);
Result FontReadI16be(FontReader *r, int16_t *out);
Result FontReadU32be(FontReader *r, uint32_t *out);
Result FontReadI32be(FontReader *r, int32_t *out);

/**
 * Peek without advancing. Same bounds rules as read.
 */
Result FontPeekU32be(const FontReader *r, uint32_t *out);

/** Subspan [offset, offset+length) of parent; validates bounds. */
Result FontSpanSlice(FontSpan parent,
                                   size_t offset,
                                   size_t length,
                                   FontSpan *out);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_FONT_BINARY_H */
