/*
 * RoboFont emoji-font suite (Goal 5.17).
 * Synthetic registry fixtures â€” no Chromium / system fonts.
 */

#include "RoboFont/RoboFont.h"
#include "RoboMem/RoboMem.h"
#include "RoboTest.h"

#include <stdio.h>
#include <string.h>

#ifndef RoboFontNameIdFamily
#define RoboFontNameIdFamily 1u
#endif
#ifndef RoboFontNameIdSubfamily
#define RoboFontNameIdSubfamily 2u
#endif
#ifndef RoboFontNameIdFull
#define RoboFontNameIdFull 4u
#endif
#ifndef RoboFontNameIdPostscript
#define RoboFontNameIdPostscript 6u
#endif

static void PutU8(uint8_t **p, uint8_t v) { *(*p)++ = v; }
static void PutU16(uint8_t **p, uint16_t v)
{
    *(*p)++ = (uint8_t)(v >> 8);
    *(*p)++ = (uint8_t)v;
}
static void PutI16(uint8_t **p, int16_t v) { PutU16(p, (uint16_t)v); }
static void PutU32(uint8_t **p, uint32_t v)
{
    *(*p)++ = (uint8_t)(v >> 24);
    *(*p)++ = (uint8_t)(v >> 16);
    *(*p)++ = (uint8_t)(v >> 8);
    *(*p)++ = (uint8_t)v;
}
static void PutI64(uint8_t **p, int64_t v)
{
    PutU32(p, (uint32_t)((uint64_t)v >> 32));
    PutU32(p, (uint32_t)((uint64_t)v));
}

typedef struct {
    uint32_t tag;
    const uint8_t *data;
    uint32_t length;
} SynthTable;

static void WriteSearchFields(uint8_t *hdr, uint16_t numTables)
{
    uint16_t pow2 = 1u;
    uint16_t entry = 0u;
    uint8_t *p;
    while ((uint16_t)(pow2 * 2u) <= numTables) {
        pow2 = (uint16_t)(pow2 * 2u);
        entry++;
    }
    p = hdr + 4;
    PutU16(&p, numTables);
    PutU16(&p, (uint16_t)(pow2 * 16u));
    PutU16(&p, entry);
    PutU16(&p, (uint16_t)(numTables * 16u - pow2 * 16u));
}

static size_t BuildSfnt(uint8_t *out, size_t outCap, const SynthTable *tables,
                         uint32_t n)
{
    size_t dirEnd = 12u + (size_t)n * 16u;
    size_t cursor;
    size_t need = dirEnd;
    uint32_t i;
    uint8_t *p;
    for (i = 0u; i < n; i++) {
        need += ((size_t)tables[i].length + 3u) & ~(size_t)3u;
    }
    if (need > outCap) {
        return 0u;
    }
    memset(out, 0, need);
    p = out;
    PutU32(&p, 0x00010000u);
    WriteSearchFields(out, (uint16_t)n);
    cursor = dirEnd;
    for (i = 0u; i < n; i++) {
        uint32_t pad = (uint32_t)(((size_t)tables[i].length + 3u) & ~(size_t)3u);
        uint8_t *rec = out + 12u + (size_t)i * 16u;
        p = rec;
        PutU32(&p, tables[i].tag);
        PutU32(&p, 0u);
        PutU32(&p, (uint32_t)cursor);
        PutU32(&p, tables[i].length);
        if (tables[i].length) {
            memcpy(out + cursor, tables[i].data, tables[i].length);
        }
        cursor += pad;
    }
    return cursor;
}

static size_t BuildHead(uint8_t *buf)
{
    uint8_t *p = buf;
    PutU16(&p, 1);
    PutU16(&p, 0);
    PutU32(&p, 0x00010000u);
    PutU32(&p, 0);
    PutU32(&p, RoboFontHeadMagic);
    PutU16(&p, 0);
    PutU16(&p, 1000);
    PutI64(&p, 0);
    PutI64(&p, 0);
    PutI16(&p, 0);
    PutI16(&p, -200);
    PutI16(&p, 1000);
    PutI16(&p, 800);
    PutU16(&p, 0);
    PutU16(&p, 9);
    PutI16(&p, 2);
    PutI16(&p, 0);
    PutI16(&p, 0);
    return (size_t)(p - buf);
}

static size_t BuildHhea(uint8_t *buf)
{
    uint8_t *p = buf;
    PutU16(&p, 1);
    PutU16(&p, 0);
    PutI16(&p, 800);
    PutI16(&p, -200);
    PutI16(&p, 90);
    PutU16(&p, 1000);
    PutI16(&p, 0);
    PutI16(&p, 0);
    PutI16(&p, 0);
    PutI16(&p, 0);
    PutI16(&p, 0);
    PutI16(&p, 0);
    PutI16(&p, 0);
    PutI16(&p, 0);
    PutI16(&p, 0);
    PutI16(&p, 0);
    PutI16(&p, 0);
    PutU16(&p, 2);
    return (size_t)(p - buf);
}

static size_t BuildMaxp(uint8_t *buf, uint16_t glyphs)
{
    uint8_t *p = buf;
    int i;
    PutU32(&p, 0x00010000u);
    PutU16(&p, glyphs);
    for (i = 0; i < 13; i++) {
        PutU16(&p, 0);
    }
    return (size_t)(p - buf);
}

static size_t BuildHmtx(uint8_t *buf)
{
    uint8_t *p = buf;
    PutU16(&p, 500);
    PutI16(&p, 0);
    PutU16(&p, 500);
    PutI16(&p, 0);
    return (size_t)(p - buf);
}

static size_t BuildLoca(uint8_t *buf)
{
    uint8_t *p = buf;
    PutU16(&p, 0);
    PutU16(&p, 0);
    PutU16(&p, 0);
    return (size_t)(p - buf);
}

static size_t BuildPost(uint8_t *buf)
{
    uint8_t *p = buf;
    PutU32(&p, 0x00030000u);
    PutU32(&p, 0);
    PutI16(&p, 0);
    PutI16(&p, 0);
    PutU32(&p, 0);
    PutU32(&p, 0);
    PutU32(&p, 0);
    PutU32(&p, 0);
    PutU32(&p, 0);
    return (size_t)(p - buf);
}

static size_t BuildOs2(uint8_t *buf)
{
    uint8_t *p = buf;
    int i;
    PutU16(&p, 4);
    PutI16(&p, 500);
    PutU16(&p, 400);
    PutU16(&p, 5);
    PutU16(&p, 0);
    for (i = 0; i < 10; i++) {
        PutI16(&p, 0);
    }
    PutI16(&p, 0);
    for (i = 0; i < 10; i++) {
        PutU8(&p, 0);
    }
    PutU32(&p, 0);
    PutU32(&p, 0);
    PutU32(&p, 0);
    PutU32(&p, 0);
    PutU8(&p, 'T');
    PutU8(&p, 'E');
    PutU8(&p, 'S');
    PutU8(&p, 'T');
    PutU16(&p, RoboFontFsRegular);
    PutU16(&p, 32);
    PutU16(&p, 126);
    PutI16(&p, 800);
    PutI16(&p, -200);
    PutI16(&p, 90);
    PutU16(&p, 800);
    PutU16(&p, 200);
    PutU32(&p, 1);
    PutU32(&p, 0);
    PutI16(&p, 500);
    PutI16(&p, 700);
    PutU16(&p, 0);
    PutU16(&p, 32);
    PutU16(&p, 0);
    return (size_t)(p - buf);
}

static void PutUtf16beAscii(uint8_t **p, const char *s)
{
    while (*s) {
        PutU16(p, (uint16_t)(uint8_t)*s);
        s++;
    }
}

static size_t BuildNameTable(uint8_t *buf, size_t cap, const char *family,
                               const char *ps)
{
    const char *strs[4];
    uint16_t ids[4];
    uint32_t count = 4u;
    uint32_t i;
    size_t header = 6u + (size_t)count * 12u;
    size_t storage = header;
    uint8_t *p = buf;

    ids[0] = RoboFontNameIdFamily;
    strs[0] = family;
    ids[1] = RoboFontNameIdSubfamily;
    strs[1] = "Regular";
    ids[2] = RoboFontNameIdFull;
    strs[2] = family;
    ids[3] = RoboFontNameIdPostscript;
    strs[3] = ps;

    if (cap < header) {
        return 0u;
    }
    PutU16(&p, 0);
    PutU16(&p, (uint16_t)count);
    PutU16(&p, (uint16_t)header);
    for (i = 0u; i < count; i++) {
        size_t slen = strlen(strs[i]) * 2u;
        uint8_t *rec = buf + 6u + (size_t)i * 12u;
        uint8_t *rp = rec;
        if (storage + slen > cap) {
            return 0u;
        }
        PutU16(&rp, 3);
        PutU16(&rp, 1);
        PutU16(&rp, 0x0409);
        PutU16(&rp, ids[i]);
        PutU16(&rp, (uint16_t)slen);
        PutU16(&rp, (uint16_t)(storage - header));
        {
            uint8_t *sp = buf + storage;
            PutUtf16beAscii(&sp, strs[i]);
        }
        storage += slen;
    }
    return storage;
}

static size_t BuildCmapFmt4Ascii(uint8_t *buf, uint16_t cp, uint16_t gid)
{
    uint8_t *p = buf;
    uint8_t *lenPtr;
    PutU16(&p, 0);
    PutU16(&p, 1);
    PutU16(&p, 3);
    PutU16(&p, 1);
    PutU32(&p, 12u);
    PutU16(&p, 4);
    lenPtr = p;
    PutU16(&p, 0);
    PutU16(&p, 0);
    PutU16(&p, 4);
    PutU16(&p, 4);
    PutU16(&p, 1);
    PutU16(&p, 0);
    PutU16(&p, cp);
    PutU16(&p, 0xFFFF);
    PutU16(&p, 0);
    PutU16(&p, cp);
    PutU16(&p, 0xFFFF);
    PutI16(&p, (int16_t)((int)gid - (int)cp));
    PutI16(&p, 1);
    PutU16(&p, 0);
    PutU16(&p, 0);
    {
        uint16_t len = (uint16_t)(p - (buf + 12));
        lenPtr[0] = (uint8_t)(len >> 8);
        lenPtr[1] = (uint8_t)len;
    }
    return (size_t)(p - buf);
}

static size_t BuildCmapFmt12Flat(uint8_t *buf, size_t cap, uint32_t first,
                                    uint32_t last, uint32_t gid)
{
    uint8_t *p = buf;
    uint32_t n;
    uint32_t i;
    uint32_t body;
    if (last < first) {
        return 0u;
    }
    n = last - first + 1u;
    body = 16u + n * 12u;
    if (cap < 12u + body) {
        return 0u;
    }
    PutU16(&p, 0);
    PutU16(&p, 1);
    PutU16(&p, 3);
    PutU16(&p, 10);
    PutU32(&p, 12u);
    PutU16(&p, 12);
    PutU16(&p, 0);
    PutU32(&p, body);
    PutU32(&p, 0);
    PutU32(&p, n);
    for (i = 0u; i < n; i++) {
        uint32_t cp = first + i;
        PutU32(&p, cp);
        PutU32(&p, cp);
        PutU32(&p, gid);
    }
    return (size_t)(p - buf);
}

static size_t BuildCmapFmt12Range(uint8_t *buf, size_t cap, uint32_t first,
                                     uint32_t last, uint32_t gid)
{
    return BuildCmapFmt12Flat(buf, cap, first, last, gid);
}

static size_t BuildMinimalColr(uint8_t *buf)
{
    uint8_t *p = buf;
    PutU16(&p, 0); /* version */
    PutU16(&p, 0); /* numBaseGlyphRecords */
    PutU32(&p, 14u); /* baseGlyphRecordsOffset */
    PutU32(&p, 14u); /* layerRecordsOffset */
    PutU16(&p, 0); /* numLayerRecords */
    return (size_t)(p - buf);
}

static size_t BuildMinimalCpal(uint8_t *buf)
{
    uint8_t *p = buf;
    PutU16(&p, 0); /* version */
    PutU16(&p, 1); /* numPaletteEntries */
    PutU16(&p, 1); /* numPalettes */
    PutU16(&p, 1); /* numColorRecords */
    PutU32(&p, 12u); /* colorRecordsArrayOffset */
    PutU16(&p, 0); /* colorRecordIndices[0] */
    PutU8(&p, 0);
    PutU8(&p, 0);
    PutU8(&p, 0);
    PutU8(&p, 255); /* BGRA */
    return (size_t)(p - buf);
}

typedef struct {
    int withColor;
    int withSbix;
    const char *family;
    const char *ps;
    uint32_t cmapFirst;
    uint32_t cmapLast;
    int asciiOnly;
    uint16_t asciiCp;
} EmojiFontSpec;

static size_t BuildEmojiFont(uint8_t *out, size_t cap, const EmojiFontSpec *spec)
{
    uint8_t head[64], hhea[64], maxp[64], hmtx[16], loca[16], post[64], os2[128];
    uint8_t name[512], cmap[1024], colr[32], cpal[32], sbix[8];
    SynthTable tabs[16];
    uint32_t nt = 0u;
    size_t nn;
    size_t nc;
    uint16_t glyphs = 2u;

    if (!spec) {
        return 0u;
    }
    nn = BuildNameTable(name, sizeof(name), spec->family, spec->ps);
    if (nn == 0u) {
        return 0u;
    }
    if (spec->asciiOnly) {
        nc = BuildCmapFmt4Ascii(cmap, spec->asciiCp, 1u);
    } else {
        /* Flat groups → GID 1 so maxp/loca stay consistent. */
        nc = BuildCmapFmt12Range(cmap, sizeof(cmap), spec->cmapFirst,
                                    spec->cmapLast, 1u);
    }
    if (nc == 0u) {
        return 0u;
    }

    tabs[nt++] = (SynthTable){RoboFontTagHead, head, (uint32_t)BuildHead(head)};
    tabs[nt++] = (SynthTable){RoboFontTagHhea, hhea, (uint32_t)BuildHhea(hhea)};
    tabs[nt++] =
        (SynthTable){RoboFontTagMaxp, maxp, (uint32_t)BuildMaxp(maxp, glyphs)};
    tabs[nt++] = (SynthTable){RoboFontTagHmtx, hmtx, (uint32_t)BuildHmtx(hmtx)};
    tabs[nt++] = (SynthTable){RoboFontTagLoca, loca, (uint32_t)BuildLoca(loca)};
    tabs[nt++] = (SynthTable){RoboFontTagGlyf, head, 0u};
    tabs[nt++] = (SynthTable){RoboFontTagPost, post, (uint32_t)BuildPost(post)};
    tabs[nt++] = (SynthTable){RoboFontTagOs2, os2, (uint32_t)BuildOs2(os2)};
    tabs[nt++] = (SynthTable){RoboFontTagName, name, (uint32_t)nn};
    tabs[nt++] = (SynthTable){RoboFontTagCmap, cmap, (uint32_t)nc};
    if (spec->withColor) {
        tabs[nt++] =
            (SynthTable){RoboFontTagColr, colr, (uint32_t)BuildMinimalColr(colr)};
        tabs[nt++] =
            (SynthTable){RoboFontTagCpal, cpal, (uint32_t)BuildMinimalCpal(cpal)};
    }
    if (spec->withSbix) {
        sbix[0] = 0;
        sbix[1] = 0;
        sbix[2] = 0;
        sbix[3] = 1; /* version-ish stub */
        tabs[nt++] = (SynthTable){RoboFontTagSbix, sbix, 4u};
    }
    return BuildSfnt(out, cap, tabs, nt);
}

static void TestClassifyKinds(void)
{
    uint8_t buf[4096];
    size_t n;
    FontSource *src = NULL;
    FontFace *face = NULL;
    FontEmojiInfo *info = NULL;
    FontEmojiAnalyzeOptions opt;
    EmojiFontSpec spec;

    /* Name hint alone â€” not an emoji font. */
    memset(&spec, 0, sizeof(spec));
    spec.family = "CoolEmojiDisplay";
    spec.ps = "CoolEmojiDisplay";
    spec.asciiOnly = 1;
    spec.asciiCp = 0x41u;
    n = BuildEmojiFont(buf, sizeof(buf), &spec);
    RoboCheck(n > 0u);
    RoboCheckEqInt(FontSourceCreateMemoryCopy(buf, n, &src), RoboOk);
    RoboCheckEqInt(FontFaceCreate(src, 0u, &face), RoboOk);
    RoboCheckEqInt(FontEmojiInfoCreateFromFace(face, NULL, &info), RoboOk);
    RoboCheckEqInt((int)FontEmojiInfoKind(info),
                      (int)RoboFontEmojiKindNone);
    RoboCheck(!FontEmojiInfoIsClassifiedEmoji(info));
    FontEmojiInfoRelease(info);
    FontFaceRelease(face);
    FontSourceRelease(src);
    info = NULL;
    face = NULL;
    src = NULL;

    /* Incidental: single emoji scalar. */
    memset(&spec, 0, sizeof(spec));
    spec.family = "TextPlusGrin";
    spec.ps = "TextPlusGrin";
    spec.cmapFirst = 0x1F600u;
    spec.cmapLast = 0x1F600u;
    n = BuildEmojiFont(buf, sizeof(buf), &spec);
    RoboCheck(n > 0u);
    RoboCheckEqInt(FontSourceCreateMemoryCopy(buf, n, &src), RoboOk);
    RoboCheckEqInt(FontFaceCreate(src, 0u, &face), RoboOk);
    RoboCheckEqInt(FontEmojiInfoCreateFromFace(face, NULL, &info), RoboOk);
    RoboCheckEqInt((int)FontEmojiInfoKind(info),
                      (int)RoboFontEmojiKindIncidental);
    RoboCheck(FontEmojiInfoSupportsEmoji(info));
    RoboCheck(!FontEmojiInfoIsClassifiedEmoji(info));
    RoboCheck(FontEmojiCoversCodepoint(info, 0x1F600u));
    FontEmojiInfoRelease(info);
    FontFaceRelease(face);
    FontSourceRelease(src);
    info = NULL;
    face = NULL;
    src = NULL;

    /* Monochrome emoji block. */
    memset(&spec, 0, sizeof(spec));
    spec.family = "MonoEmoji";
    spec.ps = "MonoEmoji";
    spec.cmapFirst = 0x1F600u;
    spec.cmapLast = 0x1F60Fu; /* 16 scalars */
    n = BuildEmojiFont(buf, sizeof(buf), &spec);
    RoboCheck(n > 0u);
    RoboCheckEqInt(FontSourceCreateMemoryCopy(buf, n, &src), RoboOk);
    RoboCheckEqInt(FontFaceCreate(src, 0u, &face), RoboOk);
    RoboCheckEqInt(FontFaceParseEmoji(face), RoboOk);
    RoboCheck(FontFaceEmoji(face) != NULL);
    RoboCheckEqInt((int)FontEmojiInfoKind(FontFaceEmoji(face)),
                      (int)RoboFontEmojiKindMonochrome);
    RoboCheck(FontEmojiInfoSupportsMonochromeEmoji(FontFaceEmoji(face)));
    RoboCheck(!FontEmojiInfoSupportsColorEmoji(FontFaceEmoji(face)));
    FontFaceRelease(face);
    FontSourceRelease(src);
    face = NULL;
    src = NULL;

    /* Color COLR/CPAL + emoji coverage. */
    memset(&spec, 0, sizeof(spec));
    spec.family = "ColorEmoji";
    spec.ps = "ColorEmoji";
    spec.cmapFirst = 0x1F600u;
    spec.cmapLast = 0x1F603u;
    spec.withColor = 1;
    n = BuildEmojiFont(buf, sizeof(buf), &spec);
    RoboCheck(n > 0u);
    RoboCheckEqInt(FontSourceCreateMemoryCopy(buf, n, &src), RoboOk);
    RoboCheckEqInt(FontFaceCreate(src, 0u, &face), RoboOk);
    RoboCheckEqInt(FontEmojiInfoCreateFromFace(face, NULL, &info), RoboOk);
    RoboCheckEqInt((int)FontEmojiInfoKind(info),
                      (int)RoboFontEmojiKindColor);
    RoboCheck(FontEmojiInfoSupportsColorEmoji(info));
    RoboCheck((FontEmojiInfoCapabilities(info) &
                RoboFontEmojiCapVectorColor) != 0u);
    FontEmojiInfoRelease(info);
    FontFaceRelease(face);
    FontSourceRelease(src);
    info = NULL;
    face = NULL;
    src = NULL;

    /* Configured override. */
    memset(&spec, 0, sizeof(spec));
    spec.family = "ConfiguredFace";
    spec.ps = "ConfiguredFace";
    spec.asciiOnly = 1;
    spec.asciiCp = 0x41u;
    n = BuildEmojiFont(buf, sizeof(buf), &spec);
    RoboCheckEqInt(FontSourceCreateMemoryCopy(buf, n, &src), RoboOk);
    RoboCheckEqInt(FontFaceCreate(src, 0u, &face), RoboOk);
    FontEmojiAnalyzeOptionsInit(&opt);
    opt.configuredAsEmoji = 1;
    RoboCheckEqInt(FontEmojiInfoCreateFromFace(face, &opt, &info), RoboOk);
    RoboCheckEqInt((int)FontEmojiInfoKind(info),
                      (int)RoboFontEmojiKindConfigured);
    FontEmojiInfoRelease(info);
    FontFaceRelease(face);
    FontSourceRelease(src);

    /* Bitmap capability (sbix) + emoji. */
    memset(&spec, 0, sizeof(spec));
    spec.family = "BitmapEmoji";
    spec.ps = "BitmapEmoji";
    spec.cmapFirst = 0x1F600u;
    spec.cmapLast = 0x1F601u;
    spec.withSbix = 1;
    n = BuildEmojiFont(buf, sizeof(buf), &spec);
    RoboCheckEqInt(FontSourceCreateMemoryCopy(buf, n, &src), RoboOk);
    RoboCheckEqInt(FontFaceCreate(src, 0u, &face), RoboOk);
    RoboCheckEqInt(FontEmojiInfoCreateFromFace(face, NULL, &info), RoboOk);
    RoboCheck((FontEmojiInfoCapabilities(info) & RoboFontEmojiCapBitmap) !=
               0u);
    RoboCheckEqInt((int)FontEmojiInfoKind(info),
                      (int)RoboFontEmojiKindColor);
    FontEmojiInfoRelease(info);
    FontFaceRelease(face);
    FontSourceRelease(src);
}

static void TestSequenceFoundation(void)
{
    FontEmojiSequence seq;
    uint32_t simple[] = {0x1F600u};
    uint32_t withVs[] = {0x263Au, RoboFontEmojiVsEmoji};
    uint32_t skin[] = {0x1F44Du, 0x1F3FDu};
    uint32_t zwj[] = {0x1F469u, RoboFontEmojiZwj, 0x1F467u};
    uint32_t flag[] = {0x1F1E6u, 0x1F1E8u};
    uint32_t keycap[] = {0x0031u, 0xFE0Fu, RoboFontEmojiKeycap};

    RoboCheckEqInt(FontEmojiSequenceSet(&seq, simple, 1u), RoboOk);
    RoboCheckEqInt(FontEmojiSequenceAnalyze(&seq), RoboOk);
    RoboCheck(FontEmojiContentIsEmojiRelated(simple, 1u));

    RoboCheckEqInt(FontEmojiSequenceSet(&seq, withVs, 2u), RoboOk);
    RoboCheckEqInt(FontEmojiSequenceAnalyze(&seq), RoboOk);
    RoboCheckEqInt((int)seq.hasVsEmoji, 1);

    RoboCheckEqInt(FontEmojiSequenceSet(&seq, skin, 2u), RoboOk);
    RoboCheckEqInt(FontEmojiSequenceAnalyze(&seq), RoboOk);
    RoboCheckEqInt((int)seq.hasModifier, 1);

    RoboCheckEqInt(FontEmojiSequenceSet(&seq, zwj, 3u), RoboOk);
    RoboCheckEqInt(FontEmojiSequenceAnalyze(&seq), RoboOk);
    RoboCheckEqInt((int)seq.hasZwj, 1);

    RoboCheckEqInt(FontEmojiSequenceSet(&seq, flag, 2u), RoboOk);
    RoboCheckEqInt(FontEmojiSequenceAnalyze(&seq), RoboOk);
    RoboCheckEqInt((int)seq.hasRegionalIndicator, 1);

    RoboCheckEqInt(FontEmojiSequenceSet(&seq, keycap, 3u), RoboOk);
    RoboCheckEqInt(FontEmojiSequenceAnalyze(&seq), RoboOk);
    RoboCheckEqInt((int)seq.hasKeycap, 1);

    RoboCheckEqInt(
        FontEmojiSequenceSet(&seq, simple, RoboFontEmojiMaxSequence + 1u),
        RoboInvalidArgument);
}

static void TestSelectAndFallback(void)
{
    uint8_t a[4096], b[4096], c[4096];
    size_t na, nb, nc;
    FontRegistry *reg = NULL;
    FontDiscoveryResult dr;
    FontDiscoverySourceId s1, s2, s3;
    FontGenericConfig *cfg = NULL;
    FontGenericResolver *gr = NULL;
    FontGenericResolverOptions gopts;
    FontFallbackResolver *fb = NULL;
    FontEmojiSelectRequest req;
    FontEmojiSelection sel;
    FontFallbackResult fres;
    const char *const emojiFams[2] = {"ColorEmoji", "MonoEmoji"};
    uint32_t cp = 0x1F600u;
    EmojiFontSpec spec;
    FontFace *primary = NULL;

    memset(&spec, 0, sizeof(spec));
    spec.family = "ColorEmoji";
    spec.ps = "ColorEmoji";
    spec.cmapFirst = 0x1F600u;
    spec.cmapLast = 0x1F60Fu;
    spec.withColor = 1;
    na = BuildEmojiFont(a, sizeof(a), &spec);
    RoboCheck(na > 0u);

    memset(&spec, 0, sizeof(spec));
    spec.family = "MonoEmoji";
    spec.ps = "MonoEmoji";
    spec.cmapFirst = 0x1F600u;
    spec.cmapLast = 0x1F60Fu;
    nb = BuildEmojiFont(b, sizeof(b), &spec);
    RoboCheck(nb > 0u);

    memset(&spec, 0, sizeof(spec));
    spec.family = "PlainSans";
    spec.ps = "PlainSans";
    spec.asciiOnly = 1;
    spec.asciiCp = 0x41u;
    nc = BuildEmojiFont(c, sizeof(c), &spec);
    RoboCheck(nc > 0u);

    RoboCheckEqInt(FontRegistryCreate(&reg), RoboOk);
    RoboCheckEqInt(FontRegistryAddMemorySource(reg, "a", a, na, &s1), RoboOk);
    RoboCheckEqInt(FontRegistryAddMemorySource(reg, "b", b, nb, &s2), RoboOk);
    RoboCheckEqInt(FontRegistryAddMemorySource(reg, "c", c, nc, &s3), RoboOk);
    RoboCheckEqInt(FontRegistryScanAll(reg, 0u, &dr), RoboOk);
    RoboCheck((int)FontRegistryRegistrationCount(reg) >= 3);

    RoboCheckEqInt(FontGenericConfigCreate(&cfg), RoboOk);
    RoboCheckEqInt(
        FontGenericConfigSetMapping(cfg, RoboFontGenericEmoji, emojiFams, 2u),
        RoboOk);
    FontGenericResolverOptionsInit(&gopts);
    gopts.application = cfg;
    RoboCheckEqInt(FontGenericResolverCreate(reg, &gopts, &gr), RoboOk);
    RoboCheckEqInt(FontFallbackResolverCreate(reg, NULL, &fb), RoboOk);

    FontEmojiSelectRequestInit(&req);
    req.codepoints = &cp;
    req.codepointCount = 1u;
    req.colorPreference = RoboFontEmojiColorPreferColor;
    req.useGenericEmoji = 1;
    FontEmojiSelectionInit(&sel);
    RoboCheckEqInt(FontEmojiSelect(reg, gr, &req, &sel), RoboOk);
    RoboCheckEqInt(sel.found, 1);
    RoboCheckStrEq(sel.family, "ColorEmoji");
    RoboCheckEqInt((int)sel.kind, (int)RoboFontEmojiKindColor);
    RoboCheck((sel.capabilities & RoboFontEmojiCapConfigured) != 0u);
    RoboCheck((sel.capabilities & RoboFontEmojiCapColor) != 0u);
    FontEmojiSelectionClear(&sel);

    req.colorPreference = RoboFontEmojiColorRequireMono;
    RoboCheckEqInt(FontEmojiSelect(reg, gr, &req, &sel), RoboOk);
    RoboCheckEqInt(sel.found, 1);
    RoboCheckStrEq(sel.family, "MonoEmoji");
    FontEmojiSelectionClear(&sel);

    /* Unsupported preference: require color when only mono configured. */
    {
        const char *monoOnly[] = {"MonoEmoji"};
        RoboCheckEqInt(FontGenericConfigSetMapping(
                              cfg, RoboFontGenericEmoji, monoOnly, 1u),
                          RoboOk);
        RoboCheckEqInt(FontGenericResolverSetApplication(gr, cfg), RoboOk);
        req.colorPreference = RoboFontEmojiColorRequireColor;
        RoboCheckEqInt(FontEmojiSelect(reg, gr, &req, &sel), RoboOk);
        /* ColorEmoji remains in the registry and can still win via coverage. */
        if (sel.found) {
            RoboCheck((sel.capabilities & RoboFontEmojiCapColor) != 0u);
        }
        FontEmojiSelectionClear(&sel);
        RoboCheckEqInt(FontGenericConfigSetMapping(
                              cfg, RoboFontGenericEmoji, emojiFams, 2u),
                          RoboOk);
        RoboCheckEqInt(FontGenericResolverSetApplication(gr, cfg), RoboOk);
    }

    /* Fallback: primary PlainSans missing emoji â†’ emoji family. */
    {
        uint32_t i;
        uint32_t n = FontRegistryRegistrationCount(reg);
        for (i = 0u; i < n; i++) {
            const FontRegistration *rec = NULL;
            RoboCheckEqInt(FontRegistryRegistrationAt(reg, i, &rec), RoboOk);
            if (rec && strcmp(rec->family, "PlainSans") == 0) {
                primary = rec->face;
                break;
            }
        }
    }
    RoboCheck(primary != NULL);
    req.colorPreference = RoboFontEmojiColorAny;
    FontFallbackResultInit(&fres);
    RoboCheckEqInt(
        FontEmojiResolveFallback(reg, gr, fb, primary, &req, &sel, &fres),
        RoboOk);
    RoboCheckEqInt(fres.found, 1);
    RoboCheckEqInt(fres.usedPrimary, 0);
    {
        const FontRegistration *rec = NULL;
        RoboCheckEqInt(FontRegistryFindByFaceId(reg, fres.faceId, &rec),
                          RoboOk);
        RoboCheck(rec != NULL);
        RoboCheck(strcmp(rec->family, "ColorEmoji") == 0 ||
                   strcmp(rec->family, "MonoEmoji") == 0);
    }
    FontFallbackResultClear(&fres);
    FontEmojiSelectionClear(&sel);

    /* Bench: repeated select. */
    {
        uint32_t i;
        for (i = 0u; i < 200u; i++) {
            RoboCheckEqInt(FontEmojiSelect(reg, gr, &req, &sel), RoboOk);
            FontEmojiSelectionClear(&sel);
        }
        printf("font.emoji.bench.select hits=200\n");
    }

    FontFallbackResolverDestroy(fb);
    FontGenericResolverDestroy(gr);
    FontGenericConfigRelease(cfg);
    FontRegistryDestroy(reg);
}

static void TestFuzzAndSeqSupport(void)
{
    uint8_t junk[64];
    uint32_t i;
    uint8_t buf[4096];
    size_t n;
    FontSource *src = NULL;
    FontFace *face = NULL;
    FontEmojiInfo *info = NULL;
    FontEmojiSequence seq;
    uint32_t zwj[] = {0x1F600u, RoboFontEmojiZwj, 0x1F602u};
    EmojiFontSpec spec;

    for (i = 0u; i < sizeof(junk); i++) {
        junk[i] = (uint8_t)(i * 19u + 3u);
    }
    RoboCheckEqInt(FontEmojiFuzzParse(junk, sizeof(junk)), RoboOk);
    RoboCheckEqInt(FontEmojiFuzzParse(NULL, 0u), RoboOk);

    memset(&spec, 0, sizeof(spec));
    spec.family = "SeqFont";
    spec.ps = "SeqFont";
    spec.cmapFirst = 0x1F600u;
    spec.cmapLast = 0x1F60Fu;
    n = BuildEmojiFont(buf, sizeof(buf), &spec);
    RoboCheckEqInt(FontSourceCreateMemoryCopy(buf, n, &src), RoboOk);
    RoboCheckEqInt(FontFaceCreate(src, 0u, &face), RoboOk);
    RoboCheckEqInt(FontEmojiInfoCreateFromFace(face, NULL, &info), RoboOk);
    RoboCheckEqInt(FontEmojiSequenceSet(&seq, zwj, 3u), RoboOk);
    RoboCheckEqInt(FontEmojiSequenceAnalyze(&seq), RoboOk);
    /* ZWJ â†’ UNKNOWN even when components covered. */
    RoboCheckEqInt((int)FontEmojiInfoSupportsSequence(info, &seq),
                      (int)RoboFontEmojiSeqSupportUnknown);
    {
        uint32_t one[] = {0x1F600u};
        RoboCheckEqInt(FontEmojiSequenceSet(&seq, one, 1u), RoboOk);
        RoboCheckEqInt(FontEmojiSequenceAnalyze(&seq), RoboOk);
        RoboCheckEqInt((int)FontEmojiInfoSupportsSequence(info, &seq),
                          (int)RoboFontEmojiSeqSupportComponents);
    }
    FontEmojiInfoRelease(info);
    FontFaceRelease(face);
    FontSourceRelease(src);
}

void TestRegisterFontEmoji(void)
{
    TestRegister("font.emoji.classify", TestClassifyKinds);
    TestRegister("font.emoji.sequence", TestSequenceFoundation);
    TestRegister("font.emoji.select_fallback", TestSelectAndFallback);
    TestRegister("font.emoji.fuzz_seq", TestFuzzAndSeqSupport);
}
