/*
 * CFF DICT encoding and Top/Private interpretation (Goal 5.5).
 */

#include "RoboFont/RoboFontCff.h"

#include "RoboMem/RoboMem.h"

#include <string.h>

void robo_font_cff_dict_clear(robo_font_cff_dict_t *d)
{
    if (!d) {
        return;
    }
    robo_free(d->entries);
    memset(d, 0, sizeof(*d));
}

void robo_font_cff_top_dict_clear(robo_font_cff_top_dict_t *t)
{
    if (!t) {
        return;
    }
    robo_font_cff_dict_clear(&t->raw);
    memset(t, 0, sizeof(*t));
    t->charset = -1;
    t->encoding = -1;
    t->charstrings = -1;
    t->private_size = -1;
    t->private_offset = -1;
    t->fdarray = -1;
    t->fdselect = -1;
    t->vstore = -1;
    t->charstring_type = 2;
}

void robo_font_cff_private_dict_clear(robo_font_cff_private_dict_t *p)
{
    if (!p) {
        return;
    }
    robo_font_cff_dict_clear(&p->raw);
    memset(p, 0, sizeof(*p));
    p->subrs = -1;
}

void robo_font_cff_font_dict_clear(robo_font_cff_font_dict_t *f)
{
    if (!f) {
        return;
    }
    robo_font_cff_dict_clear(&f->raw);
    memset(f, 0, sizeof(*f));
    f->private_size = -1;
    f->private_offset = -1;
    f->font_name_sid = -1;
}

static robo_result_t decode_real(robo_font_reader_t *r, double *out)
{
    char buf[64];
    size_t n = 0u;
    int done = 0;

    while (!done) {
        uint8_t b;
        uint8_t nibbles[2];
        int ni;
        if (robo_font_read_u8(r, &b) != ROBO_OK) {
            return ROBO_PARSE_ERROR;
        }
        nibbles[0] = (uint8_t)(b >> 4);
        nibbles[1] = (uint8_t)(b & 0x0Fu);
        for (ni = 0; ni < 2; ni++) {
            uint8_t nb = nibbles[ni];
            if (nb <= 9u) {
                if (n + 1u >= sizeof(buf)) {
                    return ROBO_PARSE_ERROR;
                }
                buf[n++] = (char)('0' + nb);
            } else if (nb == 0xAu) {
                if (n + 1u >= sizeof(buf)) {
                    return ROBO_PARSE_ERROR;
                }
                buf[n++] = '.';
            } else if (nb == 0xBu) {
                if (n + 1u >= sizeof(buf)) {
                    return ROBO_PARSE_ERROR;
                }
                buf[n++] = 'E';
            } else if (nb == 0xCu) {
                if (n + 2u >= sizeof(buf)) {
                    return ROBO_PARSE_ERROR;
                }
                buf[n++] = 'E';
                buf[n++] = '-';
            } else if (nb == 0xDu) {
                /* reserved */
                return ROBO_PARSE_ERROR;
            } else if (nb == 0xEu) {
                if (n + 1u >= sizeof(buf)) {
                    return ROBO_PARSE_ERROR;
                }
                buf[n++] = '-';
            } else if (nb == 0xFu) {
                done = 1;
                break;
            } else {
                return ROBO_PARSE_ERROR;
            }
        }
    }
    buf[n] = '\0';
    if (n == 0u) {
        return ROBO_PARSE_ERROR;
    }
    {
        char *end = NULL;
        double v;
        /* minimal float parse without locale */
        v = 0.0;
        {
            const char *p = buf;
            int neg = 0;
            double frac = 0.0;
            double place = 0.1;
            int exp = 0;
            int exp_neg = 0;
            int in_frac = 0;
            int in_exp = 0;
            if (*p == '-') {
                neg = 1;
                p++;
            }
            for (; *p; p++) {
                if (*p == '.') {
                    in_frac = 1;
                    continue;
                }
                if (*p == 'E' || *p == 'e') {
                    in_exp = 1;
                    in_frac = 0;
                    if (p[1] == '-') {
                        exp_neg = 1;
                        p++;
                    }
                    continue;
                }
                if (*p < '0' || *p > '9') {
                    return ROBO_PARSE_ERROR;
                }
                if (in_exp) {
                    exp = exp * 10 + (*p - '0');
                } else if (in_frac) {
                    frac += place * (double)(*p - '0');
                    place *= 0.1;
                } else {
                    v = v * 10.0 + (double)(*p - '0');
                }
            }
            v = v + frac;
            if (exp_neg) {
                while (exp-- > 0) {
                    v *= 0.1;
                }
            } else {
                while (exp-- > 0) {
                    v *= 10.0;
                }
            }
            if (neg) {
                v = -v;
            }
            *out = v;
            (void)end;
        }
    }
    return ROBO_OK;
}

static robo_result_t push_num(robo_font_cff_number_t *stack,
                              uint8_t *sp,
                              robo_font_cff_number_t num)
{
    if (*sp >= ROBO_FONT_CFF_MAX_DICT_OPERANDS) {
        return ROBO_PARSE_ERROR;
    }
    stack[(*sp)++] = num;
    return ROBO_OK;
}

robo_result_t robo_font_cff_dict_parse(robo_font_span_t dict_bytes,
                                       robo_font_cff_dict_t *out)
{
    robo_font_reader_t r;
    robo_font_cff_number_t stack[ROBO_FONT_CFF_MAX_DICT_OPERANDS];
    uint8_t sp = 0u;
    robo_font_cff_dict_entry_t *entries = NULL;
    uint32_t cap = 0u;
    uint32_t count = 0u;

    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    robo_font_cff_dict_clear(out);
    if (dict_bytes.size == 0u) {
        return ROBO_OK;
    }
    if (dict_bytes.size > (1u << 20)) {
        return ROBO_PARSE_ERROR;
    }

    robo_font_reader_init_span(&r, dict_bytes);

    while (robo_font_reader_remaining(&r) > 0u) {
        uint8_t b0;
        if (robo_font_read_u8(&r, &b0) != ROBO_OK) {
            robo_free(entries);
            return ROBO_PARSE_ERROR;
        }

        if (b0 >= 32u || b0 == 28u || b0 == 29u || b0 == 30u) {
            robo_font_cff_number_t num;
            memset(&num, 0, sizeof(num));
            if (b0 == 28u) {
                int16_t s;
                if (robo_font_read_i16be(&r, &s) != ROBO_OK) {
                    robo_free(entries);
                    return ROBO_PARSE_ERROR;
                }
                num.i = (int32_t)s;
                num.real = (double)s;
            } else if (b0 == 29u) {
                int32_t v;
                if (robo_font_read_i32be(&r, &v) != ROBO_OK) {
                    robo_free(entries);
                    return ROBO_PARSE_ERROR;
                }
                num.i = v;
                num.real = (double)v;
            } else if (b0 == 30u) {
                double rv;
                if (decode_real(&r, &rv) != ROBO_OK) {
                    robo_free(entries);
                    return ROBO_PARSE_ERROR;
                }
                num.is_real = 1;
                num.real = rv;
                num.i = (int32_t)rv;
            } else if (b0 <= 246u) {
                int32_t v = (int32_t)b0 - 139;
                num.i = v;
                num.real = (double)v;
            } else if (b0 <= 250u) {
                uint8_t b1;
                int32_t v;
                if (robo_font_read_u8(&r, &b1) != ROBO_OK) {
                    robo_free(entries);
                    return ROBO_PARSE_ERROR;
                }
                v = ((int32_t)b0 - 247) * 256 + (int32_t)b1 + 108;
                num.i = v;
                num.real = (double)v;
            } else if (b0 <= 254u) {
                uint8_t b1;
                int32_t v;
                if (robo_font_read_u8(&r, &b1) != ROBO_OK) {
                    robo_free(entries);
                    return ROBO_PARSE_ERROR;
                }
                v = -((int32_t)b0 - 251) * 256 - (int32_t)b1 - 108;
                num.i = v;
                num.real = (double)v;
            } else {
                /* 255 reserved in DICT */
                robo_free(entries);
                return ROBO_PARSE_ERROR;
            }
            if (push_num(stack, &sp, num) != ROBO_OK) {
                robo_free(entries);
                return ROBO_PARSE_ERROR;
            }
            continue;
        }

        /* operator */
        {
            uint16_t op = b0;
            robo_font_cff_dict_entry_t ent;
            if (b0 == 12u) {
                uint8_t b1;
                if (robo_font_read_u8(&r, &b1) != ROBO_OK) {
                    robo_free(entries);
                    return ROBO_PARSE_ERROR;
                }
                op = (uint16_t)(1200u + (uint16_t)b1);
            }
            if (count >= ROBO_FONT_CFF_MAX_DICT_OPS) {
                robo_free(entries);
                return ROBO_PARSE_ERROR;
            }
            if (count + 1u > cap) {
                uint32_t ncap = cap ? cap * 2u : 16u;
                robo_font_cff_dict_entry_t *ne =
                    (robo_font_cff_dict_entry_t *)robo_calloc(
                        ncap, sizeof(robo_font_cff_dict_entry_t));
                if (!ne) {
                    robo_free(entries);
                    return ROBO_OUT_OF_MEMORY;
                }
                if (entries) {
                    memcpy(ne, entries, count * sizeof(*entries));
                    robo_free(entries);
                }
                entries = ne;
                cap = ncap;
            }
            memset(&ent, 0, sizeof(ent));
            ent.op = op;
            ent.argc = sp;
            memcpy(ent.args, stack, (size_t)sp * sizeof(stack[0]));
            entries[count++] = ent;
            sp = 0u;
        }
    }

    if (sp != 0u) {
        /* leftover operands without operator */
        robo_free(entries);
        return ROBO_PARSE_ERROR;
    }

    out->count = count;
    out->entries = entries;
    return ROBO_OK;
}

robo_result_t robo_font_cff_dict_find(const robo_font_cff_dict_t *d,
                                      uint16_t op,
                                      const robo_font_cff_dict_entry_t **out)
{
    uint32_t i;
    if (!d || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    for (i = 0u; i < d->count; i++) {
        if (d->entries[i].op == op) {
            *out = &d->entries[i];
            return ROBO_OK;
        }
    }
    return ROBO_NOT_FOUND;
}

static int32_t arg_i(const robo_font_cff_dict_entry_t *e, uint8_t i)
{
    if (i >= e->argc) {
        return 0;
    }
    return e->args[i].i;
}

static double arg_r(const robo_font_cff_dict_entry_t *e, uint8_t i)
{
    if (i >= e->argc) {
        return 0.0;
    }
    return e->args[i].real;
}

robo_result_t robo_font_cff_top_dict_interpret(const robo_font_cff_dict_t *raw,
                                               robo_font_cff_top_dict_t *out)
{
    uint32_t i;
    if (!raw || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    robo_font_cff_top_dict_clear(out);
    /* shallow: keep raw by re-parsing is awkward; copy pointer ownership?
     * Caller owns raw separately. We duplicate entries into out->raw. */
    out->raw.count = raw->count;
    if (raw->count) {
        out->raw.entries = (robo_font_cff_dict_entry_t *)robo_calloc(
            raw->count, sizeof(robo_font_cff_dict_entry_t));
        if (!out->raw.entries) {
            return ROBO_OUT_OF_MEMORY;
        }
        memcpy(out->raw.entries, raw->entries,
               raw->count * sizeof(robo_font_cff_dict_entry_t));
    }

    out->charset = 0; /* predefined ISOAdobe default when absent for non-CID */
    out->encoding = 0;
    out->charstrings = -1;
    out->private_size = -1;
    out->private_offset = -1;
    out->fdarray = -1;
    out->fdselect = -1;
    out->vstore = -1;
    out->charstring_type = 2;

    for (i = 0u; i < raw->count; i++) {
        const robo_font_cff_dict_entry_t *e = &raw->entries[i];
        switch (e->op) {
        case ROBO_CFF_OP_CHARSET:
            if (e->argc >= 1u) {
                out->charset = arg_i(e, 0);
            }
            break;
        case ROBO_CFF_OP_ENCODING:
            if (e->argc >= 1u) {
                out->encoding = arg_i(e, 0);
            }
            break;
        case ROBO_CFF_OP_CHARSTRINGS:
            if (e->argc >= 1u) {
                out->charstrings = arg_i(e, 0);
            }
            break;
        case ROBO_CFF_OP_PRIVATE:
            if (e->argc >= 2u) {
                out->private_size = arg_i(e, 0);
                out->private_offset = arg_i(e, 1);
            }
            break;
        case ROBO_CFF_OP_FDARRAY:
            if (e->argc >= 1u) {
                out->fdarray = arg_i(e, 0);
            }
            break;
        case ROBO_CFF_OP_FDSELECT:
            if (e->argc >= 1u) {
                out->fdselect = arg_i(e, 0);
            }
            break;
        case ROBO_CFF_OP_VSTORE:
            if (e->argc >= 1u) {
                out->vstore = arg_i(e, 0);
            }
            break;
        case ROBO_CFF_OP_ROS:
            if (e->argc >= 3u) {
                out->is_cid = 1;
                out->ros_registry = arg_i(e, 0);
                out->ros_ordering = arg_i(e, 1);
                out->ros_supplement = arg_i(e, 2);
            }
            break;
        case ROBO_CFF_OP_CIDCOUNT:
            if (e->argc >= 1u) {
                out->cid_count = arg_i(e, 0);
            }
            break;
        case ROBO_CFF_OP_FONTMATRIX:
            if (e->argc >= 6u) {
                uint8_t k;
                for (k = 0u; k < 6u; k++) {
                    out->font_matrix[k] = arg_r(e, k);
                }
                out->has_font_matrix = 1;
            }
            break;
        case ROBO_CFF_OP_FONTBBOX:
            if (e->argc >= 4u) {
                out->font_bbox[0] = arg_i(e, 0);
                out->font_bbox[1] = arg_i(e, 1);
                out->font_bbox[2] = arg_i(e, 2);
                out->font_bbox[3] = arg_i(e, 3);
                out->has_font_bbox = 1;
            }
            break;
        case ROBO_CFF_OP_CHARSTRINGTYPE:
            if (e->argc >= 1u) {
                out->charstring_type = arg_i(e, 0);
            }
            break;
        default:
            break;
        }
    }
    return ROBO_OK;
}

robo_result_t robo_font_cff_private_dict_interpret(const robo_font_cff_dict_t *raw,
                                                   robo_font_cff_private_dict_t *out)
{
    uint32_t i;
    if (!raw || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    robo_font_cff_private_dict_clear(out);
    out->raw.count = raw->count;
    if (raw->count) {
        out->raw.entries = (robo_font_cff_dict_entry_t *)robo_calloc(
            raw->count, sizeof(robo_font_cff_dict_entry_t));
        if (!out->raw.entries) {
            return ROBO_OUT_OF_MEMORY;
        }
        memcpy(out->raw.entries, raw->entries,
               raw->count * sizeof(robo_font_cff_dict_entry_t));
    }
    out->subrs = -1;
    for (i = 0u; i < raw->count; i++) {
        const robo_font_cff_dict_entry_t *e = &raw->entries[i];
        if (e->op == ROBO_CFF_OP_SUBRS && e->argc >= 1u) {
            out->subrs = arg_i(e, 0);
        } else if (e->op == ROBO_CFF_OP_DEFAULTWIDTHX && e->argc >= 1u) {
            out->default_width_x = arg_r(e, 0);
            out->has_default_width = 1;
        } else if (e->op == ROBO_CFF_OP_NOMINALWIDTHX && e->argc >= 1u) {
            out->nominal_width_x = arg_r(e, 0);
            out->has_nominal_width = 1;
        }
    }
    return ROBO_OK;
}

robo_result_t robo_font_cff_font_dict_interpret(const robo_font_cff_dict_t *raw,
                                                robo_font_cff_font_dict_t *out)
{
    uint32_t i;
    if (!raw || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    robo_font_cff_font_dict_clear(out);
    out->raw.count = raw->count;
    if (raw->count) {
        out->raw.entries = (robo_font_cff_dict_entry_t *)robo_calloc(
            raw->count, sizeof(robo_font_cff_dict_entry_t));
        if (!out->raw.entries) {
            return ROBO_OUT_OF_MEMORY;
        }
        memcpy(out->raw.entries, raw->entries,
               raw->count * sizeof(robo_font_cff_dict_entry_t));
    }
    out->private_size = -1;
    out->private_offset = -1;
    out->font_name_sid = -1;
    for (i = 0u; i < raw->count; i++) {
        const robo_font_cff_dict_entry_t *e = &raw->entries[i];
        if (e->op == ROBO_CFF_OP_PRIVATE && e->argc >= 2u) {
            out->private_size = arg_i(e, 0);
            out->private_offset = arg_i(e, 1);
        } else if (e->op == ROBO_CFF_OP_FONTNAME && e->argc >= 1u) {
            out->font_name_sid = arg_i(e, 0);
        }
    }
    return ROBO_OK;
}
