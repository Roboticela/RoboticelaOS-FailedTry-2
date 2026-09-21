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

void FontCffCharstringClear(FontCffCharstring *cs)
{
    if (!cs) {
        return;
    }
    RoboFree(cs->ops);
    memset(cs, 0, sizeof(*cs));
    cs->width = INT32_MIN;
}

typedef struct {
    int32_t stack[RoboFontCffMaxOperandStack];
    uint8_t sp;
    uint32_t stemHints;
    uint32_t depth;
    uint32_t calls;
    int inHeader; /* width / stem phase */
    FontCffCharstring *out;
    uint32_t opCap;
    uint32_t flags;
} CsVm;

static Result CsEmit(CsVm *vm, uint16_t op, uint8_t argc,
                             const int32_t *args, uint8_t maskBytes,
                             const uint8_t *mask)
{
    FontCffCsOpRec *rec;
    if (vm->out->opCount >= RoboFontCffMaxCsOps) {
        return RoboParseError;
    }
    if (vm->out->opCount + 1u > vm->opCap) {
        uint32_t ncap = vm->opCap ? vm->opCap * 2u : 32u;
        FontCffCsOpRec *ne =
            (FontCffCsOpRec *)RoboCalloc(ncap, sizeof(*ne));
        if (!ne) {
            return RoboOutOfMemory;
        }
        if (vm->out->ops) {
            memcpy(ne, vm->out->ops, vm->out->opCount * sizeof(*ne));
            RoboFree(vm->out->ops);
        }
        vm->out->ops = ne;
        vm->opCap = ncap;
    }
    rec = &vm->out->ops[vm->out->opCount++];
    memset(rec, 0, sizeof(*rec));
    rec->op = op;
    rec->argc = argc;
    if (argc && args) {
        memcpy(rec->args, args, (size_t)argc * sizeof(int32_t));
    }
    rec->maskBytes = maskBytes;
    if (maskBytes && mask) {
        memcpy(rec->mask, mask, maskBytes);
    }
    return RoboOk;
}

static Result CsPush(CsVm *vm, int32_t v)
{
    if (vm->sp >= RoboFontCffMaxOperandStack) {
        return RoboParseError;
    }
    vm->stack[vm->sp++] = v;
    return RoboOk;
}

static Result CsPop(CsVm *vm, int32_t *out)
{
    if (vm->sp == 0u) {
        return RoboParseError;
    }
    *out = vm->stack[--vm->sp];
    return RoboOk;
}

static Result ClearStackEmit(CsVm *vm, uint16_t op)
{
    Result res =
        CsEmit(vm, op, vm->sp, vm->stack, 0u, NULL);
    vm->sp = 0u;
    return res;
}

static Result ParseStream(CsVm *vm,
                                  FontSpan bytes,
                                  const FontCffSubrCtx *subrs,
                                  double defaultWidth,
                                  double nominalWidth,
                                  int hasNominalWidth);

static Result CallSubr(CsVm *vm,
                               const FontCffSubrCtx *subrs,
                               int isGlobal,
                               double defaultWidth,
                               double nominalWidth,
                               int hasNominalWidth)
{
    int32_t num;
    uint32_t bias;
    uint32_t idx;
    const FontCffIndex *ix;
    FontSpan sub;
    Result res;

    if (CsPop(vm, &num) != RoboOk) {
        return RoboParseError;
    }
    if (!subrs) {
        return RoboParseError;
    }
    ix = isGlobal ? subrs->globalSubrs : subrs->localSubrs;
    bias = isGlobal ? subrs->globalBias : subrs->localBias;
    if (!ix) {
        return RoboParseError;
    }
    if (num < 0 || (uint32_t)num + bias < bias) {
        /* allow negative after adding bias carefully */
    }
    {
        int64_t t = (int64_t)num + (int64_t)bias;
        if (t < 0 || (uint64_t)t >= (uint64_t)ix->count) {
            return RoboParseError;
        }
        idx = (uint32_t)t;
    }
    if (vm->depth >= RoboFontCffMaxSubrDepth ||
        vm->calls >= RoboFontCffMaxSubrCalls) {
        return RoboParseError;
    }
    if (FontCffIndexGet(ix, idx, &sub) != RoboOk) {
        return RoboParseError;
    }
    vm->depth++;
    vm->calls++;
    res = ParseStream(vm, sub, subrs, defaultWidth, nominalWidth,
                       hasNominalWidth);
    vm->depth--;
    return res;
}

static uint8_t MaskByteCount(uint32_t stems)
{
    return (uint8_t)((stems + 7u) / 8u);
}

static Result ParseStream(CsVm *vm,
                                  FontSpan bytes,
                                  const FontCffSubrCtx *subrs,
                                  double defaultWidth,
                                  double nominalWidth,
                                  int hasNominalWidth)
{
    FontReader r;
    int ended = 0;

    if (bytes.size > RoboFontCffMaxCharstringLen) {
        return RoboParseError;
    }
    FontReaderInitSpan(&r, bytes);

    while (!ended && FontReaderRemaining(&r) > 0u) {
        uint8_t b0;
        if (FontReadU8(&r, &b0) != RoboOk) {
            return RoboParseError;
        }

        /* numbers */
        if (b0 == 28u) {
            int16_t s;
            if (FontReadI16be(&r, &s) != RoboOk) {
                return RoboParseError;
            }
            if (CsPush(vm, (int32_t)s) != RoboOk) {
                return RoboParseError;
            }
            continue;
        }
        if (b0 == 255u) {
            int32_t v;
            if (FontReadI32be(&r, &v) != RoboOk) {
                return RoboParseError;
            }
            /* 16.16 fixed → integer truncation for IR */
            if (CsPush(vm, v >> 16) != RoboOk) {
                return RoboParseError;
            }
            continue;
        }
        if (b0 >= 32u && b0 <= 246u) {
            if (CsPush(vm, (int32_t)b0 - 139) != RoboOk) {
                return RoboParseError;
            }
            continue;
        }
        if (b0 >= 247u && b0 <= 250u) {
            uint8_t b1;
            if (FontReadU8(&r, &b1) != RoboOk) {
                return RoboParseError;
            }
            if (CsPush(vm, ((int32_t)b0 - 247) * 256 + (int32_t)b1 + 108) !=
                RoboOk) {
                return RoboParseError;
            }
            continue;
        }
        if (b0 >= 251u && b0 <= 254u) {
            uint8_t b1;
            if (FontReadU8(&r, &b1) != RoboOk) {
                return RoboParseError;
            }
            if (CsPush(vm, -((int32_t)b0 - 251) * 256 - (int32_t)b1 - 108) !=
                RoboOk) {
                return RoboParseError;
            }
            continue;
        }

        /* operators */
        switch (b0) {
        case 1:  /* hstem */
        case 3:  /* vstem */
        case 18: /* hstemhm */
        case 23: /* vstemhm */
            if (vm->inHeader && (vm->sp & 1u) == 1u) {
                /* odd count → width */
                int32_t w = vm->stack[0];
                uint8_t i;
                if (hasNominalWidth) {
                    vm->out->width = (int32_t)(nominalWidth + (double)w);
                } else {
                    vm->out->width = w;
                }
                vm->out->hasWidth = 1;
                for (i = 1u; i < vm->sp; i++) {
                    vm->stack[i - 1u] = vm->stack[i];
                }
                vm->sp--;
            }
            vm->stemHints += (uint32_t)(vm->sp / 2u);
            vm->out->stemHintCount = vm->stemHints;
            if (ClearStackEmit(vm, b0) != RoboOk) {
                return RoboParseError;
            }
            break;

        case 19: /* hintmask */
        case 20: /* cntrmask */ {
            uint8_t mb;
            uint8_t mask[32];
            uint8_t i;
            /* optional vstem args before mask */
            if (vm->sp > 0u) {
                if (vm->inHeader && (vm->sp & 1u) == 1u) {
                    int32_t w = vm->stack[0];
                    uint8_t j;
                    if (hasNominalWidth) {
                        vm->out->width = (int32_t)(nominalWidth + (double)w);
                    } else {
                        vm->out->width = w;
                    }
                    vm->out->hasWidth = 1;
                    for (j = 1u; j < vm->sp; j++) {
                        vm->stack[j - 1u] = vm->stack[j];
                    }
                    vm->sp--;
                }
                vm->stemHints += (uint32_t)(vm->sp / 2u);
                vm->out->stemHintCount = vm->stemHints;
                if (ClearStackEmit(vm, 23) != RoboOk) { /* implied vstemhm */
                    return RoboParseError;
                }
            }
            mb = MaskByteCount(vm->stemHints);
            if (mb > sizeof(mask)) {
                return RoboParseError;
            }
            for (i = 0u; i < mb; i++) {
                if (FontReadU8(&r, &mask[i]) != RoboOk) {
                    return RoboParseError;
                }
            }
            if (CsEmit(vm, b0, 0u, NULL, mb, mask) != RoboOk) {
                return RoboParseError;
            }
            vm->inHeader = 0;
            break;
        }

        case 4:  /* vmoveto */
        case 21: /* rmoveto */
        case 22: /* hmoveto */
            if (vm->inHeader &&
                ((b0 == 21 && vm->sp == 3u) ||
                 ((b0 == 4 || b0 == 22) && vm->sp == 2u))) {
                int32_t w = vm->stack[0];
                uint8_t i;
                if (hasNominalWidth) {
                    vm->out->width = (int32_t)(nominalWidth + (double)w);
                } else {
                    vm->out->width = w;
                }
                vm->out->hasWidth = 1;
                for (i = 1u; i < vm->sp; i++) {
                    vm->stack[i - 1u] = vm->stack[i];
                }
                vm->sp--;
            } else if (vm->inHeader && !vm->out->hasWidth) {
                vm->out->width = (int32_t)defaultWidth;
                vm->out->hasWidth = 1;
            }
            vm->inHeader = 0;
            if (ClearStackEmit(vm, b0) != RoboOk) {
                return RoboParseError;
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
            vm->inHeader = 0;
            if (ClearStackEmit(vm, b0) != RoboOk) {
                return RoboParseError;
            }
            break;

        case 10: /* callsubr */
            if (CallSubr(vm, subrs, 0, defaultWidth, nominalWidth,
                          hasNominalWidth) != RoboOk) {
                return RoboParseError;
            }
            break;
        case 29: /* callgsubr */
            if (CallSubr(vm, subrs, 1, defaultWidth, nominalWidth,
                          hasNominalWidth) != RoboOk) {
                return RoboParseError;
            }
            break;
        case 11: /* return */
            return RoboOk;

        case 14: /* endchar */
            if (vm->inHeader && vm->sp == 1u) {
                int32_t w = vm->stack[0];
                if (hasNominalWidth) {
                    vm->out->width = (int32_t)(nominalWidth + (double)w);
                } else {
                    vm->out->width = w;
                }
                vm->out->hasWidth = 1;
                vm->sp = 0u;
            } else if (vm->inHeader && !vm->out->hasWidth) {
                vm->out->width = (int32_t)defaultWidth;
                vm->out->hasWidth = 1;
            }
            if (CsEmit(vm, 14, vm->sp, vm->stack, 0u, NULL) != RoboOk) {
                return RoboParseError;
            }
            vm->sp = 0u;
            ended = 1;
            break;

        case 12: { /* escape */
            uint8_t b1;
            uint16_t op;
            if (FontReadU8(&r, &b1) != RoboOk) {
                return RoboParseError;
            }
            op = (uint16_t)(1200u + b1);
            /* flex family clears stack as path ops */
            if (b1 == 34u || b1 == 35u || b1 == 36u || b1 == 37u) {
                vm->inHeader = 0;
                if (ClearStackEmit(vm, op) != RoboOk) {
                    return RoboParseError;
                }
                break;
            }
            /* arithmetic / stack ops — execute for validation */
            switch (b1) {
            case 9: { /* abs */
                int32_t a;
                if (CsPop(vm, &a) != RoboOk) {
                    return RoboParseError;
                }
                if (a < 0) {
                    a = -a;
                }
                if (CsPush(vm, a) != RoboOk) {
                    return RoboParseError;
                }
                break;
            }
            case 10: { /* add */
                int32_t a, b;
                if (CsPop(vm, &b) != RoboOk || CsPop(vm, &a) != RoboOk) {
                    return RoboParseError;
                }
                if (CsPush(vm, a + b) != RoboOk) {
                    return RoboParseError;
                }
                break;
            }
            case 11: { /* sub */
                int32_t a, b;
                if (CsPop(vm, &b) != RoboOk || CsPop(vm, &a) != RoboOk) {
                    return RoboParseError;
                }
                if (CsPush(vm, a - b) != RoboOk) {
                    return RoboParseError;
                }
                break;
            }
            case 12: { /* div */
                int32_t a, b;
                if (CsPop(vm, &b) != RoboOk || CsPop(vm, &a) != RoboOk ||
                    b == 0) {
                    return RoboParseError;
                }
                if (CsPush(vm, a / b) != RoboOk) {
                    return RoboParseError;
                }
                break;
            }
            case 14: { /* neg */
                int32_t a;
                if (CsPop(vm, &a) != RoboOk) {
                    return RoboParseError;
                }
                if (CsPush(vm, -a) != RoboOk) {
                    return RoboParseError;
                }
                break;
            }
            case 18: /* drop */
                if (vm->sp == 0u) {
                    return RoboParseError;
                }
                vm->sp--;
                break;
            case 27: { /* dup */
                int32_t a;
                if (vm->sp == 0u) {
                    return RoboParseError;
                }
                a = vm->stack[vm->sp - 1u];
                if (CsPush(vm, a) != RoboOk) {
                    return RoboParseError;
                }
                break;
            }
            case 28: { /* exch */
                int32_t a, b;
                if (CsPop(vm, &b) != RoboOk || CsPop(vm, &a) != RoboOk) {
                    return RoboParseError;
                }
                if (CsPush(vm, b) != RoboOk || CsPush(vm, a) != RoboOk) {
                    return RoboParseError;
                }
                break;
            }
            case 24: { /* mul */
                int32_t a, b;
                if (CsPop(vm, &b) != RoboOk || CsPop(vm, &a) != RoboOk) {
                    return RoboParseError;
                }
                if (CsPush(vm, a * b) != RoboOk) {
                    return RoboParseError;
                }
                break;
            }
            default:
                /* unsupported escape — record and clear for flex-like safety */
                if (ClearStackEmit(vm, op) != RoboOk) {
                    return RoboParseError;
                }
                break;
            }
            break;
        }

        default:
            return RoboParseError; /* unknown / reserved */
        }
    }

    if (vm->depth == 0u && !ended) {
        if ((vm->flags & RoboFontCffCsFlagsAllowNoEndchar) != 0u) {
            return RoboOk; /* CFF2 may omit endchar */
        }
        /* top-level must end with endchar */
        return RoboParseError;
    }
    return RoboOk;
}

Result FontCffCharstringParse(FontSpan csBytes,
                                             const FontCffSubrCtx *subrs,
                                             double defaultWidth,
                                             double nominalWidth,
                                             int hasNominalWidth,
                                             FontCffCharstring *out)
{
    return FontCffCharstringParseEx(csBytes, subrs, defaultWidth,
                                             nominalWidth, hasNominalWidth,
                                             RoboFontCffCsFlagsNone, out);
}

Result FontCffCharstringParseEx(FontSpan csBytes,
                                                const FontCffSubrCtx *subrs,
                                                double defaultWidth,
                                                double nominalWidth,
                                                int hasNominalWidth,
                                                uint32_t flags,
                                                FontCffCharstring *out)
{
    CsVm vm;
    if (!out) {
        return RoboInvalidArgument;
    }
    FontCffCharstringClear(out);
    memset(&vm, 0, sizeof(vm));
    vm.out = out;
    vm.inHeader = 1;
    vm.flags = flags;
    out->width = INT32_MIN;
    return ParseStream(&vm, csBytes, subrs, defaultWidth, nominalWidth,
                        hasNominalWidth);
}
