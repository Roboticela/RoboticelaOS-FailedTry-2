#include "RoboGraphics/RoboGraphicsPixelFormat.h"

#include <stdint.h>

robo_result_t robo_gfx_pixel_format_info(robo_gfx_pixel_format_t format,
                                         robo_gfx_pixel_format_info_t *out)
{
    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    out->format = format;
    out->bytes_per_pixel = 0u;
    out->channel_count = 0u;
    out->channel_bits = 0u;
    out->has_alpha = 0u;
    out->alpha_stored = 0u;
    out->is_grayscale = 0u;
    out->is_float = 0u;
    out->is_normalized_int = 0u;
    out->is_byte_addressable = 0u;
    out->is_implemented = 0u;
    out->order = ROBO_GFX_CHANNELS_UNKNOWN;
    out->name = "unknown";

    switch (format) {
    case ROBO_GFX_FORMAT_RGBA8:
        out->bytes_per_pixel = 4u;
        out->channel_count = 4u;
        out->channel_bits = 8u;
        out->has_alpha = 1u;
        out->alpha_stored = 1u;
        out->is_normalized_int = 1u;
        out->is_byte_addressable = 1u;
        out->is_implemented = 1u;
        out->order = ROBO_GFX_CHANNELS_RGBA;
        out->name = "RGBA8";
        return ROBO_OK;
    case ROBO_GFX_FORMAT_BGRA8:
        out->bytes_per_pixel = 4u;
        out->channel_count = 4u;
        out->channel_bits = 8u;
        out->has_alpha = 1u;
        out->alpha_stored = 1u;
        out->is_normalized_int = 1u;
        out->is_byte_addressable = 1u;
        out->is_implemented = 1u;
        out->order = ROBO_GFX_CHANNELS_BGRA;
        out->name = "BGRA8";
        return ROBO_OK;
    case ROBO_GFX_FORMAT_RGB8:
        out->bytes_per_pixel = 3u;
        out->channel_count = 3u;
        out->channel_bits = 8u;
        out->has_alpha = 0u;
        out->alpha_stored = 0u;
        out->is_normalized_int = 1u;
        out->is_byte_addressable = 1u;
        out->is_implemented = 1u;
        out->order = ROBO_GFX_CHANNELS_RGB;
        out->name = "RGB8";
        return ROBO_OK;
    case ROBO_GFX_FORMAT_GRAY8:
        out->bytes_per_pixel = 1u;
        out->channel_count = 1u;
        out->channel_bits = 8u;
        out->has_alpha = 0u;
        out->alpha_stored = 0u;
        out->is_grayscale = 1u;
        out->is_normalized_int = 1u;
        out->is_byte_addressable = 1u;
        out->is_implemented = 1u;
        out->order = ROBO_GFX_CHANNELS_GRAY;
        out->name = "GRAY8";
        return ROBO_OK;
    case ROBO_GFX_FORMAT_ALPHA8:
        out->bytes_per_pixel = 1u;
        out->channel_count = 1u;
        out->channel_bits = 8u;
        out->has_alpha = 1u;
        out->alpha_stored = 1u;
        out->is_normalized_int = 1u;
        out->is_byte_addressable = 1u;
        out->is_implemented = 1u;
        out->order = ROBO_GFX_CHANNELS_ALPHA;
        out->name = "ALPHA8";
        return ROBO_OK;
    case ROBO_GFX_FORMAT_RGBA16:
        out->bytes_per_pixel = 8u;
        out->channel_count = 4u;
        out->channel_bits = 16u;
        out->has_alpha = 1u;
        out->alpha_stored = 1u;
        out->is_normalized_int = 1u;
        out->is_byte_addressable = 1u;
        out->is_implemented = 0u;
        out->order = ROBO_GFX_CHANNELS_RGBA;
        out->name = "RGBA16";
        return ROBO_OK;
    case ROBO_GFX_FORMAT_RGBA_F32:
        out->bytes_per_pixel = 16u;
        out->channel_count = 4u;
        out->channel_bits = 32u;
        out->has_alpha = 1u;
        out->alpha_stored = 1u;
        out->is_float = 1u;
        out->is_normalized_int = 0u;
        out->is_byte_addressable = 1u;
        out->is_implemented = 0u;
        out->order = ROBO_GFX_CHANNELS_RGBA;
        out->name = "RGBA_F32";
        return ROBO_OK;
    case ROBO_GFX_FORMAT_UNKNOWN:
    default:
        return ROBO_INVALID_ARGUMENT;
    }
}

int robo_gfx_pixel_format_is_known(robo_gfx_pixel_format_t format)
{
    robo_gfx_pixel_format_info_t info;
    return robo_gfx_pixel_format_info(format, &info) == ROBO_OK;
}

int robo_gfx_pixel_format_is_implemented(robo_gfx_pixel_format_t format)
{
    robo_gfx_pixel_format_info_t info;
    if (robo_gfx_pixel_format_info(format, &info) != ROBO_OK) {
        return 0;
    }
    return info.is_implemented != 0u;
}

robo_result_t robo_gfx_pixel_format_bytes_per_pixel(robo_gfx_pixel_format_t format,
                                                    uint8_t *out_bpp)
{
    robo_gfx_pixel_format_info_t info;
    robo_result_t res;

    if (!out_bpp) {
        return ROBO_INVALID_ARGUMENT;
    }
    res = robo_gfx_pixel_format_info(format, &info);
    if (res != ROBO_OK) {
        return res;
    }
    if (info.bytes_per_pixel == 0u) {
        return ROBO_NOT_SUPPORTED;
    }
    *out_bpp = info.bytes_per_pixel;
    return ROBO_OK;
}

robo_result_t robo_gfx_pixel_format_min_stride(robo_gfx_pixel_format_t format,
                                               uint32_t width,
                                               size_t *out_stride)
{
    uint8_t bpp = 0u;
    robo_result_t res;

    if (!out_stride || width == 0u) {
        return ROBO_INVALID_ARGUMENT;
    }
    res = robo_gfx_pixel_format_bytes_per_pixel(format, &bpp);
    if (res != ROBO_OK) {
        return res;
    }
    if (width > (SIZE_MAX / (size_t)bpp)) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out_stride = (size_t)width * (size_t)bpp;
    return ROBO_OK;
}

robo_result_t robo_gfx_pixel_format_row_bytes(robo_gfx_pixel_format_t format,
                                              uint32_t width,
                                              size_t *out_row_bytes)
{
    return robo_gfx_pixel_format_min_stride(format, width, out_row_bytes);
}

robo_result_t robo_gfx_pixel_format_validate_stride(robo_gfx_pixel_format_t format,
                                                    uint32_t width,
                                                    size_t stride_bytes)
{
    size_t min_stride = 0u;
    robo_result_t res;

    res = robo_gfx_pixel_format_min_stride(format, width, &min_stride);
    if (res != ROBO_OK) {
        return res;
    }
    if (stride_bytes < min_stride) {
        return ROBO_INVALID_ARGUMENT;
    }
    return ROBO_OK;
}

robo_result_t robo_gfx_pixel_format_buffer_bytes(robo_gfx_pixel_format_t format,
                                                 uint32_t width,
                                                 uint32_t height,
                                                 size_t stride_bytes,
                                                 size_t *out_bytes)
{
    size_t stride = 0u;
    robo_result_t res;

    if (!out_bytes || height == 0u) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (stride_bytes == 0u) {
        res = robo_gfx_pixel_format_min_stride(format, width, &stride);
        if (res != ROBO_OK) {
            return res;
        }
    } else {
        res = robo_gfx_pixel_format_validate_stride(format, width, stride_bytes);
        if (res != ROBO_OK) {
            return res;
        }
        stride = stride_bytes;
    }
    if (height > (SIZE_MAX / stride)) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out_bytes = stride * (size_t)height;
    return ROBO_OK;
}

robo_result_t robo_gfx_pixel_format_pixel_offset(robo_gfx_pixel_format_t format,
                                                 size_t stride_bytes,
                                                 uint32_t x,
                                                 uint32_t y,
                                                 size_t *out_offset)
{
    uint8_t bpp = 0u;
    size_t row_off;
    size_t pix_off;
    robo_result_t res;

    if (!out_offset || stride_bytes == 0u) {
        return ROBO_INVALID_ARGUMENT;
    }
    res = robo_gfx_pixel_format_bytes_per_pixel(format, &bpp);
    if (res != ROBO_OK) {
        return res;
    }
    if ((size_t)bpp > stride_bytes) {
        return ROBO_INVALID_ARGUMENT;
    }
    /* Pixel must fit in the row: x*bpp + bpp <= stride */
    if (x > ((stride_bytes - (size_t)bpp) / (size_t)bpp)) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (y > 0u && y > (SIZE_MAX / stride_bytes)) {
        return ROBO_INVALID_ARGUMENT;
    }
    row_off = (size_t)y * stride_bytes;
    pix_off = (size_t)x * (size_t)bpp;
    if (pix_off > SIZE_MAX - row_off) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out_offset = row_off + pix_off;
    return ROBO_OK;
}

robo_result_t robo_gfx_pixel_pack(robo_gfx_pixel_format_t format,
                                  robo_gfx_color_rgba8_t color,
                                  uint8_t *dst,
                                  size_t dst_cap)
{
    robo_gfx_pixel_format_info_t info;

    if (!dst) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (robo_gfx_pixel_format_info(format, &info) != ROBO_OK) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!info.is_implemented) {
        return ROBO_NOT_SUPPORTED;
    }
    if (dst_cap < info.bytes_per_pixel) {
        return ROBO_INVALID_ARGUMENT;
    }

    switch (format) {
    case ROBO_GFX_FORMAT_RGBA8:
        dst[0] = color.r;
        dst[1] = color.g;
        dst[2] = color.b;
        dst[3] = color.a;
        return ROBO_OK;
    case ROBO_GFX_FORMAT_BGRA8:
        dst[0] = color.b;
        dst[1] = color.g;
        dst[2] = color.r;
        dst[3] = color.a;
        return ROBO_OK;
    case ROBO_GFX_FORMAT_RGB8:
        dst[0] = color.r;
        dst[1] = color.g;
        dst[2] = color.b;
        return ROBO_OK;
    case ROBO_GFX_FORMAT_GRAY8:
        dst[0] = robo_gfx_color_rgba8_luminance8(color);
        return ROBO_OK;
    case ROBO_GFX_FORMAT_ALPHA8:
        dst[0] = color.a;
        return ROBO_OK;
    default:
        return ROBO_NOT_SUPPORTED;
    }
}

robo_result_t robo_gfx_pixel_unpack(robo_gfx_pixel_format_t format,
                                    const uint8_t *src,
                                    size_t src_len,
                                    robo_gfx_color_rgba8_t *out_color)
{
    robo_gfx_pixel_format_info_t info;

    if (!src || !out_color) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (robo_gfx_pixel_format_info(format, &info) != ROBO_OK) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!info.is_implemented) {
        return ROBO_NOT_SUPPORTED;
    }
    if (src_len < info.bytes_per_pixel) {
        return ROBO_INVALID_ARGUMENT;
    }

    switch (format) {
    case ROBO_GFX_FORMAT_RGBA8:
        *out_color = robo_gfx_rgba8(src[0], src[1], src[2], src[3]);
        return ROBO_OK;
    case ROBO_GFX_FORMAT_BGRA8:
        *out_color = robo_gfx_rgba8(src[2], src[1], src[0], src[3]);
        return ROBO_OK;
    case ROBO_GFX_FORMAT_RGB8:
        *out_color = robo_gfx_rgba8(src[0], src[1], src[2], 255u);
        return ROBO_OK;
    case ROBO_GFX_FORMAT_GRAY8:
        *out_color = robo_gfx_rgba8(src[0], src[0], src[0], 255u);
        return ROBO_OK;
    case ROBO_GFX_FORMAT_ALPHA8:
        *out_color = robo_gfx_rgba8(0u, 0u, 0u, src[0]);
        return ROBO_OK;
    default:
        return ROBO_NOT_SUPPORTED;
    }
}

robo_result_t robo_gfx_pixel_convert(robo_gfx_pixel_format_t src_format,
                                     const uint8_t *src,
                                     size_t src_len,
                                     robo_gfx_pixel_format_t dst_format,
                                     uint8_t *dst,
                                     size_t dst_cap)
{
    robo_gfx_color_rgba8_t color;
    robo_result_t res;

    res = robo_gfx_pixel_unpack(src_format, src, src_len, &color);
    if (res != ROBO_OK) {
        return res;
    }
    return robo_gfx_pixel_pack(dst_format, color, dst, dst_cap);
}
