/*
 * CFF DICT encoding and Top/Private interpretation (Goal 5.5).
 */

#include "RoboFont/RoboFontCff.h"

#include "RoboMem/RoboMem.h"

#include <string.h>

void FontCffDictClear(FontCffDict *d)
{
    if (!d) {
        return;
    }
    RoboFree(d->entries);
    memset(d, 0, sizeof(*d));
}

void FontCffTopDictClear(FontCffTopDict *t)
{
    if (!t) {
        return;
    }
    FontCffDictClear(&t->raw);
    memset(t, 0, sizeof(*t));
    t->charset = -1;
    t->encoding = -1;
    t->charstrings = -1;
    t->privateSize = -1;
    t->privateOffset = -1;
    t->fdarray = -1;
    t->fdselect = -1;
    t->vstore = -1;
    t->charstringType = 2;
}

void FontCffPrivateDictClear(FontCffPrivateDict *p)
{
    if (!p) {
        return;
    }
    FontCffDictClear(&p->raw);
    memset(p, 0, sizeof(*p));
    p->subrs = -1;
}

void FontCffFontDictClear(FontCffFontDict *f)
{
    if (!f) {
        return;
    }
    FontCffDictClear(&f->raw);
    memset(f, 0, sizeof(*f));
    f->privateSize = -1;
    f->privateOffset = -1;
    f->fontNameSid = -1;
}

static Result DecodeReal(FontReader *r, double *out)
{
    char buf[64];
    size_t n = 0u;
    int done = 0;

    while (!done) {
        uint8_t b;
        uint8_t nibbles[2];
        int ni;
        if (FontReadU8(r, &b) != RoboOk) {
            return RoboParseError;
        }
        nibbles[0] = (uint8_t)(b >> 4);
        nibbles[1] = (uint8_t)(b & 0x0Fu);
        for (ni = 0; ni < 2; ni++) {
            uint8_t nb = nibbles[ni];
            if (nb <= 9u) {
                if (n + 1u >= sizeof(buf)) {
                    return RoboParseError;
                }
                buf[n++] = (char)('0' + nb);
            } else if (nb == 0xAu) {
                if (n + 1u >= sizeof(buf)) {
                    return RoboParseError;
                }
                buf[n++] = '.';
            } else if (nb == 0xBu) {
                if (n + 1u >= sizeof(buf)) {
                    return RoboParseError;
                }
                buf[n++] = 'E';
            } else if (nb == 0xCu) {
                if (n + 2u >= sizeof(buf)) {
                    return RoboParseError;
                }
                buf[n++] = 'E';
                buf[n++] = '-';
            } else if (nb == 0xDu) {
                /* reserved */
                return RoboParseError;
            } else if (nb == 0xEu) {
                if (n + 1u >= sizeof(buf)) {
                    return RoboParseError;
                }
                buf[n++] = '-';
            } else if (nb == 0xFu) {
                done = 1;
                break;
            } else {
                return RoboParseError;
            }
        }
    }
    buf[n] = '\0';
    if (n == 0u) {
        return RoboParseError;
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
            int expNeg = 0;
            int inFrac = 0;
            int inExp = 0;
            if (*p == '-') {
                neg = 1;
                p++;
            }
            for (; *p; p++) {
                if (*p == '.') {
                    inFrac = 1;
                    continue;
                }
                if (*p == 'E' || *p == 'e') {
                    inExp = 1;
                    inFrac = 0;
                    if (p[1] == '-') {
                        expNeg = 1;
                        p++;
                    }
                    continue;
                }
                if (*p < '0' || *p > '9') {
                    return RoboParseError;
                }
                if (inExp) {
                    exp = exp * 10 + (*p - '0');
                } else if (inFrac) {
                    frac += place * (double)(*p - '0');
                    place *= 0.1;
                } else {
                    v = v * 10.0 + (double)(*p - '0');
                }
            }
            v = v + frac;
            if (expNeg) {
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
    return RoboOk;
}

static Result PushNum(FontCffNumber *stack,
                              uint8_t *sp,
                              FontCffNumber num)
{
    if (*sp >= RoboFontCffMaxDictOperands) {
        return RoboParseError;
    }
    stack[(*sp)++] = num;
    return RoboOk;
}

Result FontCffDictParse(FontSpan dictBytes,
                                       FontCffDict *out)
{
    FontReader r;
    FontCffNumber stack[RoboFontCffMaxDictOperands];
    uint8_t sp = 0u;
    FontCffDictEntry *entries = NULL;
    uint32_t cap = 0u;
    uint32_t count = 0u;

    if (!out) {
        return RoboInvalidArgument;
    }
    FontCffDictClear(out);
    if (dictBytes.size == 0u) {
        return RoboOk;
    }
    if (dictBytes.size > (1u << 20)) {
        return RoboParseError;
    }

    FontReaderInitSpan(&r, dictBytes);

    while (FontReaderRemaining(&r) > 0u) {
        uint8_t b0;
        if (FontReadU8(&r, &b0) != RoboOk) {
            RoboFree(entries);
            return RoboParseError;
        }

        if (b0 >= 32u || b0 == 28u || b0 == 29u || b0 == 30u) {
            FontCffNumber num;
            memset(&num, 0, sizeof(num));
            if (b0 == 28u) {
                int16_t s;
                if (FontReadI16be(&r, &s) != RoboOk) {
                    RoboFree(entries);
                    return RoboParseError;
                }
                num.i = (int32_t)s;
                num.real = (double)s;
            } else if (b0 == 29u) {
                int32_t v;
                if (FontReadI32be(&r, &v) != RoboOk) {
                    RoboFree(entries);
                    return RoboParseError;
                }
                num.i = v;
                num.real = (double)v;
            } else if (b0 == 30u) {
                double rv;
                if (DecodeReal(&r, &rv) != RoboOk) {
                    RoboFree(entries);
                    return RoboParseError;
                }
                num.isReal = 1;
                num.real = rv;
                num.i = (int32_t)rv;
            } else if (b0 <= 246u) {
                int32_t v = (int32_t)b0 - 139;
                num.i = v;
                num.real = (double)v;
            } else if (b0 <= 250u) {
                uint8_t b1;
                int32_t v;
                if (FontReadU8(&r, &b1) != RoboOk) {
                    RoboFree(entries);
                    return RoboParseError;
                }
                v = ((int32_t)b0 - 247) * 256 + (int32_t)b1 + 108;
                num.i = v;
                num.real = (double)v;
            } else if (b0 <= 254u) {
                uint8_t b1;
                int32_t v;
                if (FontReadU8(&r, &b1) != RoboOk) {
                    RoboFree(entries);
                    return RoboParseError;
                }
                v = -((int32_t)b0 - 251) * 256 - (int32_t)b1 - 108;
                num.i = v;
                num.real = (double)v;
            } else {
                /* 255 reserved in DICT */
                RoboFree(entries);
                return RoboParseError;
            }
            if (PushNum(stack, &sp, num) != RoboOk) {
                RoboFree(entries);
                return RoboParseError;
            }
            continue;
        }

        /* operator */
        {
            uint16_t op = b0;
            FontCffDictEntry ent;
            if (b0 == 12u) {
                uint8_t b1;
                if (FontReadU8(&r, &b1) != RoboOk) {
                    RoboFree(entries);
                    return RoboParseError;
                }
                op = (uint16_t)(1200u + (uint16_t)b1);
            }
            if (count >= RoboFontCffMaxDictOps) {
                RoboFree(entries);
                return RoboParseError;
            }
            if (count + 1u > cap) {
                uint32_t ncap = cap ? cap * 2u : 16u;
                FontCffDictEntry *ne =
                    (FontCffDictEntry *)RoboCalloc(
                        ncap, sizeof(FontCffDictEntry));
                if (!ne) {
                    RoboFree(entries);
                    return RoboOutOfMemory;
                }
                if (entries) {
                    memcpy(ne, entries, count * sizeof(*entries));
                    RoboFree(entries);
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
        RoboFree(entries);
        return RoboParseError;
    }

    out->count = count;
    out->entries = entries;
    return RoboOk;
}

Result FontCffDictFind(const FontCffDict *d,
                                      uint16_t op,
                                      const FontCffDictEntry **out)
{
    uint32_t i;
    if (!d || !out) {
        return RoboInvalidArgument;
    }
    for (i = 0u; i < d->count; i++) {
        if (d->entries[i].op == op) {
            *out = &d->entries[i];
            return RoboOk;
        }
    }
    return RoboNotFound;
}

static int32_t ArgI(const FontCffDictEntry *e, uint8_t i)
{
    if (i >= e->argc) {
        return 0;
    }
    return e->args[i].i;
}

static double ArgR(const FontCffDictEntry *e, uint8_t i)
{
    if (i >= e->argc) {
        return 0.0;
    }
    return e->args[i].real;
}

Result FontCffTopDictInterpret(const FontCffDict *raw,
                                               FontCffTopDict *out)
{
    uint32_t i;
    if (!raw || !out) {
        return RoboInvalidArgument;
    }
    FontCffTopDictClear(out);
    /* shallow: keep raw by re-parsing is awkward; copy pointer ownership?
     * Caller owns raw separately. We duplicate entries into out->raw. */
    out->raw.count = raw->count;
    if (raw->count) {
        out->raw.entries = (FontCffDictEntry *)RoboCalloc(
            raw->count, sizeof(FontCffDictEntry));
        if (!out->raw.entries) {
            return RoboOutOfMemory;
        }
        memcpy(out->raw.entries, raw->entries,
               raw->count * sizeof(FontCffDictEntry));
    }

    out->charset = 0; /* predefined ISOAdobe default when absent for non-CID */
    out->encoding = 0;
    out->charstrings = -1;
    out->privateSize = -1;
    out->privateOffset = -1;
    out->fdarray = -1;
    out->fdselect = -1;
    out->vstore = -1;
    out->charstringType = 2;

    for (i = 0u; i < raw->count; i++) {
        const FontCffDictEntry *e = &raw->entries[i];
        switch (e->op) {
        case RoboCffOpCharset:
            if (e->argc >= 1u) {
                out->charset = ArgI(e, 0);
            }
            break;
        case RoboCffOpEncoding:
            if (e->argc >= 1u) {
                out->encoding = ArgI(e, 0);
            }
            break;
        case RoboCffOpCharstrings:
            if (e->argc >= 1u) {
                out->charstrings = ArgI(e, 0);
            }
            break;
        case RoboCffOpPrivate:
            if (e->argc >= 2u) {
                out->privateSize = ArgI(e, 0);
                out->privateOffset = ArgI(e, 1);
            }
            break;
        case RoboCffOpFdarray:
            if (e->argc >= 1u) {
                out->fdarray = ArgI(e, 0);
            }
            break;
        case RoboCffOpFdselect:
            if (e->argc >= 1u) {
                out->fdselect = ArgI(e, 0);
            }
            break;
        case RoboCffOpVstore:
            if (e->argc >= 1u) {
                out->vstore = ArgI(e, 0);
            }
            break;
        case RoboCffOpRos:
            if (e->argc >= 3u) {
                out->isCid = 1;
                out->rosRegistry = ArgI(e, 0);
                out->rosOrdering = ArgI(e, 1);
                out->rosSupplement = ArgI(e, 2);
            }
            break;
        case RoboCffOpCidcount:
            if (e->argc >= 1u) {
                out->cidCount = ArgI(e, 0);
            }
            break;
        case RoboCffOpFontmatrix:
            if (e->argc >= 6u) {
                uint8_t k;
                for (k = 0u; k < 6u; k++) {
                    out->fontMatrix[k] = ArgR(e, k);
                }
                out->hasFontMatrix = 1;
            }
            break;
        case RoboCffOpFontbbox:
            if (e->argc >= 4u) {
                out->fontBbox[0] = ArgI(e, 0);
                out->fontBbox[1] = ArgI(e, 1);
                out->fontBbox[2] = ArgI(e, 2);
                out->fontBbox[3] = ArgI(e, 3);
                out->hasFontBbox = 1;
            }
            break;
        case RoboCffOpCharstringtype:
            if (e->argc >= 1u) {
                out->charstringType = ArgI(e, 0);
            }
            break;
        default:
            break;
        }
    }
    return RoboOk;
}

Result FontCffPrivateDictInterpret(const FontCffDict *raw,
                                                   FontCffPrivateDict *out)
{
    uint32_t i;
    if (!raw || !out) {
        return RoboInvalidArgument;
    }
    FontCffPrivateDictClear(out);
    out->raw.count = raw->count;
    if (raw->count) {
        out->raw.entries = (FontCffDictEntry *)RoboCalloc(
            raw->count, sizeof(FontCffDictEntry));
        if (!out->raw.entries) {
            return RoboOutOfMemory;
        }
        memcpy(out->raw.entries, raw->entries,
               raw->count * sizeof(FontCffDictEntry));
    }
    out->subrs = -1;
    for (i = 0u; i < raw->count; i++) {
        const FontCffDictEntry *e = &raw->entries[i];
        if (e->op == RoboCffOpSubrs && e->argc >= 1u) {
            out->subrs = ArgI(e, 0);
        } else if (e->op == RoboCffOpDefaultwidthx && e->argc >= 1u) {
            out->defaultWidthX = ArgR(e, 0);
            out->hasDefaultWidth = 1;
        } else if (e->op == RoboCffOpNominalwidthx && e->argc >= 1u) {
            out->nominalWidthX = ArgR(e, 0);
            out->hasNominalWidth = 1;
        }
    }
    return RoboOk;
}

Result FontCffFontDictInterpret(const FontCffDict *raw,
                                                FontCffFontDict *out)
{
    uint32_t i;
    if (!raw || !out) {
        return RoboInvalidArgument;
    }
    FontCffFontDictClear(out);
    out->raw.count = raw->count;
    if (raw->count) {
        out->raw.entries = (FontCffDictEntry *)RoboCalloc(
            raw->count, sizeof(FontCffDictEntry));
        if (!out->raw.entries) {
            return RoboOutOfMemory;
        }
        memcpy(out->raw.entries, raw->entries,
               raw->count * sizeof(FontCffDictEntry));
    }
    out->privateSize = -1;
    out->privateOffset = -1;
    out->fontNameSid = -1;
    for (i = 0u; i < raw->count; i++) {
        const FontCffDictEntry *e = &raw->entries[i];
        if (e->op == RoboCffOpPrivate && e->argc >= 2u) {
            out->privateSize = ArgI(e, 0);
            out->privateOffset = ArgI(e, 1);
        } else if (e->op == RoboCffOpFontname && e->argc >= 1u) {
            out->fontNameSid = ArgI(e, 0);
        }
    }
    return RoboOk;
}
