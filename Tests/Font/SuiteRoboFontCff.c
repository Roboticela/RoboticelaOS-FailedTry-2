/*
 * RoboFont CFF / CFF2 suite (Goal 5.5).
 * Synthetic fixtures only — no Chromium / system fonts.
 */

#include "RoboFont/RoboFont.h"
#include "RoboTest.h"

#include <string.h>

static void PutU8(uint8_t **p, uint8_t v) { *(*p)++ = v; }
static void PutU16(uint8_t **p, uint16_t v)
{
    *(*p)++ = (uint8_t)(v >> 8);
    *(*p)++ = (uint8_t)v;
}
static void PutU32(uint8_t **p, uint32_t v)
{
    *(*p)++ = (uint8_t)(v >> 24);
    *(*p)++ = (uint8_t)(v >> 16);
    *(*p)++ = (uint8_t)(v >> 8);
    *(*p)++ = (uint8_t)v;
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

static size_t BuildSfnt(uint8_t *out,
                         size_t outCap,
                         uint32_t version,
                         const SynthTable *tables,
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
    PutU32(&p, version);
    WriteSearchFields(out, (uint16_t)n);
    cursor = dirEnd;
    for (i = 0u; i < n; i++) {
        uint32_t pad = (uint32_t)(((size_t)tables[i].length + 3u) & ~(size_t)3u);
        uint32_t sum = 0u;
        uint32_t j;
        uint8_t *rec = out + 12u + (size_t)i * 16u;
        p = rec;
        PutU32(&p, tables[i].tag);
        for (j = 0u; j + 3u < tables[i].length; j += 4u) {
            sum += ((uint32_t)tables[i].data[j] << 24) |
                   ((uint32_t)tables[i].data[j + 1u] << 16) |
                   ((uint32_t)tables[i].data[j + 2u] << 8) |
                   (uint32_t)tables[i].data[j + 3u];
        }
        PutU32(&p, sum);
        PutU32(&p, (uint32_t)cursor);
        PutU32(&p, tables[i].length);
        memcpy(out + cursor, tables[i].data, tables[i].length);
        cursor += pad;
    }
    return cursor;
}

/* Encode DICT integer in -107..107 range as one byte. */
static void DictPutSmall(uint8_t **p, int32_t v)
{
    PutU8(p, (uint8_t)(v + 139));
}

static void DictPutInt(uint8_t **p, int32_t v)
{
    if (v >= -107 && v <= 107) {
        DictPutSmall(p, v);
    } else if (v >= -32768 && v <= 32767) {
        PutU8(p, 28);
        PutU16(p, (uint16_t)(int16_t)v);
    } else {
        PutU8(p, 29);
        PutU32(p, (uint32_t)v);
    }
}

/* CFF1 INDEX with Card16 count, offSize 1 */
static size_t WriteIndex1(uint8_t *buf, const uint8_t *obj, uint32_t objLen)
{
    uint8_t *p = buf;
    PutU16(&p, 1u);
    PutU8(&p, 1u);
    PutU8(&p, 1u);
    PutU8(&p, (uint8_t)(1u + objLen));
    memcpy(p, obj, objLen);
    p += objLen;
    return (size_t)(p - buf);
}

static size_t WriteEmptyIndex1(uint8_t *buf)
{
    uint8_t *p = buf;
    PutU16(&p, 0u);
    return 2u;
}

/*
 * Minimal CFF1:
 *   header + Name + TopDICT + empty String + empty GSubr
 *   + CharStrings (1 glyph: endchar) + charset fmt0 + Private
 */
static size_t BuildMinimalCff1(uint8_t *out, size_t cap)
{
    uint8_t name[] = {'T', 'e', 's', 't'};
    uint8_t cs[] = {0x0e};
    uint8_t topd[32];
    uint8_t *t;
    uint8_t privd[8];
    uint8_t *pv;
    size_t privLen;
    size_t topLen;
    size_t pos;
    size_t csPos;
    size_t charsetPos;
    size_t privPos;
    size_t topIndexSize;
    size_t baseAfterIndexes;
    size_t estTop = 20u;

    if (cap < 256u) {
        return 0u;
    }

    pv = privd;
    DictPutSmall(&pv, 0);
    PutU8(&pv, 20);
    privLen = (size_t)(pv - privd);

    topIndexSize = 5u + estTop;
    baseAfterIndexes = 4u + (2u + 1u + 2u + 4u) + topIndexSize + 2u + 2u;
    csPos = baseAfterIndexes;
    charsetPos = csPos + 5u + 1u;
    privPos = charsetPos + 1u;

    t = topd;
    DictPutInt(&t, (int32_t)charsetPos);
    PutU8(&t, 15);
    DictPutInt(&t, (int32_t)csPos);
    PutU8(&t, 17);
    DictPutInt(&t, (int32_t)privLen);
    DictPutInt(&t, (int32_t)privPos);
    PutU8(&t, 18);
    topLen = (size_t)(t - topd);

    topIndexSize = 5u + topLen;
    baseAfterIndexes = 4u + (2u + 1u + 2u + 4u) + topIndexSize + 2u + 2u;
    csPos = baseAfterIndexes;
    charsetPos = csPos + 5u + 1u;
    privPos = charsetPos + 1u;

    t = topd;
    DictPutInt(&t, (int32_t)charsetPos);
    PutU8(&t, 15);
    DictPutInt(&t, (int32_t)csPos);
    PutU8(&t, 17);
    DictPutInt(&t, (int32_t)privLen);
    DictPutInt(&t, (int32_t)privPos);
    PutU8(&t, 18);
    topLen = (size_t)(t - topd);

    memset(out, 0, cap);
    pos = 0u;
    out[pos++] = 1;
    out[pos++] = 0;
    out[pos++] = 4;
    out[pos++] = 1;
    pos += WriteIndex1(out + pos, name, 4u);
    pos += WriteIndex1(out + pos, topd, (uint32_t)topLen);
    pos += WriteEmptyIndex1(out + pos);
    pos += WriteEmptyIndex1(out + pos);
    if (pos != csPos) {
        return 0u;
    }
    pos += WriteIndex1(out + pos, cs, 1u);
    if (pos != charsetPos) {
        return 0u;
    }
    out[pos++] = 0;
    if (pos != privPos) {
        return 0u;
    }
    memcpy(out + pos, privd, privLen);
    pos += privLen;
    return pos;
}

static void TestCffIndexDict(void)
{
    uint8_t buf[64];
    uint8_t *p = buf;
    FontCffIndex idx;
    FontSpan span;
    FontSpan obj;
    size_t end = 0u;
    FontCffDict dict;
    const FontCffDictEntry *ent;

    /* INDEX: count=2, offSize=1, objs "A","BC" */
    PutU16(&p, 2u);
    PutU8(&p, 1u);
    PutU8(&p, 1u); /* off0 */
    PutU8(&p, 2u); /* off1 */
    PutU8(&p, 4u); /* off2 */
    PutU8(&p, 'A');
    PutU8(&p, 'B');
    PutU8(&p, 'C');
    span.data = buf;
    span.size = (size_t)(p - buf);
    memset(&idx, 0, sizeof(idx));
    RoboCheckEqInt(FontCffIndexParse(span, 0, &idx, &end), RoboOk);
    RoboCheckEqInt((int)FontCffIndexCount(&idx), 2);
    RoboCheckEqInt(FontCffIndexGet(&idx, 0, &obj), RoboOk);
    RoboCheckEqInt((int)obj.size, 1);
    RoboCheckEqInt(obj.data[0], 'A');
    RoboCheckEqInt(FontCffIndexGet(&idx, 1, &obj), RoboOk);
    RoboCheckEqInt((int)obj.size, 2);
    FontCffIndexClear(&idx);

    /* DICT: FontBBox -10 -20 100 200 */
    p = buf;
    DictPutInt(&p, -10);
    DictPutInt(&p, -20);
    DictPutInt(&p, 100);
    DictPutInt(&p, 200);
    PutU8(&p, 5);
    span.size = (size_t)(p - buf);
    memset(&dict, 0, sizeof(dict));
    RoboCheckEqInt(FontCffDictParse(span, &dict), RoboOk);
    RoboCheckEqInt(FontCffDictFind(&dict, RoboCffOpFontbbox, &ent), RoboOk);
    RoboCheckEqInt(ent->argc, 4);
    RoboCheckEqInt(ent->args[0].i, -10);
    RoboCheckEqInt(ent->args[3].i, 200);
    FontCffDictClear(&dict);

    RoboCheckEqInt((int)FontCffSubrBias(10u), 107);
    RoboCheckEqInt((int)FontCffSubrBias(2000u), 1131);
}

static void TestCffCharstring(void)
{
    uint8_t cs[] = {
        /* 10 20 rmoveto  30 0 rlineto  endchar */
        (uint8_t)(10 + 139), (uint8_t)(20 + 139), 21, (uint8_t)(30 + 139),
        (uint8_t)(0 + 139), 5, 0x0e};
    FontSpan span = {cs, sizeof(cs)};
    FontCffCharstring out;
    memset(&out, 0, sizeof(out));
    RoboCheckEqInt(
        FontCffCharstringParse(span, NULL, 0.0, 0.0, 0, &out), RoboOk);
    RoboCheck(out.opCount >= 3u);
    RoboCheckEqInt(out.ops[out.opCount - 1u].op, 14);
    FontCffCharstringClear(&out);

    /* stack overflow */
    {
        uint8_t bad[64];
        uint8_t *p = bad;
        int i;
        for (i = 0; i < 50; i++) {
            PutU8(&p, (uint8_t)(139)); /* push 0 */
        }
        PutU8(&p, 0x0e);
        span.data = bad;
        span.size = (size_t)(p - bad);
        RoboCheck(FontCffCharstringParse(span, NULL, 0.0, 0.0, 0, &out) !=
                   RoboOk);
        FontCffCharstringClear(&out);
    }
}

static void TestCffCharsetEncodingFdselect(void)
{
    uint8_t buf[40];
    uint8_t *p;
    FontSpan span;
    FontCffCharset cs;
    FontCffEncoding enc;
    FontCffFdselect fd;
    uint16_t sid;
    uint8_t fdi;

    /* Place payload at offset 10 so parsers treat it as a real table offset. */
    memset(buf, 0, sizeof(buf));
    p = buf + 10;
    PutU8(&p, 0);
    PutU16(&p, 1);
    PutU16(&p, 2);
    span.data = buf;
    span.size = (size_t)(p - buf);
    memset(&cs, 0, sizeof(cs));
    RoboCheckEqInt(FontCffCharsetParse(span, 10, 3u, 0, &cs), RoboOk);
    RoboCheckEqInt(FontCffCharsetSid(&cs, 1, &sid), RoboOk);
    RoboCheckEqInt(sid, 1);
    FontCffCharsetClear(&cs);

    memset(buf, 0, sizeof(buf));
    p = buf + 10;
    PutU8(&p, 0);
    PutU8(&p, 2);
    PutU8(&p, 65);
    PutU8(&p, 66);
    span.size = (size_t)(p - buf);
    memset(&enc, 0, sizeof(enc));
    RoboCheckEqInt(FontCffEncodingParse(span, 10, 3u, &enc), RoboOk);
    RoboCheckEqInt((int)enc.nCodes, 2);
    RoboCheckEqInt(enc.codes[0], 65);
    FontCffEncodingClear(&enc);

    memset(buf, 0, sizeof(buf));
    p = buf + 10;
    PutU8(&p, 3);
    PutU16(&p, 1);
    PutU16(&p, 0);
    PutU8(&p, 0);
    PutU16(&p, 3);
    span.size = (size_t)(p - buf);
    memset(&fd, 0, sizeof(fd));
    RoboCheckEqInt(FontCffFdselectParse(span, 10, 3u, 1u, &fd), RoboOk);
    RoboCheckEqInt(FontCffFdselectGet(&fd, 2, &fdi), RoboOk);
    RoboCheckEqInt(fdi, 0);
    FontCffFdselectClear(&fd);
}

static void TestCffFontBundle(void)
{
    uint8_t cff[256];
    uint8_t font[512];
    size_t cffLen;
    size_t n;
    SynthTable tabs[1];
    FontSource *src = NULL;
    FontSfnt *sfnt = NULL;
    FontCffFont *cf = NULL;
    FontCffCharstring glyph;

    cffLen = BuildMinimalCff1(cff, sizeof(cff));
    RoboCheck(cffLen > 0u);
    tabs[0].tag = RoboFontTagCff;
    tabs[0].data = cff;
    tabs[0].length = (uint32_t)cffLen;
    n = BuildSfnt(font, sizeof(font), RoboFontSfntVersionOtto, tabs, 1u);
    RoboCheck(n > 0u);

    RoboCheckEqInt(FontSourceCreateMemoryCopy(font, n, &src), RoboOk);
    RoboCheckEqInt(FontSfntParse(src, 0, 0, &sfnt), RoboOk);
    RoboCheckEqInt((int)FontOtDetectOutline(sfnt), (int)RoboFontOutlineCff);
    RoboCheckEqInt(FontCffParse(sfnt, RoboFontCffFlagsParseAll, &cf),
                      RoboOk);
    RoboCheckEqInt((int)FontCffOutlineTech(cf), (int)RoboFontOutlineCff);
    RoboCheckEqInt((int)FontCffGlyphCount(cf), 1);
    RoboCheckEqInt(cf->major, 1);
    RoboCheck(cf->top.charstrings >= 0);
    memset(&glyph, 0, sizeof(glyph));
    RoboCheckEqInt(FontCffParseGlyph(cf, 0, &glyph), RoboOk);
    RoboCheck(glyph.opCount >= 1u);
    FontCffCharstringClear(&glyph);

    RoboCheckEqInt(FontCffFuzzParse(font, n), RoboOk);

    FontCffRelease(cf);
    FontSfntRelease(sfnt);
    FontSourceRelease(src);
}

static void TestCff2AndVstore(void)
{
    uint8_t cff2[256];
    uint8_t top[40];
    uint8_t fontDict[16];
    uint8_t fdselect[16];
    uint8_t font[512];
    uint8_t *t;
    uint8_t *fd;
    uint8_t *fs;
    uint8_t *p;
    size_t topLen;
    size_t fdLen;
    size_t fdsLen;
    size_t hdr = 5u;
    size_t topEst = 32u;
    size_t gsubrOff;
    size_t csOff;
    size_t fdOff;
    size_t fdselectOff;
    size_t privOff;
    size_t vstoreOff;
    size_t csSize = 8u; /* Card32 INDEX with 1×endchar */
    size_t gEmpty = 4u;
    size_t fdsSize = 8u;
    size_t privSize = 0u;
    size_t n;
    SynthTable tabs[1];
    FontSource *src = NULL;
    FontSfnt *sfnt = NULL;
    FontCffFont *cf = NULL;

    gsubrOff = hdr + topEst;
    csOff = gsubrOff + gEmpty;
    fdOff = csOff + csSize;
    /* FD dict size estimate then refine */
    fd = fontDict;
    DictPutInt(&fd, 0);
    DictPutInt(&fd, 100); /* placeholder priv */
    PutU8(&fd, 18);
    fdLen = (size_t)(fd - fontDict);
    fdselectOff = fdOff + 4u + 1u + 2u + fdLen;
    privOff = fdselectOff + fdsSize;
    vstoreOff = privOff + privSize;

    fd = fontDict;
    DictPutInt(&fd, (int32_t)privSize);
    DictPutInt(&fd, (int32_t)privOff);
    PutU8(&fd, 18);
    fdLen = (size_t)(fd - fontDict);
    fdselectOff = fdOff + 4u + 1u + 2u + fdLen;
    privOff = fdselectOff + fdsSize;
    vstoreOff = privOff + privSize;

    t = top;
    DictPutInt(&t, (int32_t)csOff);
    PutU8(&t, 17);
    DictPutInt(&t, (int32_t)fdOff);
    PutU8(&t, 12);
    PutU8(&t, 36);
    DictPutInt(&t, (int32_t)fdselectOff);
    PutU8(&t, 12);
    PutU8(&t, 37);
    DictPutInt(&t, (int32_t)vstoreOff);
    PutU8(&t, 12);
    PutU8(&t, 24);
    topLen = (size_t)(t - top);

    if (topLen != topEst) {
        {
            int32_t d = (int32_t)topLen - (int32_t)topEst;
            csOff = (size_t)((int32_t)csOff + d);
            fdOff = (size_t)((int32_t)fdOff + d);
            fdselectOff = (size_t)((int32_t)fdselectOff + d);
            privOff = (size_t)((int32_t)privOff + d);
            vstoreOff = (size_t)((int32_t)vstoreOff + d);
            gsubrOff = (size_t)((int32_t)gsubrOff + d);
        }
        fd = fontDict;
        DictPutInt(&fd, (int32_t)privSize);
        DictPutInt(&fd, (int32_t)privOff);
        PutU8(&fd, 18);
        fdLen = (size_t)(fd - fontDict);
        t = top;
        DictPutInt(&t, (int32_t)csOff);
        PutU8(&t, 17);
        DictPutInt(&t, (int32_t)fdOff);
        PutU8(&t, 12);
        PutU8(&t, 36);
        DictPutInt(&t, (int32_t)fdselectOff);
        PutU8(&t, 12);
        PutU8(&t, 37);
        DictPutInt(&t, (int32_t)vstoreOff);
        PutU8(&t, 12);
        PutU8(&t, 24);
        topLen = (size_t)(t - top);
    }

    fs = fdselect;
    PutU8(&fs, 3);
    PutU16(&fs, 1);
    PutU16(&fs, 0);
    PutU8(&fs, 0);
    PutU16(&fs, 1);
    fdsLen = (size_t)(fs - fdselect);

    memset(cff2, 0, sizeof(cff2));
    p = cff2;
    PutU8(&p, 2);
    PutU8(&p, 0);
    PutU8(&p, 5);
    PutU16(&p, (uint16_t)topLen);
    memcpy(p, top, topLen);
    p += topLen;
    PutU32(&p, 0u); /* empty global subrs */
    PutU32(&p, 1u);
    PutU8(&p, 1u);
    PutU8(&p, 1u);
    PutU8(&p, 2u);
    PutU8(&p, 0x0e);
    PutU32(&p, 1u);
    PutU8(&p, 1u);
    PutU8(&p, 1u);
    PutU8(&p, (uint8_t)(1u + fdLen));
    memcpy(p, fontDict, fdLen);
    p += fdLen;
    memcpy(p, fdselect, fdsLen);
    p += fdsLen;
    /* vstore */
    PutU16(&p, 1);
    PutU32(&p, 8);
    PutU16(&p, 0);
    PutU16(&p, 0);
    PutU16(&p, 0);
    n = (size_t)(p - cff2);

    tabs[0].tag = RoboFontTagCff2;
    tabs[0].data = cff2;
    tabs[0].length = (uint32_t)n;
    n = BuildSfnt(font, sizeof(font), RoboFontSfntVersionOtto, tabs, 1u);
    RoboCheck(n > 0u);
    RoboCheckEqInt(FontSourceCreateMemoryCopy(font, n, &src), RoboOk);
    RoboCheckEqInt(FontSfntParse(src, 0, 0, &sfnt), RoboOk);
    RoboCheckEqInt((int)FontOtDetectOutline(sfnt),
                      (int)RoboFontOutlineCff2);
    RoboCheckEqInt(FontCffParse(sfnt, RoboFontCffFlagsParseAll, &cf),
                      RoboOk);
    RoboCheckEqInt(cf->major, 2);
    RoboCheckEqInt((int)FontCffGlyphCount(cf), 1);
    RoboCheck(cf->hasVstore);
    RoboCheckEqInt(cf->vstore.format, 1);
    RoboCheckEqInt((int)cf->fdCount, 1);

    FontCffRelease(cf);
    FontSfntRelease(sfnt);
    FontSourceRelease(src);
}

static void TestCffMalformed(void)
{
    uint8_t badIx[] = {0x00, 0x01, 0x05}; /* bad offSize */
    FontSpan span = {badIx, sizeof(badIx)};
    FontCffIndex idx;
    size_t end = 0u;
    memset(&idx, 0, sizeof(idx));
    RoboCheck(FontCffIndexParse(span, 0, &idx, &end) != RoboOk);

    RoboCheck(FontCffFuzzParse((const uint8_t *)"xxxx", 4) != RoboOk);
    {
        uint8_t trunc[] = {1, 0, 4, 1, 0, 1}; /* header + truncated INDEX */
        span.data = trunc;
        span.size = sizeof(trunc);
        RoboCheck(FontCffIndexParse(span, 4, &idx, &end) != RoboOk);
    }
}

void TestRegisterFontCff(void)
{
    TestRegister("font.cff.index_dict", TestCffIndexDict);
    TestRegister("font.cff.charstring", TestCffCharstring);
    TestRegister("font.cff.charset_enc", TestCffCharsetEncodingFdselect);
    TestRegister("font.cff.bundle", TestCffFontBundle);
    TestRegister("font.cff.cff2", TestCff2AndVstore);
    TestRegister("font.cff.malformed", TestCffMalformed);
}
