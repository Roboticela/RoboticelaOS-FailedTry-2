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

typedef struct robo_font_span {
    const uint8_t *data;
    size_t size;
} robo_font_span_t;

typedef struct robo_font_reader {
    robo_font_span_t span;
    size_t pos;
} robo_font_reader_t;

void robo_font_reader_init(robo_font_reader_t *r, const uint8_t *data, size_t size);
void robo_font_reader_init_span(robo_font_reader_t *r, robo_font_span_t span);

size_t robo_font_reader_remaining(const robo_font_reader_t *r);
size_t robo_font_reader_pos(const robo_font_reader_t *r);

/** Seek absolute; pos > size → ROBO_PARSE_ERROR. */
robo_result_t robo_font_reader_seek(robo_font_reader_t *r, size_t pos);

/** Checked: if pos + n overflows or exceeds size → error. */
robo_result_t robo_font_checked_range(size_t size, size_t offset, size_t length);

robo_result_t robo_font_read_u8(robo_font_reader_t *r, uint8_t *out);
robo_result_t robo_font_read_u16be(robo_font_reader_t *r, uint16_t *out);
robo_result_t robo_font_read_i16be(robo_font_reader_t *r, int16_t *out);
robo_result_t robo_font_read_u32be(robo_font_reader_t *r, uint32_t *out);
robo_result_t robo_font_read_i32be(robo_font_reader_t *r, int32_t *out);

/**
 * Peek without advancing. Same bounds rules as read.
 */
robo_result_t robo_font_peek_u32be(const robo_font_reader_t *r, uint32_t *out);

/** Subspan [offset, offset+length) of parent; validates bounds. */
robo_result_t robo_font_span_slice(robo_font_span_t parent,
                                   size_t offset,
                                   size_t length,
                                   robo_font_span_t *out);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_FONT_BINARY_H */
