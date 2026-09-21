/*
 * RoboFont naming & metadata suite (Goal 5.9).
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

static void PutUtf16be(uint8_t **p, const char *ascii)
{
    while (*ascii) {
        PutU8(p, 0);
        PutU8(p, (uint8_t)*ascii++);
    }
}

static size_t BuildNameMulti(uint8_t *buf, size_t cap)
{
    /* format 0, 4 records: family en-US, family de-DE, subfamily, PS, version, typo family */
    uint8_t storage[256];
    uint8_t *s = storage;
    uint8_t *p = buf;
    size_t storageOff;
    uint16_t offTest, offProbe, offReg, offPs, offVer, offTypo;
    const char *ps = "TestPS";

    if (cap < 200u) {
        return 0u;
    }
    offTest = (uint16_t)(s - storage);
    PutUtf16be(&s, "Test");
    offProbe = (uint16_t)(s - storage);
    PutUtf16be(&s, "Probe");
    offReg = (uint16_t)(s - storage);
    PutUtf16be(&s, "Regular");
    offPs = (uint16_t)(s - storage);
    PutUtf16be(&s, ps);
    offVer = (uint16_t)(s - storage);
    PutUtf16be(&s, "Version 1.2.3");
    offTypo = (uint16_t)(s - storage);
    PutUtf16be(&s, "TypoFamily");

    storageOff = 6u + 6u * 12u;
    PutU16(&p, 0);
    PutU16(&p, 6);
    PutU16(&p, (uint16_t)storageOff);

    /* en-US family */
    PutU16(&p, 3);
    PutU16(&p, 1);
    PutU16(&p, 0x0409);
    PutU16(&p, RoboFontNameIdFamily);
    PutU16(&p, 8);
    PutU16(&p, offTest);
    /* de-DE family */
    PutU16(&p, 3);
    PutU16(&p, 1);
    PutU16(&p, 0x0407);
    PutU16(&p, RoboFontNameIdFamily);
    PutU16(&p, 10);
    PutU16(&p, offProbe);
    /* subfamily */
    PutU16(&p, 3);
    PutU16(&p, 1);
    PutU16(&p, 0x0409);
    PutU16(&p, RoboFontNameIdSubfamily);
    PutU16(&p, 14);
    PutU16(&p, offReg);
    /* PostScript */
    PutU16(&p, 3);
    PutU16(&p, 1);
    PutU16(&p, 0x0409);
    PutU16(&p, RoboFontNameIdPostscript);
    PutU16(&p, (uint16_t)(strlen(ps) * 2u));
    PutU16(&p, offPs);
    /* version */
    PutU16(&p, 3);
    PutU16(&p, 1);
    PutU16(&p, 0x0409);
    PutU16(&p, RoboFontNameIdVersion);
    PutU16(&p, (uint16_t)(strlen("Version 1.2.3") * 2u));
    PutU16(&p, offVer);
    /* typographic family */
    PutU16(&p, 3);
    PutU16(&p, 1);
    PutU16(&p, 0x0409);
    PutU16(&p, RoboFontNameIdTypoFamily);
    PutU16(&p, (uint16_t)(strlen("TypoFamily") * 2u));
    PutU16(&p, offTypo);

    memcpy(buf + storageOff, storage, (size_t)(s - storage));
    return storageOff + (size_t)(s - storage);
}

static size_t BuildNameMacRoman(uint8_t *buf, size_t cap)
{
    uint8_t *p = buf;
    size_t storageOff = 6u + 12u;
    const char *s = "MacFace";
    if (cap < storageOff + 8u) {
        return 0u;
    }
    PutU16(&p, 0);
    PutU16(&p, 1);
    PutU16(&p, (uint16_t)storageOff);
    PutU16(&p, 1); /* Mac */
    PutU16(&p, 0); /* Roman */
    PutU16(&p, 0); /* English */
    PutU16(&p, RoboFontNameIdFamily);
    PutU16(&p, (uint16_t)strlen(s));
    PutU16(&p, 0);
    memcpy(buf + storageOff, s, strlen(s));
    return storageOff + strlen(s);
}

static size_t BuildNameSurrogate(uint8_t *buf, size_t cap)
{
    /* U+1F600 😀 as UTF-16BE surrogate pair in full name */
    uint8_t *p = buf;
    size_t storageOff = 6u + 12u;
    uint8_t emoji[] = {0xD8, 0x3D, 0xDE, 0x00}; /* wrong - 1F600 is D83D DE00 */
    if (cap < storageOff + 8u) {
        return 0u;
    }
    emoji[0] = 0xD8;
    emoji[1] = 0x3D;
    emoji[2] = 0xDE;
    emoji[3] = 0x00;
    PutU16(&p, 0);
    PutU16(&p, 1);
    PutU16(&p, (uint16_t)storageOff);
    PutU16(&p, 3);
    PutU16(&p, 1);
    PutU16(&p, 0x0409);
    PutU16(&p, RoboFontNameIdFull);
    PutU16(&p, 4);
    PutU16(&p, 0);
    memcpy(buf + storageOff, emoji, 4);
    return storageOff + 4u;
}

static void TestNameMetadataBasic(void)
{
    uint8_t name[512];
    uint8_t font[1024];
    SynthTable tabs[1];
    size_t nn = BuildNameMulti(name, sizeof(name));
    size_t n;
    FontSource *src = NULL;
    FontFace *face = NULL;
    const FontMetadata *meta;
    FontMetaField pref;
    I18nLocale de;
    const FontNameEntry *ent = NULL;
    FontNameLookupKind kind;

    RoboCheck(nn > 0u);
    tabs[0].tag = RoboFontTagName;
    tabs[0].data = name;
    tabs[0].length = (uint32_t)nn;
    n = BuildSfnt(font, sizeof(font), RoboFontSfntVersionTrue, tabs, 1);
    RoboCheck(n > 0u);

    RoboCheckEqInt(FontSourceCreateMemoryCopy(font, n, &src), RoboOk);
    RoboCheckEqInt(FontFaceCreate(src, 0, &face), RoboOk);
    RoboCheckEqInt(FontFaceParseMetadata(face), RoboOk);
    meta = FontFaceMetadata(face);
    RoboCheck(meta != NULL);
    RoboCheckEqInt((int)meta->familyName.avail, (int)RoboFontNamePresent);
    RoboCheckStrEq(meta->familyName.utf8, "Test");
    RoboCheckStrEq(meta->subfamilyName.utf8, "Regular");
    RoboCheckStrEq(meta->postscriptName.utf8, "TestPS");
    RoboCheck(meta->postscriptNameStructurallyOk);
    RoboCheckStrEq(meta->typographicFamilyName.utf8, "TypoFamily");
    RoboCheck(meta->versionParsed.valid);
    RoboCheckEqInt((int)meta->versionParsed.major, 1);
    RoboCheckEqInt((int)meta->versionParsed.minor, 2);

    RoboCheckEqInt(FontMetadataPreferredFamily(meta, &pref), RoboOk);
    RoboCheckStrEq(pref.utf8, "TypoFamily");
    RoboCheckEqInt((int)pref.avail, (int)RoboFontNamePresent);

    RoboCheckEqInt(I18nLocaleParse("de-DE", 5, &de), RoboOk);
    RoboCheckEqInt(FontNameInfoLookup(FontFaceNameInfo(face),
                                                 RoboFontNameIdFamily, &de, &ent, &kind),
                      RoboOk);
    RoboCheckStrEq(ent->utf8, "Probe");
    RoboCheckEqInt((int)kind, (int)RoboFontNameLookupExactLocale);

    RoboCheckEqInt(FontNameFuzzParse(font, n), RoboOk);
    RoboCheckEqInt(FontNameFuzzParse(name, nn), RoboOk);

    FontFaceRelease(face);
    FontSourceRelease(src);
}

static void TestNameMacRoman(void)
{
    uint8_t name[128];
    size_t nn = BuildNameMacRoman(name, sizeof(name));
    FontSpan span = {name, nn};
    FontNameInfo *info = NULL;
    const FontMetadata *meta;

    RoboCheckEqInt(FontNameInfoFromSpan(span, NULL, &info), RoboOk);
    meta = FontNameInfoMetadata(info);
    RoboCheckStrEq(meta->familyName.utf8, "MacFace");
    FontNameInfoRelease(info);
}

static void TestNameUtf16Surrogate(void)
{
    uint8_t name[64];
    size_t nn = BuildNameSurrogate(name, sizeof(name));
    FontSpan span = {name, nn};
    FontNameInfo *info = NULL;
    const FontMetadata *meta;

    RoboCheckEqInt(FontNameInfoFromSpan(span, NULL, &info), RoboOk);
    meta = FontNameInfoMetadata(info);
    RoboCheckEqInt((int)meta->fullName.avail, (int)RoboFontNamePresent);
    RoboCheck(meta->fullName.utf8Len >= 4u); /* 4-byte UTF-8 emoji */
    FontNameInfoRelease(info);
}

static void TestNameMalformed(void)
{
    uint8_t bad[] = {0x00, 0x00, 0xff, 0xff, 0x00, 0x06}; /* huge count */
    FontSpan span = {bad, sizeof(bad)};
    FontNameInfo *info = NULL;
    RoboCheck(FontNameInfoFromSpan(span, NULL, &info) != RoboOk);
    RoboCheckEqInt(FontNameFuzzParse(bad, sizeof(bad)), RoboOk);

    /* truncated UTF-16 */
    {
        uint8_t name[32];
        uint8_t *p = name;
        size_t storageOff = 6u + 12u;
        PutU16(&p, 0);
        PutU16(&p, 1);
        PutU16(&p, (uint16_t)storageOff);
        PutU16(&p, 3);
        PutU16(&p, 1);
        PutU16(&p, 0x0409);
        PutU16(&p, RoboFontNameIdFamily);
        PutU16(&p, 3); /* odd length */
        PutU16(&p, 0);
        name[storageOff] = 0;
        name[storageOff + 1] = 'A';
        name[storageOff + 2] = 0;
        span.data = name;
        span.size = storageOff + 3u;
        /* parse may fail on odd length at record validation (length ok if storage fits)
         * length 3 with storage size 3 is OK structurally; decode → MALFORMED */
        if (FontNameInfoFromSpan(span, NULL, &info) == RoboOk) {
            const FontNameEntry *e = FontNameInfoEntry(info, 0);
            RoboCheck(e != NULL);
            RoboCheckEqInt((int)e->decodeStatus, (int)RoboFontNameMalformed);
            FontNameInfoRelease(info);
        }
    }
}

static void TestNameDuplicates(void)
{
    /* two identical en-US family records — first wins among equal platform score */
    uint8_t name[128];
    uint8_t *p = name;
    uint8_t storage[16];
    uint8_t *s = storage;
    size_t storageOff = 6u + 24u;
    size_t i;
    FontSpan span;
    FontNameInfo *info = NULL;
    const FontNameEntry *ent = NULL;

    PutUtf16be(&s, "One");
    PutUtf16be(&s, "Two");
    PutU16(&p, 0);
    PutU16(&p, 2);
    PutU16(&p, (uint16_t)storageOff);
    for (i = 0u; i < 2u; i++) {
        PutU16(&p, 3);
        PutU16(&p, 1);
        PutU16(&p, 0x0409);
        PutU16(&p, RoboFontNameIdFamily);
        PutU16(&p, 6);
        PutU16(&p, (uint16_t)(i * 6u));
    }
    memcpy(name + storageOff, storage, (size_t)(s - storage));
    span.data = name;
    span.size = storageOff + (size_t)(s - storage);
    RoboCheckEqInt(FontNameInfoFromSpan(span, NULL, &info), RoboOk);
    RoboCheckEqInt(FontNameInfoLookup(info, RoboFontNameIdFamily, NULL, &ent,
                                                 NULL),
                      RoboOk);
    RoboCheckStrEq(ent->utf8, "One");
    FontNameInfoRelease(info);
}

static void TestNameUnknownId(void)
{
    uint8_t name[64];
    uint8_t *p = name;
    size_t storageOff = 6u + 12u;
    uint8_t bytes[] = {0x00, 'X'};
    FontSpan span;
    FontNameInfo *info = NULL;
    const FontNameEntry *ent = NULL;

    PutU16(&p, 0);
    PutU16(&p, 1);
    PutU16(&p, (uint16_t)storageOff);
    PutU16(&p, 3);
    PutU16(&p, 1);
    PutU16(&p, 0x0409);
    PutU16(&p, 300); /* font-specific / unknown */
    PutU16(&p, 2);
    PutU16(&p, 0);
    memcpy(name + storageOff, bytes, 2);
    span.data = name;
    span.size = storageOff + 2u;
    RoboCheckEqInt(FontNameInfoFromSpan(span, NULL, &info), RoboOk);
    RoboCheckEqInt(FontNameInfoLookup(info, 300, NULL, &ent, NULL), RoboOk);
    RoboCheckStrEq(ent->utf8, "X");
    FontNameInfoRelease(info);
}

void TestRegisterFontName(void)
{
    TestRegister("font.name.metadata_basic", TestNameMetadataBasic);
    TestRegister("font.name.mac_roman", TestNameMacRoman);
    TestRegister("font.name.utf16_surrogate", TestNameUtf16Surrogate);
    TestRegister("font.name.malformed", TestNameMalformed);
    TestRegister("font.name.duplicates", TestNameDuplicates);
    TestRegister("font.name.unknown_id", TestNameUnknownId);
}
