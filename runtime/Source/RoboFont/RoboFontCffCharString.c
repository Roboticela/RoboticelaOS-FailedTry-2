/*
 * Type 2 CharString validation / IR (Goal 5.5).
 * Does not produce final glyph outlines (Goal 5.8).
 */

#include "RoboFont/RoboFontCff.h"

#include "RoboMem/RoboMem.h"

#include <string.h>

#ifndef INT32_MIN
#define INT32_MIN (-2147483647 - 1)
#endif

void robo_font_cff_charstring_clear(robo_font_cff_charstring_t *cs)
{
    if (!cs) {
        return;
    }
    robo_free(cs->ops);
    memset(cs, 0, sizeof(*cs));
    cs->width = INT32_MIN;
}

typedef struct {
    int32_t stack[ROBO_FONT_CFF_MAX_OPERAND_STACK];
    uint8_t sp;
    uint32_t stem_hints;
    uint32_t depth;
    uint32_t calls;
    int in_header; /* width / stem phase */
    robo_font_cff_charstring_t *out;
    uint32_t op_cap;
    uint32_t flags;
} cs_vm_t;

static robo_result_t cs_emit(cs_vm_t *vm, uint16_t op, uint8_t argc,
                             const int32_t *args, uint8_t mask_bytes,
                             const uint8_t *mask)
{
    robo_font_cff_cs_op_rec_t *rec;
    if (vm->out->op_count >= ROBO_FONT_CFF_MAX_CS_OPS) {
        return ROBO_PARSE_ERROR;
    }
    if (vm->out->op_count + 1u > vm->op_cap) {
        uint32_t ncap = vm->op_cap ? vm->op_cap * 2u : 32u;
        robo_font_cff_cs_op_rec_t *ne =
            (robo_font_cff_cs_op_rec_t *)robo_calloc(ncap, sizeof(*ne));
        if (!ne) {
            return ROBO_OUT_OF_MEMORY;
        }
        if (vm->out->ops) {
            memcpy(ne, vm->out->ops, vm->out->op_count * sizeof(*ne));
            robo_free(vm->out->ops);
        }
        vm->out->ops = ne;
        vm->op_cap = ncap;
    }
    rec = &vm->out->ops[vm->out->op_count++];
    memset(rec, 0, sizeof(*rec));
    rec->op = op;
    rec->argc = argc;
    if (argc && args) {
        memcpy(rec->args, args, (size_t)argc * sizeof(int32_t));
    }
    rec->mask_bytes = mask_bytes;
    if (mask_bytes && mask) {
        memcpy(rec->mask, mask, mask_bytes);
    }
    return ROBO_OK;
}

static robo_result_t cs_push(cs_vm_t *vm, int32_t v)
{
    if (vm->sp >= ROBO_FONT_CFF_MAX_OPERAND_STACK) {
        return ROBO_PARSE_ERROR;
    }
    vm->stack[vm->sp++] = v;
    return ROBO_OK;
}

static robo_result_t cs_pop(cs_vm_t *vm, int32_t *out)
{
    if (vm->sp == 0u) {
        return ROBO_PARSE_ERROR;
    }
    *out = vm->stack[--vm->sp];
    return ROBO_OK;
}

static robo_result_t clear_stack_emit(cs_vm_t *vm, uint16_t op)
{
    robo_result_t res =
        cs_emit(vm, op, vm->sp, vm->stack, 0u, NULL);
    vm->sp = 0u;
    return res;
}

static robo_result_t parse_stream(cs_vm_t *vm,
                                  robo_font_span_t bytes,
                                  const robo_font_cff_subr_ctx_t *subrs,
                                  double default_width,
                                  double nominal_width,
                                  int has_nominal_width);

static robo_result_t call_subr(cs_vm_t *vm,
                               const robo_font_cff_subr_ctx_t *subrs,
                               int is_global,
                               double default_width,
                               double nominal_width,
                               int has_nominal_width)
{
    int32_t num;
    uint32_t bias;
    uint32_t idx;
    const robo_font_cff_index_t *ix;
    robo_font_span_t sub;
    robo_result_t res;

    if (cs_pop(vm, &num) != ROBO_OK) {
        return ROBO_PARSE_ERROR;
    }
    if (!subrs) {
        return ROBO_PARSE_ERROR;
    }
    ix = is_global ? subrs->global_subrs : subrs->local_subrs;
    bias = is_global ? subrs->global_bias : subrs->local_bias;
    if (!ix) {
        return ROBO_PARSE_ERROR;
    }
    if (num < 0 || (uint32_t)num + bias < bias) {
        /* allow negative after adding bias carefully */
    }
    {
        int64_t t = (int64_t)num + (int64_t)bias;
        if (t < 0 || (uint64_t)t >= (uint64_t)ix->count) {
            return ROBO_PARSE_ERROR;
        }
        idx = (uint32_t)t;
    }
    if (vm->depth >= ROBO_FONT_CFF_MAX_SUBR_DEPTH ||
        vm->calls >= ROBO_FONT_CFF_MAX_SUBR_CALLS) {
        return ROBO_PARSE_ERROR;
    }
    if (robo_font_cff_index_get(ix, idx, &sub) != ROBO_OK) {
        return ROBO_PARSE_ERROR;
    }
    vm->depth++;
    vm->calls++;
    res = parse_stream(vm, sub, subrs, default_width, nominal_width,
                       has_nominal_width);
    vm->depth--;
    return res;
}

static uint8_t mask_byte_count(uint32_t stems)
{
    return (uint8_t)((stems + 7u) / 8u);
}

static robo_result_t parse_stream(cs_vm_t *vm,
                                  robo_font_span_t bytes,
                                  const robo_font_cff_subr_ctx_t *subrs,
                                  double default_width,
                                  double nominal_width,
                                  int has_nominal_width)
{
    robo_font_reader_t r;
    int ended = 0;

    if (bytes.size > ROBO_FONT_CFF_MAX_CHARSTRING_LEN) {
        return ROBO_PARSE_ERROR;
    }
    robo_font_reader_init_span(&r, bytes);

    while (!ended && robo_font_reader_remaining(&r) > 0u) {
        uint8_t b0;
        if (robo_font_read_u8(&r, &b0) != ROBO_OK) {
            return ROBO_PARSE_ERROR;
        }

        /* numbers */
        if (b0 == 28u) {
            int16_t s;
            if (robo_font_read_i16be(&r, &s) != ROBO_OK) {
                return ROBO_PARSE_ERROR;
            }
            if (cs_push(vm, (int32_t)s) != ROBO_OK) {
                return ROBO_PARSE_ERROR;
            }
            continue;
        }
        if (b0 == 255u) {
            int32_t v;
            if (robo_font_read_i32be(&r, &v) != ROBO_OK) {
                return ROBO_PARSE_ERROR;
            }
            /* 16.16 fixed → integer truncation for IR */
            if (cs_push(vm, v >> 16) != ROBO_OK) {
                return ROBO_PARSE_ERROR;
            }
            continue;
        }
        if (b0 >= 32u && b0 <= 246u) {
            if (cs_push(vm, (int32_t)b0 - 139) != ROBO_OK) {
                return ROBO_PARSE_ERROR;
            }
            continue;
        }
        if (b0 >= 247u && b0 <= 250u) {
            uint8_t b1;
            if (robo_font_read_u8(&r, &b1) != ROBO_OK) {
                return ROBO_PARSE_ERROR;
            }
            if (cs_push(vm, ((int32_t)b0 - 247) * 256 + (int32_t)b1 + 108) !=
                ROBO_OK) {
                return ROBO_PARSE_ERROR;
            }
            continue;
        }
        if (b0 >= 251u && b0 <= 254u) {
            uint8_t b1;
            if (robo_font_read_u8(&r, &b1) != ROBO_OK) {
                return ROBO_PARSE_ERROR;
            }
            if (cs_push(vm, -((int32_t)b0 - 251) * 256 - (int32_t)b1 - 108) !=
                ROBO_OK) {
                return ROBO_PARSE_ERROR;
            }
            continue;
        }

        /* operators */
        switch (b0) {
        case 1:  /* hstem */
        case 3:  /* vstem */
        case 18: /* hstemhm */
        case 23: /* vstemhm */
            if (vm->in_header && (vm->sp & 1u) == 1u) {
                /* odd count → width */
                int32_t w = vm->stack[0];
                uint8_t i;
                if (has_nominal_width) {
                    vm->out->width = (int32_t)(nominal_width + (double)w);
                } else {
                    vm->out->width = w;
                }
                vm->out->has_width = 1;
                for (i = 1u; i < vm->sp; i++) {
                    vm->stack[i - 1u] = vm->stack[i];
                }
                vm->sp--;
            }
            vm->stem_hints += (uint32_t)(vm->sp / 2u);
            vm->out->stem_hint_count = vm->stem_hints;
            if (clear_stack_emit(vm, b0) != ROBO_OK) {
                return ROBO_PARSE_ERROR;
            }
            break;

        case 19: /* hintmask */
        case 20: /* cntrmask */ {
            uint8_t mb;
            uint8_t mask[32];
            uint8_t i;
            /* optional vstem args before mask */
            if (vm->sp > 0u) {
                if (vm->in_header && (vm->sp & 1u) == 1u) {
                    int32_t w = vm->stack[0];
                    uint8_t j;
                    if (has_nominal_width) {
                        vm->out->width = (int32_t)(nominal_width + (double)w);
                    } else {
                        vm->out->width = w;
                    }
                    vm->out->has_width = 1;
                    for (j = 1u; j < vm->sp; j++) {
                        vm->stack[j - 1u] = vm->stack[j];
                    }
                    vm->sp--;
                }
                vm->stem_hints += (uint32_t)(vm->sp / 2u);
                vm->out->stem_hint_count = vm->stem_hints;
                if (clear_stack_emit(vm, 23) != ROBO_OK) { /* implied vstemhm */
                    return ROBO_PARSE_ERROR;
                }
            }
            mb = mask_byte_count(vm->stem_hints);
            if (mb > sizeof(mask)) {
                return ROBO_PARSE_ERROR;
            }
            for (i = 0u; i < mb; i++) {
                if (robo_font_read_u8(&r, &mask[i]) != ROBO_OK) {
                    return ROBO_PARSE_ERROR;
                }
            }
            if (cs_emit(vm, b0, 0u, NULL, mb, mask) != ROBO_OK) {
                return ROBO_PARSE_ERROR;
            }
            vm->in_header = 0;
            break;
        }

        case 4:  /* vmoveto */
        case 21: /* rmoveto */
        case 22: /* hmoveto */
            if (vm->in_header &&
                ((b0 == 21 && vm->sp == 3u) ||
                 ((b0 == 4 || b0 == 22) && vm->sp == 2u))) {
                int32_t w = vm->stack[0];
                uint8_t i;
                if (has_nominal_width) {
                    vm->out->width = (int32_t)(nominal_width + (double)w);
                } else {
                    vm->out->width = w;
                }
                vm->out->has_width = 1;
                for (i = 1u; i < vm->sp; i++) {
                    vm->stack[i - 1u] = vm->stack[i];
                }
                vm->sp--;
            } else if (vm->in_header && !vm->out->has_width) {
                vm->out->width = (int32_t)default_width;
                vm->out->has_width = 1;
            }
            vm->in_header = 0;
            if (clear_stack_emit(vm, b0) != ROBO_OK) {
                return ROBO_PARSE_ERROR;
            }
            break;

        case 5:  /* rlineto */
        case 6:  /* hlineto */
        case 7:  /* vlineto */
        case 8:  /* rrcurveto */
        case 24: /* rcurveline */
        case 25: /* rlinecurve */
        case 26: /* vvcurveto */
        case 27: /* hhcurveto */
        case 30: /* vhcurveto */
        case 31: /* hvcurveto */
            vm->in_header = 0;
            if (clear_stack_emit(vm, b0) != ROBO_OK) {
                return ROBO_PARSE_ERROR;
            }
            break;

        case 10: /* callsubr */
            if (call_subr(vm, subrs, 0, default_width, nominal_width,
                          has_nominal_width) != ROBO_OK) {
                return ROBO_PARSE_ERROR;
            }
            break;
        case 29: /* callgsubr */
            if (call_subr(vm, subrs, 1, default_width, nominal_width,
                          has_nominal_width) != ROBO_OK) {
                return ROBO_PARSE_ERROR;
            }
            break;
        case 11: /* return */
            return ROBO_OK;

        case 14: /* endchar */
            if (vm->in_header && vm->sp == 1u) {
                int32_t w = vm->stack[0];
                if (has_nominal_width) {
                    vm->out->width = (int32_t)(nominal_width + (double)w);
                } else {
                    vm->out->width = w;
                }
                vm->out->has_width = 1;
                vm->sp = 0u;
            } else if (vm->in_header && !vm->out->has_width) {
                vm->out->width = (int32_t)default_width;
                vm->out->has_width = 1;
            }
            if (cs_emit(vm, 14, vm->sp, vm->stack, 0u, NULL) != ROBO_OK) {
                return ROBO_PARSE_ERROR;
            }
            vm->sp = 0u;
            ended = 1;
            break;

        case 12: { /* escape */
            uint8_t b1;
            uint16_t op;
            if (robo_font_read_u8(&r, &b1) != ROBO_OK) {
                return ROBO_PARSE_ERROR;
            }
            op = (uint16_t)(1200u + b1);
            /* flex family clears stack as path ops */
            if (b1 == 34u || b1 == 35u || b1 == 36u || b1 == 37u) {
                vm->in_header = 0;
                if (clear_stack_emit(vm, op) != ROBO_OK) {
                    return ROBO_PARSE_ERROR;
                }
                break;
            }
            /* arithmetic / stack ops — execute for validation */
            switch (b1) {
            case 9: { /* abs */
                int32_t a;
                if (cs_pop(vm, &a) != ROBO_OK) {
                    return ROBO_PARSE_ERROR;
                }
                if (a < 0) {
                    a = -a;
                }
                if (cs_push(vm, a) != ROBO_OK) {
                    return ROBO_PARSE_ERROR;
                }
                break;
            }
            case 10: { /* add */
                int32_t a, b;
                if (cs_pop(vm, &b) != ROBO_OK || cs_pop(vm, &a) != ROBO_OK) {
                    return ROBO_PARSE_ERROR;
                }
                if (cs_push(vm, a + b) != ROBO_OK) {
                    return ROBO_PARSE_ERROR;
                }
                break;
            }
            case 11: { /* sub */
                int32_t a, b;
                if (cs_pop(vm, &b) != ROBO_OK || cs_pop(vm, &a) != ROBO_OK) {
                    return ROBO_PARSE_ERROR;
                }
                if (cs_push(vm, a - b) != ROBO_OK) {
                    return ROBO_PARSE_ERROR;
                }
                break;
            }
            case 12: { /* div */
                int32_t a, b;
                if (cs_pop(vm, &b) != ROBO_OK || cs_pop(vm, &a) != ROBO_OK ||
                    b == 0) {
                    return ROBO_PARSE_ERROR;
                }
                if (cs_push(vm, a / b) != ROBO_OK) {
                    return ROBO_PARSE_ERROR;
                }
                break;
            }
            case 14: { /* neg */
                int32_t a;
                if (cs_pop(vm, &a) != ROBO_OK) {
                    return ROBO_PARSE_ERROR;
                }
                if (cs_push(vm, -a) != ROBO_OK) {
                    return ROBO_PARSE_ERROR;
                }
                break;
            }
            case 18: /* drop */
                if (vm->sp == 0u) {
                    return ROBO_PARSE_ERROR;
                }
                vm->sp--;
                break;
            case 27: { /* dup */
                int32_t a;
                if (vm->sp == 0u) {
                    return ROBO_PARSE_ERROR;
                }
                a = vm->stack[vm->sp - 1u];
                if (cs_push(vm, a) != ROBO_OK) {
                    return ROBO_PARSE_ERROR;
                }
                break;
            }
            case 28: { /* exch */
                int32_t a, b;
                if (cs_pop(vm, &b) != ROBO_OK || cs_pop(vm, &a) != ROBO_OK) {
                    return ROBO_PARSE_ERROR;
                }
                if (cs_push(vm, b) != ROBO_OK || cs_push(vm, a) != ROBO_OK) {
                    return ROBO_PARSE_ERROR;
                }
                break;
            }
            case 24: { /* mul */
                int32_t a, b;
                if (cs_pop(vm, &b) != ROBO_OK || cs_pop(vm, &a) != ROBO_OK) {
                    return ROBO_PARSE_ERROR;
                }
                if (cs_push(vm, a * b) != ROBO_OK) {
                    return ROBO_PARSE_ERROR;
                }
                break;
            }
            default:
                /* unsupported escape — record and clear for flex-like safety */
                if (clear_stack_emit(vm, op) != ROBO_OK) {
                    return ROBO_PARSE_ERROR;
                }
                break;
            }
            break;
        }

        default:
            return ROBO_PARSE_ERROR; /* unknown / reserved */
        }
    }

    if (vm->depth == 0u && !ended) {
        if ((vm->flags & ROBO_FONT_CFF_CS_FLAGS_ALLOW_NO_ENDCHAR) != 0u) {
            return ROBO_OK; /* CFF2 may omit endchar */
        }
        /* top-level must end with endchar */
        return ROBO_PARSE_ERROR;
    }
    return ROBO_OK;
}

robo_result_t robo_font_cff_charstring_parse(robo_font_span_t cs_bytes,
                                             const robo_font_cff_subr_ctx_t *subrs,
                                             double default_width,
                                             double nominal_width,
                                             int has_nominal_width,
                                             robo_font_cff_charstring_t *out)
{
    return robo_font_cff_charstring_parse_ex(cs_bytes, subrs, default_width,
                                             nominal_width, has_nominal_width,
                                             ROBO_FONT_CFF_CS_FLAGS_NONE, out);
}

robo_result_t robo_font_cff_charstring_parse_ex(robo_font_span_t cs_bytes,
                                                const robo_font_cff_subr_ctx_t *subrs,
                                                double default_width,
                                                double nominal_width,
                                                int has_nominal_width,
                                                uint32_t flags,
                                                robo_font_cff_charstring_t *out)
{
    cs_vm_t vm;
    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    robo_font_cff_charstring_clear(out);
    memset(&vm, 0, sizeof(vm));
    vm.out = out;
    vm.in_header = 1;
    vm.flags = flags;
    out->width = INT32_MIN;
    return parse_stream(&vm, cs_bytes, subrs, default_width, nominal_width,
                        has_nominal_width);
}
