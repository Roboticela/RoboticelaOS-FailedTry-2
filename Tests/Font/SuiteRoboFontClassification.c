/*
 * RoboFont style / weight / stretch classification suite (Goal 5.10).
 * Synthetic fixtures only — no Chromium / system fonts.
 */

#include "RoboFont/RoboFont.h"
#include "RoboTest.h"

#include <string.h>

static void RawClear(FontClassRaw *r)
{
    memset(r, 0, sizeof(*r));
}

static void TestWeightCategories(void)
{
    RoboCheckEqInt(MakeFontWeightCategory(100), RoboFontWeightCatThin);
    RoboCheckEqInt(MakeFontWeightCategory(200), RoboFontWeightCatExtraLight);
    RoboCheckEqInt(MakeFontWeightCategory(300), RoboFontWeightCatLight);
    RoboCheckEqInt(MakeFontWeightCategory(400), RoboFontWeightCatNormal);
    RoboCheckEqInt(MakeFontWeightCategory(500), RoboFontWeightCatMedium);
    RoboCheckEqInt(MakeFontWeightCategory(600), RoboFontWeightCatSemiBold);
    RoboCheckEqInt(MakeFontWeightCategory(700), RoboFontWeightCatBold);
    RoboCheckEqInt(MakeFontWeightCategory(800), RoboFontWeightCatExtraBold);
    RoboCheckEqInt(MakeFontWeightCategory(900), RoboFontWeightCatBlack);
    RoboCheckEqInt(MakeFontWeightCategory(1000), RoboFontWeightCatExtraBlack);
    RoboCheckEqInt(MakeFontWeightCategory(0), RoboFontWeightCatUnknown);
    RoboCheckEqInt(MakeFontWeightCategory(1001), RoboFontWeightCatUnknown);
}

static void TestWeightOs2AndBoldBit(void)
{
    FontClassRaw raw;
    FontClassification cls;

    RawClear(&raw);
    raw.hasOs2 = 1u;
    raw.usWeightClass = 350u;
    raw.usWidthClass = 5u;
    raw.fsSelection = RoboFontFsBold; /* must NOT override valid weight */
    RoboCheckEqInt(FontClassify(&raw, &cls), RoboOk);
    RoboCheckEqInt(cls.weight, 350);
    RoboCheckEqInt(cls.weightSource, RoboFontClassSrcOs2);
    RoboCheckEqInt(cls.weightAvail, RoboFontClassPresent);
    RoboCheckEqInt(cls.conflictBoldBitVsWeight, 1);

    RawClear(&raw);
    raw.hasOs2 = 1u;
    raw.usWeightClass = 0u; /* invalid */
    raw.usWidthClass = 5u;
    raw.fsSelection = RoboFontFsBold;
    RoboCheckEqInt(FontClassify(&raw, &cls), RoboOk);
    RoboCheckEqInt(cls.weight, RoboFontWeightBold);
    RoboCheckEqInt(cls.weightSource, RoboFontClassSrcFsSelection);
    RoboCheckEqInt(cls.weightAvail, RoboFontClassFallback);
    RoboCheckEqInt(cls.raw.usWeightClass, 0);

    RawClear(&raw);
    raw.hasOs2 = 1u;
    raw.usWeightClass = 2000u; /* invalid preserved */
    raw.usWidthClass = 5u;
    RoboCheckEqInt(FontClassify(&raw, &cls), RoboOk);
    RoboCheckEqInt(cls.raw.usWeightClass, 2000);
    RoboCheckEqInt(cls.weight, RoboFontWeightNormal);
    RoboCheckEqInt(cls.weightAvail, RoboFontClassInvalid);
}

static void TestWeightHeadFallback(void)
{
    FontClassRaw raw;
    FontClassification cls;

    RawClear(&raw);
    raw.hasHead = 1u;
    raw.macStyle = RoboFontMacBold;
    RoboCheckEqInt(FontClassify(&raw, &cls), RoboOk);
    RoboCheckEqInt(cls.weight, RoboFontWeightBold);
    RoboCheckEqInt(cls.weightSource, RoboFontClassSrcHead);
}

static void TestStretchClasses(void)
{
    uint16_t wc;
    FontClassRaw raw;
    FontClassification cls;

    for (wc = 1u; wc <= 9u; wc++) {
        RawClear(&raw);
        raw.hasOs2 = 1u;
        raw.usWeightClass = 400u;
        raw.usWidthClass = wc;
        RoboCheckEqInt(FontClassify(&raw, &cls), RoboOk);
        RoboCheckEqInt(cls.widthClass, (int)wc);
        RoboCheckEqInt(cls.stretch, FontStretchFromWidthClass(wc));
        RoboCheckEqInt(cls.stretchAvail, RoboFontClassPresent);
    }

    RawClear(&raw);
    raw.hasOs2 = 1u;
    raw.usWeightClass = 400u;
    raw.usWidthClass = 99u;
    RoboCheckEqInt(FontClassify(&raw, &cls), RoboOk);
    RoboCheckEqInt(cls.raw.usWidthClass, 99);
    RoboCheckEqInt(cls.stretch, RoboFontStretchNormal);
    RoboCheckEqInt(cls.stretchAvail, RoboFontClassInvalid);
    RoboCheckEqInt(cls.widthClass, RoboFontWidthUnknown);
}

static void TestStyleItalicOblique(void)
{
    FontClassRaw raw;
    FontClassification cls;

    RawClear(&raw);
    raw.hasOs2 = 1u;
    raw.usWeightClass = 400u;
    raw.usWidthClass = 5u;
    raw.fsSelection = RoboFontFsItalic;
    RoboCheckEqInt(FontClassify(&raw, &cls), RoboOk);
    RoboCheckEqInt(cls.style, RoboFontStyleItalic);
    RoboCheckEqInt(cls.styleSource, RoboFontClassSrcOs2);

    raw.fsSelection = RoboFontFsOblique;
    RoboCheckEqInt(FontClassify(&raw, &cls), RoboOk);
    RoboCheckEqInt(cls.style, RoboFontStyleOblique);

    raw.fsSelection = (uint16_t)(RoboFontFsItalic | RoboFontFsOblique);
    RoboCheckEqInt(FontClassify(&raw, &cls), RoboOk);
    RoboCheckEqInt(cls.style, RoboFontStyleOblique);
    RoboCheckEqInt(cls.bothItalicAndOblique, 1);

    raw.fsSelection =
        (uint16_t)(RoboFontFsItalic | RoboFontFsRegular);
    RoboCheckEqInt(FontClassify(&raw, &cls), RoboOk);
    RoboCheckEqInt(cls.style, RoboFontStyleItalic);
    RoboCheckEqInt(cls.conflictRegularAndSlope, 1);

    RawClear(&raw);
    raw.hasPost = 1u;
    raw.italicAngleFixed = -786432; /* -12 degrees Fixed */
    RoboCheckEqInt(FontClassify(&raw, &cls), RoboOk);
    RoboCheckEqInt(cls.style, RoboFontStyleOblique);
    RoboCheckEqInt(cls.styleSource, RoboFontClassSrcPost);
    RoboCheckEqInt(cls.hasItalicAngle, 1);
    RoboCheck(cls.italicAngleMillidegrees < 0);
}

static void TestWwsFlagAndNameLayers(void)
{
    FontClassRaw raw;
    FontClassification cls;
    FontMetadata meta;

    RawClear(&raw);
    raw.hasOs2 = 1u;
    raw.usWeightClass = 400u;
    raw.usWidthClass = 5u;
    raw.fsSelection = RoboFontFsWws;
    RoboCheckEqInt(FontClassify(&raw, &cls), RoboOk);
    RoboCheckEqInt(cls.wwsFlag, 1);

    memset(&meta, 0, sizeof(meta));
    meta.familyName.avail = RoboFontNamePresent;
    meta.subfamilyName.avail = RoboFontNamePresent;
    meta.typographicFamilyName.avail = RoboFontNamePresent;
    meta.wwsFamilyName.avail = RoboFontNamePresent;
    meta.wwsSubfamilyName.avail = RoboFontNamePresent;

    {
        FontClassificationInfo *info = NULL;
        /* Build via classify after manually filling name flags: */
        raw.hasNameMeta = 1u;
        raw.nameHasLegacyFamily = 1u;
        raw.nameHasLegacySubfamily = 1u;
        raw.nameHasTypoFamily = 1u;
        raw.nameHasWwsFamily = 1u;
        raw.nameHasWwsSubfamily = 1u;
        RoboCheckEqInt(FontClassify(&raw, &cls), RoboOk);
        RoboCheckEqInt(cls.raw.nameHasLegacyFamily, 1);
        RoboCheckEqInt(cls.raw.nameHasTypoFamily, 1);
        RoboCheckEqInt(cls.raw.nameHasWwsFamily, 1);
        RoboCheckEqInt(cls.raw.nameHasWwsSubfamily, 1);
        (void)info;
        (void)meta;
    }
}

static void TestStatAxisTagsRetained(void)
{
    /* Structural STAT axes do not override OS/2 numerics. */
    FontClassRaw raw;
    FontClassification cls;

    RawClear(&raw);
    raw.hasOs2 = 1u;
    raw.usWeightClass = 450u;
    raw.usWidthClass = 7u;
    raw.hasStat = 1u;
    raw.statAxisCount = 3u;
    raw.statHasWght = 1u;
    raw.statHasWdth = 1u;
    raw.statHasItal = 1u;
    RoboCheckEqInt(FontClassify(&raw, &cls), RoboOk);
    RoboCheckEqInt(cls.weight, 450);
    RoboCheckEqInt(cls.weightSource, RoboFontClassSrcOs2);
    RoboCheckEqInt(cls.widthClass, RoboFontWidthExpanded);
    RoboCheckEqInt(cls.raw.statHasWght, 1);
    RoboCheckEqInt(cls.raw.statHasWdth, 1);
    RoboCheckEqInt(cls.raw.statHasItal, 1);
}

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
static void PutI16(uint8_t **p, int16_t v) { PutU16(p, (uint16_t)v); }

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

static size_t BuildSfnt(uint8_t *out, size_t outCap, uint32_t version,
                         const SynthTable *tables, uint32_t n)
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
        uint8_t *rec = out + 12u + (size_t)i * 16u;
        p = rec;
        PutU32(&p, tables[i].tag);
        PutU32(&p, 0u);
        PutU32(&p, (uint32_t)cursor);
        PutU32(&p, tables[i].length);
        memcpy(out + cursor, tables[i].data, tables[i].length);
        cursor += pad;
    }
    return cursor;
}

static void PutI64(uint8_t **p, int64_t v)
{
    PutU32(p, (uint32_t)((uint64_t)v >> 32));
    PutU32(p, (uint32_t)((uint64_t)v));
}

static size_t BuildHeadOk(uint8_t *buf, uint16_t macStyle)
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
    PutU16(&p, macStyle);
    PutU16(&p, 9);
    PutI16(&p, 2);
    PutI16(&p, 0); /* short loca */
    PutI16(&p, 0);
    return (size_t)(p - buf);
}

static size_t BuildHhea(uint8_t *b)
{
    uint8_t *p = b;
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
    PutU16(&p, 1);
    return (size_t)(p - b);
}

static size_t BuildMaxp(uint8_t *b)
{
    uint8_t *p = b;
    int i;
    PutU32(&p, 0x00010000u);
    PutU16(&p, 1); /* numGlyphs */
    for (i = 0; i < 13; i++) {
        PutU16(&p, 0);
    }
    return (size_t)(p - b);
}

static size_t BuildHmtx(uint8_t *b)
{
    uint8_t *p = b;
    PutU16(&p, 500);
    PutI16(&p, 0);
    return (size_t)(p - b);
}

static size_t BuildLoca(uint8_t *b)
{
    uint8_t *p = b;
    PutU16(&p, 0);
    PutU16(&p, 0);
    return (size_t)(p - b);
}

static size_t BuildGlyf(uint8_t *b)
{
    (void)b;
    return 0u;
}

static size_t BuildPost(uint8_t *b, int32_t angleFixed)
{
    uint8_t *p = b;
    PutU32(&p, 0x00030000u);
    PutU32(&p, (uint32_t)angleFixed);
    PutI16(&p, -100);
    PutI16(&p, 50);
    PutU32(&p, 0);
    PutU32(&p, 0);
    PutU32(&p, 0);
    PutU32(&p, 0);
    PutU32(&p, 0);
    return (size_t)(p - b);
}

static size_t BuildOs2(uint8_t *buf, uint16_t weight, uint16_t width, uint16_t fs)
{
    uint8_t *p = buf;
    int i;
    PutU16(&p, 4); /* version with OBLIQUE bit defined */
    PutI16(&p, 500);
    PutU16(&p, weight);
    PutU16(&p, width);
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
    PutU16(&p, fs);
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

static size_t BuildStatWght(uint8_t *buf)
{
    uint8_t *p = buf;
    /* version 1.1 header + 1 axis wght */
    PutU16(&p, 1);
    PutU16(&p, 1);
    PutU16(&p, 8); /* designAxisSize */
    PutU16(&p, 1); /* designAxisCount */
    PutU32(&p, 20); /* offset to axes from table start */
    PutU16(&p, 0); /* axisValueCount */
    PutU32(&p, 0); /* offsetToAxisValueOffsets */
    PutU16(&p, 2); /* elidedFallbackNameID */
    /* axis at offset 20 */
    PutU32(&p, RoboFontTag('w', 'g', 'h', 't'));
    PutU16(&p, 256);
    PutU16(&p, 0);
    return (size_t)(p - buf);
}

static void TestFaceClassificationIntegration(void)
{
    uint8_t head[64], hhea[64], maxp[64], hmtx[8], loca[8], post[64], os2[128], stat[64];
    uint8_t font[512];
    size_t n;
    size_t nh, nhea, nm, nhm, nl, np, no, ns;
    SynthTable tables[8];
    FontSource *src = NULL;
    FontFace *face = NULL;
    const FontClassification *cls;

    nh = BuildHeadOk(head, 0);
    nhea = BuildHhea(hhea);
    nm = BuildMaxp(maxp);
    nhm = BuildHmtx(hmtx);
    nl = BuildLoca(loca);
    np = BuildPost(post, 0);
    no = BuildOs2(os2, 600, 3, RoboFontFsItalic);
    ns = BuildStatWght(stat);

    tables[0].tag = RoboFontTagHead;
    tables[0].data = head;
    tables[0].length = (uint32_t)nh;
    tables[1].tag = RoboFontTagHhea;
    tables[1].data = hhea;
    tables[1].length = (uint32_t)nhea;
    tables[2].tag = RoboFontTagMaxp;
    tables[2].data = maxp;
    tables[2].length = (uint32_t)nm;
    tables[3].tag = RoboFontTagHmtx;
    tables[3].data = hmtx;
    tables[3].length = (uint32_t)nhm;
    tables[4].tag = RoboFontTagLoca;
    tables[4].data = loca;
    tables[4].length = (uint32_t)nl;
    tables[5].tag = RoboFontTagGlyf;
    tables[5].data = head; /* empty glyf: length 0 */
    tables[5].length = 0u;
    tables[6].tag = RoboFontTagPost;
    tables[6].data = post;
    tables[6].length = (uint32_t)np;
    tables[7].tag = RoboFontTagOs2;
    tables[7].data = os2;
    tables[7].length = (uint32_t)no;

    n = BuildSfnt(font, sizeof(font), 0x00010000u, tables, 8u);
    RoboCheck(n > 0u);

    RoboCheckEqInt(FontSourceCreateMemoryCopy(font, n, &src), RoboOk);
    RoboCheckEqInt(FontFaceCreate(src, 0u, &face), RoboOk);
    RoboCheckEqInt(FontFaceParseClassification(face), RoboOk);
    cls = FontFaceClassification(face);
    RoboCheck(cls != NULL);
    RoboCheckEqInt(cls->weight, 600);
    RoboCheckEqInt(cls->weightCategory, RoboFontWeightCatSemiBold);
    RoboCheckEqInt(cls->widthClass, RoboFontWidthCondensed);
    RoboCheckEqInt(cls->style, RoboFontStyleItalic);
    RoboCheckEqInt(cls->raw.usWeightClass, 600);
    RoboCheckEqInt(cls->raw.usWidthClass, 3);

    /* Add STAT in a second font to verify axis retention without override */
    {
        SynthTable t2[9];
        uint8_t font2[640];
        size_t n2;
        memcpy(t2, tables, sizeof(tables));
        t2[8].tag = RoboFontTagStat;
        t2[8].data = stat;
        t2[8].length = (uint32_t)ns;
        n2 = BuildSfnt(font2, sizeof(font2), 0x00010000u, t2, 9u);
        RoboCheck(n2 > 0u);
        FontSourceRelease(src);
        FontFaceRelease(face);
        src = NULL;
        face = NULL;
        RoboCheckEqInt(FontSourceCreateMemoryCopy(font2, n2, &src), RoboOk);
        RoboCheckEqInt(FontFaceCreate(src, 0u, &face), RoboOk);
        RoboCheckEqInt(FontFaceParseClassification(face), RoboOk);
        cls = FontFaceClassification(face);
        RoboCheck(cls != NULL);
        RoboCheckEqInt(cls->weight, 600);
        RoboCheckEqInt(cls->raw.statHasWght, 1);
        RoboCheckEqInt(cls->weightSource, RoboFontClassSrcOs2);
    }

    FontFaceRelease(face);
    FontSourceRelease(src);
    (void)BuildGlyf;
}

static void TestFuzzClassification(void)
{
    uint8_t junk[64];
    size_t i;
    for (i = 0u; i < sizeof(junk); i++) {
        junk[i] = (uint8_t)(i * 37u);
    }
    RoboCheckEqInt(FontClassificationFuzzParse(junk, sizeof(junk)), RoboOk);
    RoboCheckEqInt(FontClassificationFuzzParse(junk, 0u), RoboOk);
}

void TestRegisterFontClassification(void)
{
    TestRegister("font.class.weight_categories", TestWeightCategories);
    TestRegister("font.class.weight_os2_bold", TestWeightOs2AndBoldBit);
    TestRegister("font.class.weight_head", TestWeightHeadFallback);
    TestRegister("font.class.stretch", TestStretchClasses);
    TestRegister("font.class.style", TestStyleItalicOblique);
    TestRegister("font.class.wws_name", TestWwsFlagAndNameLayers);
    TestRegister("font.class.stat_axes", TestStatAxisTagsRetained);
    TestRegister("font.class.face", TestFaceClassificationIntegration);
    TestRegister("font.class.fuzz", TestFuzzClassification);
}
