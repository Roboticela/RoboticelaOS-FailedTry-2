#include "RoboFont/RoboFontName.h"

#include "RoboFont/RoboFontBinary.h"
#include "RoboFont/RoboFontSource.h"
#include "RoboMem/RoboMem.h"
#include "RoboUnicode/RoboUnicode.h"

#include <string.h>

struct FontNameInfo {
    uint32_t refs;
    uint32_t entryCount;
    FontNameEntry *entries;
    char **ownedStrings; /* parallel to entries; may be NULL slot */
    size_t totalDecoded;
    FontMetadata meta;
    /* metadata string storage (separate from entry strings when shared via pointer) */
    char *metaScratch[20];
    uint32_t metaScratchN;
};

/* MacRoman 0x80..0xFF → Unicode (Apple Mac OS Roman). */
static const uint16_t kMacRomanHi[128] = {
    0x00C4, 0x00C5, 0x00C7, 0x00C9, 0x00D1, 0x00D6, 0x00DC, 0x00E1, 0x00E0, 0x00E2,
    0x00E4, 0x00E3, 0x00E5, 0x00E7, 0x00E9, 0x00E8, 0x00EA, 0x00EB, 0x00ED, 0x00EC,
    0x00EE, 0x00EF, 0x00F1, 0x00F3, 0x00F2, 0x00F4, 0x00F6, 0x00F5, 0x00FA, 0x00F9,
    0x00FB, 0x00FC, 0x2020, 0x00B0, 0x00A2, 0x00A3, 0x00A7, 0x2022, 0x00B6, 0x00DF,
    0x00AE, 0x00A9, 0x2122, 0x00B4, 0x00A8, 0x2260, 0x00C6, 0x00D8, 0x221E, 0x00B1,
    0x2264, 0x2265, 0x00A5, 0x00B5, 0x2202, 0x2211, 0x220F, 0x03C0, 0x222B, 0x00AA,
    0x00BA, 0x03A9, 0x00E6, 0x00F8, 0x00BF, 0x00A1, 0x00AC, 0x221A, 0x0192, 0x2248,
    0x2206, 0x00AB, 0x00BB, 0x2026, 0x00A0, 0x00C0, 0x00C3, 0x00D5, 0x0152, 0x0153,
    0x2013, 0x2014, 0x201C, 0x201D, 0x2018, 0x2019, 0x00F7, 0x25CA, 0x00FF, 0x0178,
    0x2044, 0x20AC, 0x2039, 0x203A, 0xFB01, 0xFB02, 0x2021, 0x00B7, 0x201A, 0x201E,
    0x2030, 0x00C2, 0x00CA, 0x00C1, 0x00CB, 0x00C8, 0x00CD, 0x00CE, 0x00CF, 0x00CC,
    0x00D3, 0x00D4, 0xF8FF, 0x00D2, 0x00DA, 0x00DB, 0x00D9, 0x0131, 0x02C6, 0x02DC,
    0x00AF, 0x02D8, 0x02D9, 0x02DA, 0x00B8, 0x02DD, 0x02DB, 0x02C7};

typedef struct {
    uint16_t lcid;
    const char *tag;
} WinLcidMap;

static const WinLcidMap kWinLcid[] = {
    {0x0409u, "en-US"}, {0x0809u, "en-GB"}, {0x0c09u, "en-AU"}, {0x1009u, "en-CA"},
    {0x0407u, "de-DE"}, {0x0807u, "de-CH"}, {0x040cu, "fr-FR"}, {0x0c0cu, "fr-CA"},
    {0x0410u, "it-IT"}, {0x0413u, "nl-NL"}, {0x0414u, "nb-NO"}, {0x0814u, "nn-NO"},
    {0x041du, "sv-SE"}, {0x0406u, "da-DK"}, {0x040bu, "fi-FI"}, {0x040au, "es-ES"},
    {0x0c0au, "es-ES"}, {0x080au, "es-MX"}, {0x0416u, "pt-BR"}, {0x0816u, "pt-PT"},
    {0x0415u, "pl-PL"}, {0x0419u, "ru-RU"}, {0x0422u, "uk-UA"}, {0x0405u, "cs-CZ"},
    {0x040eu, "hu-HU"}, {0x041fu, "tr-TR"}, {0x0408u, "el-GR"}, {0x040du, "he-IL"},
    {0x0401u, "ar-SA"}, {0x0411u, "ja-JP"}, {0x0412u, "ko-KR"}, {0x0804u, "zh-CN"},
    {0x0404u, "zh-TW"}, {0x0c04u, "zh-HK"}, {0x041eu, "th-TH"}, {0x042au, "vi-VN"},
    {0x0424u, "sl-SI"}, {0x041bu, "sk-SK"}, {0x0418u, "ro-RO"}, {0x0402u, "bg-BG"},
    {0x041au, "hr-HR"}, {0x0421u, "id-ID"}, {0x0427u, "lt-LT"}, {0x0426u, "lv-LV"},
    {0x0425u, "et-EE"}, {0x0436u, "af-ZA"}, {0x042du, "eu-ES"}, {0x042fu, "mk-MK"},
};

typedef struct {
    uint16_t mid;
    const char *tag;
} MacLangMap;

static const MacLangMap kMacLang[] = {
    {0u, "en"},  {1u, "fr"},  {2u, "de"},  {3u, "it"},  {4u, "nl"},  {5u, "sv"},
    {6u, "es"},  {7u, "da"},  {8u, "pt"},  {9u, "no"},  {10u, "he"}, {11u, "ja"},
    {12u, "ar"}, {13u, "fi"}, {14u, "el"}, {15u, "is"}, {16u, "mt"}, {17u, "tr"},
    {18u, "hr"}, {19u, "zh-Hant"}, {20u, "ur"}, {21u, "hi"}, {22u, "th"},
    {23u, "ko"}, {24u, "lt"}, {25u, "pl"}, {26u, "hu"}, {27u, "et"}, {28u, "lv"},
    {29u, "se"}, {30u, "fo"}, {31u, "fa"}, {32u, "ru"}, {33u, "zh-Hans"},
    {34u, "nl-BE"}, {35u, "ga"}, {36u, "sq"}, {37u, "ro"}, {38u, "cs"}, {39u, "sk"},
    {40u, "sl"}, {41u, "yi"}, {42u, "sr"}, {43u, "mk"}, {44u, "bg"}, {45u, "uk"},
    {48u, "vi"}, {49u, "id"},
};

static void LocaleFromTag(const char *tag, I18nLocale *out, uint8_t *has)
{
    *has = 0u;
    I18nLocaleClear(out);
    if (!tag || !tag[0]) {
        return;
    }
    if (I18nLocaleParseCanonical(tag, strlen(tag), out) == RoboOk) {
        *has = 1u;
    }
}

static void MapWinLang(uint16_t langId, char *tagOut, size_t tagCap, uint8_t *has)
{
    size_t i;
    *has = 0u;
    if (!tagOut || tagCap == 0u) {
        return;
    }
    tagOut[0] = '\0';
    if (langId >= 0x8000u) {
        return; /* format-1 lang tag handled separately */
    }
    for (i = 0u; i < sizeof(kWinLcid) / sizeof(kWinLcid[0]); i++) {
        if (kWinLcid[i].lcid == langId) {
            size_t n = strlen(kWinLcid[i].tag);
            if (n + 1u > tagCap) {
                return;
            }
            memcpy(tagOut, kWinLcid[i].tag, n + 1u);
            *has = 1u;
            return;
        }
    }
    /* primary language only from LCID low 10 bits when unknown region */
    {
        uint16_t primary = (uint16_t)(langId & 0x3ffu);
        const char *p = NULL;
        if (primary == 0x09u) {
            p = "en";
        } else if (primary == 0x07u) {
            p = "de";
        } else if (primary == 0x0cu) {
            p = "fr";
        } else if (primary == 0x0au) {
            p = "es";
        } else if (primary == 0x11u) {
            p = "ja";
        } else if (primary == 0x12u) {
            p = "ko";
        } else if (primary == 0x04u) {
            p = "zh";
        } else if (primary == 0x19u) {
            p = "ru";
        }
        if (p) {
            size_t n = strlen(p);
            if (n + 1u <= tagCap) {
                memcpy(tagOut, p, n + 1u);
                *has = 1u;
            }
        }
    }
}

static void MapMacLang(uint16_t langId, char *tagOut, size_t tagCap, uint8_t *has)
{
    size_t i;
    *has = 0u;
    if (!tagOut || tagCap == 0u) {
        return;
    }
    tagOut[0] = '\0';
    for (i = 0u; i < sizeof(kMacLang) / sizeof(kMacLang[0]); i++) {
        if (kMacLang[i].mid == langId) {
            size_t n = strlen(kMacLang[i].tag);
            if (n + 1u > tagCap) {
                return;
            }
            memcpy(tagOut, kMacLang[i].tag, n + 1u);
            *has = 1u;
            return;
        }
    }
}

static Result DecodeUtf16be(const uint8_t *bytes,
                                    size_t len,
                                    char **outUtf8,
                                    size_t *outLen)
{
    Uchar16 *u16;
    size_t nunits;
    size_t ui;
    size_t utf8Len = 0u;
    char *buf;
    UnicodeStatus st;

    *outUtf8 = NULL;
    *outLen = 0u;
    if ((len & 1u) != 0u) {
        return RoboParseError;
    }
    nunits = len / 2u;
    u16 = (Uchar16 *)RoboCalloc(nunits == 0u ? 1u : nunits, sizeof(*u16));
    if (!u16) {
        return RoboOutOfMemory;
    }
    for (ui = 0u; ui < nunits; ui++) {
        u16[ui] = (Uchar16)(((uint16_t)bytes[ui * 2u] << 8) |
                                   (uint16_t)bytes[ui * 2u + 1u]);
    }
    st = UnicodeUtf16ToUtf8(u16, nunits, NULL, 0u, &utf8Len, RoboUnicodeStrict);
    if (st != RoboUnicodeOk && st != RoboUnicodeNoSpace) {
        RoboFree(u16);
        return RoboParseError;
    }
    if (utf8Len > RoboFontNameMaxString) {
        RoboFree(u16);
        return RoboParseError;
    }
    buf = (char *)RoboCalloc(utf8Len + 1u, 1u);
    if (!buf) {
        RoboFree(u16);
        return RoboOutOfMemory;
    }
    st = UnicodeUtf16ToUtf8(u16, nunits, buf, utf8Len + 1u, &utf8Len,
                                    RoboUnicodeStrict);
    RoboFree(u16);
    if (st != RoboUnicodeOk) {
        RoboFree(buf);
        return RoboParseError;
    }
    buf[utf8Len] = '\0';
    *outUtf8 = buf;
    *outLen = utf8Len;
    return RoboOk;
}

static Result DecodeMacRoman(const uint8_t *bytes,
                                      size_t len,
                                      char **outUtf8,
                                      size_t *outLen)
{
    /* Worst case: each byte → 3 UTF-8 bytes (BMP) */
    size_t cap = len * 3u + 1u;
    char *buf;
    size_t w = 0u;
    size_t i;
    if (len > RoboFontNameMaxString) {
        return RoboParseError;
    }
    buf = (char *)RoboCalloc(cap, 1u);
    if (!buf) {
        return RoboOutOfMemory;
    }
    for (i = 0u; i < len; i++) {
        uint32_t cp = bytes[i];
        size_t n = 0u;
        if (cp >= 0x80u) {
            cp = kMacRomanHi[cp - 0x80u];
        }
        if (UnicodeUtf8Encode((Uchar32)cp, buf + w, cap - w, &n) !=
            RoboUnicodeOk) {
            RoboFree(buf);
            return RoboParseError;
        }
        w += n;
    }
    buf[w] = '\0';
    *outUtf8 = buf;
    *outLen = w;
    return RoboOk;
}

static Result DecodeRecordBytes(uint16_t platform,
                                         uint16_t encoding,
                                         const uint8_t *bytes,
                                         size_t len,
                                         char **outUtf8,
                                         size_t *outLen,
                                         FontNameAvail *status)
{
    *outUtf8 = NULL;
    *outLen = 0u;
    *status = RoboFontNameUnsupported;

    if (platform == RoboFontNamePlatformWindows &&
        (encoding == RoboFontNameWinEncUnicodeBmp ||
         encoding == RoboFontNameWinEncUnicodeFull ||
         encoding == RoboFontNameWinEncSymbol)) {
        Result r = DecodeUtf16be(bytes, len, outUtf8, outLen);
        if (r == RoboOk) {
            *status = RoboFontNamePresent;
            return RoboOk;
        }
        if (r == RoboOutOfMemory) {
            return r;
        }
        *status = RoboFontNameMalformed;
        return RoboOk;
    }
    if (platform == RoboFontNamePlatformUnicode) {
        /* Unicode platform: UTF-16BE for encoding 3/4 (UTF-16) and commonly 0–2 */
        if (encoding <= 4u) {
            Result r = DecodeUtf16be(bytes, len, outUtf8, outLen);
            if (r == RoboOk) {
                *status = RoboFontNamePresent;
                return RoboOk;
            }
            if (r == RoboOutOfMemory) {
                return r;
            }
            *status = RoboFontNameMalformed;
            return RoboOk;
        }
        return RoboOk;
    }
    if (platform == RoboFontNamePlatformMacintosh &&
        encoding == RoboFontNameMacEncRoman) {
        Result r = DecodeMacRoman(bytes, len, outUtf8, outLen);
        if (r == RoboOk) {
            *status = RoboFontNamePresent;
            return RoboOk;
        }
        if (r == RoboOutOfMemory) {
            return r;
        }
        *status = RoboFontNameMalformed;
        return RoboOk;
    }
    return RoboOk;
}

static void FillLocaleForRecord(uint16_t platform,
                                   uint16_t languageId,
                                   const char *langTagOverride,
                                   char *tagOut,
                                   size_t tagCap,
                                   uint8_t *has)
{
    *has = 0u;
    tagOut[0] = '\0';
    if (langTagOverride && langTagOverride[0]) {
        size_t n = strlen(langTagOverride);
        if (n + 1u <= tagCap) {
            memcpy(tagOut, langTagOverride, n + 1u);
            *has = 1u;
        }
        return;
    }
    if (platform == RoboFontNamePlatformWindows) {
        MapWinLang(languageId, tagOut, tagCap, has);
    } else if (platform == RoboFontNamePlatformMacintosh) {
        MapMacLang(languageId, tagOut, tagCap, has);
    }
}

static int PlatformScore(uint16_t platform, uint16_t encoding, uint16_t language)
{
    int s = 0;
    if (platform == RoboFontNamePlatformWindows &&
        (encoding == RoboFontNameWinEncUnicodeBmp ||
         encoding == RoboFontNameWinEncUnicodeFull)) {
        s = 40;
        if (language == 0x0409u) {
            s += 10;
        }
    } else if (platform == RoboFontNamePlatformUnicode) {
        s = 30;
    } else if (platform == RoboFontNamePlatformMacintosh &&
               encoding == RoboFontNameMacEncRoman) {
        s = 10;
    } else if (platform == RoboFontNamePlatformWindows) {
        s = 5;
    }
    return s;
}

static int PsNameOk(const char *s, size_t n)
{
    size_t i;
    if (!s || n == 0u || n > 63u) {
        return 0;
    }
    for (i = 0u; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c < 0x21u || c > 0x7eu) {
            return 0;
        }
        if (c == ' ' || c == '[' || c == ']' || c == '(' || c == ')' || c == '{' ||
            c == '}' || c == '<' || c == '>' || c == '/' || c == '%') {
            return 0;
        }
    }
    return 1;
}

static void ParseVersionString(const char *s, FontVersionParsed *out)
{
    const char *p;
    unsigned maj = 0u, min = 0u, rev = 0u;
    memset(out, 0, sizeof(*out));
    if (!s) {
        return;
    }
    p = s;
    if ((p[0] == 'V' || p[0] == 'v') && strncmp(p, "Version", 7) == 0) {
        p += 7;
        while (*p == ' ' || *p == '\t') {
            p++;
        }
    }
    while (*p >= '0' && *p <= '9') {
        maj = maj * 10u + (unsigned)(*p - '0');
        if (maj > 65535u) {
            return;
        }
        p++;
    }
    if (*p != '.') {
        return;
    }
    p++;
    while (*p >= '0' && *p <= '9') {
        min = min * 10u + (unsigned)(*p - '0');
        if (min > 65535u) {
            return;
        }
        p++;
    }
    if (*p == '.') {
        p++;
        while (*p >= '0' && *p <= '9') {
            rev = rev * 10u + (unsigned)(*p - '0');
            if (rev > 65535u) {
                return;
            }
            p++;
        }
    }
    out->valid = 1u;
    out->major = (uint16_t)maj;
    out->minor = (uint16_t)min;
    out->revision = (uint16_t)rev;
}

/* Parse format-1 language tags into allocated UTF-8 BCP47 strings. */
static Result LoadLangTags(FontSpan span,
                                    const FontNameTable *nt,
                                    char ***outTags,
                                    uint16_t *outCount)
{
    FontReader r;
    uint16_t langCount = 0u;
    uint16_t i;
    char **tags;
    size_t afterRecords;

    *outTags = NULL;
    *outCount = 0u;
    if (nt->format != 1u) {
        return RoboOk;
    }
    afterRecords = 6u + (size_t)nt->count * 12u;
    if (span.size < afterRecords + 2u) {
        return RoboOk; /* tolerate missing */
    }
    FontReaderInitSpan(&r, span);
    r.pos = afterRecords;
    if (FontReadU16be(&r, &langCount) != RoboOk) {
        return RoboOk;
    }
    if (langCount > RoboFontNameMaxLangTags) {
        return RoboParseError;
    }
    if (span.size < afterRecords + 2u + (size_t)langCount * 4u) {
        return RoboParseError;
    }
    if (langCount == 0u) {
        return RoboOk;
    }
    tags = (char **)RoboCalloc(langCount, sizeof(char *));
    if (!tags) {
        return RoboOutOfMemory;
    }
    for (i = 0u; i < langCount; i++) {
        uint16_t length = 0u, offset = 0u;
        char *decoded = NULL;
        size_t dlen = 0u;
        FontNameAvail st;
        if (FontReadU16be(&r, &length) != RoboOk ||
            FontReadU16be(&r, &offset) != RoboOk) {
            break;
        }
        if (FontCheckedRange(nt->storage.size, offset, length) != RoboOk) {
            continue;
        }
        /* Language tags are UTF-16BE */
        if (DecodeRecordBytes(RoboFontNamePlatformUnicode, 3u,
                                nt->storage.data + offset, length, &decoded, &dlen,
                                &st) == RoboOk &&
            st == RoboFontNamePresent && decoded) {
            tags[i] = decoded;
        }
    }
    *outTags = tags;
    *outCount = langCount;
    return RoboOk;
}

static void FreeLangTags(char **tags, uint16_t n)
{
    uint16_t i;
    if (!tags) {
        return;
    }
    for (i = 0u; i < n; i++) {
        RoboFree(tags[i]);
    }
    RoboFree(tags);
}

static void MetaFieldClear(FontMetaField *f)
{
    f->utf8 = NULL;
    f->utf8Len = 0u;
    f->avail = RoboFontNameUnavailable;
    f->sourceNameId = 0xffffu;
}

static void MetaFieldSet(FontMetaField *f,
                           const FontNameEntry *e,
                           uint16_t id)
{
    if (!e || e->decodeStatus != RoboFontNamePresent || !e->utf8) {
        MetaFieldClear(f);
        return;
    }
    f->utf8 = e->utf8;
    f->utf8Len = e->utf8Len;
    f->avail = RoboFontNamePresent;
    f->sourceNameId = id;
}

static const FontNameEntry *PickDefaultEntry(const FontNameInfo *info,
                                                        uint16_t nameId)
{
    const FontNameEntry *best = NULL;
    int bestScore = -1;
    uint32_t i;
    for (i = 0u; i < info->entryCount; i++) {
        const FontNameEntry *e = &info->entries[i];
        int sc;
        if (e->nameId != nameId || e->decodeStatus != RoboFontNamePresent) {
            continue;
        }
        sc = PlatformScore(e->platformId, e->encodingId, e->languageId);
        if (!best || sc > bestScore) {
            best = e;
            bestScore = sc;
        }
        /* equal score: keep lower index (first assigned best) */
    }
    return best;
}

static void BuildMetadata(FontNameInfo *info)
{
    const FontNameEntry *e;
    memset(&info->meta, 0, sizeof(info->meta));
    MetaFieldClear(&info->meta.familyName);
    MetaFieldClear(&info->meta.subfamilyName);
    MetaFieldClear(&info->meta.fullName);
    MetaFieldClear(&info->meta.postscriptName);
    MetaFieldClear(&info->meta.version);
    MetaFieldClear(&info->meta.typographicFamilyName);
    MetaFieldClear(&info->meta.typographicSubfamilyName);
    MetaFieldClear(&info->meta.wwsFamilyName);
    MetaFieldClear(&info->meta.wwsSubfamilyName);
    MetaFieldClear(&info->meta.manufacturer);
    MetaFieldClear(&info->meta.designer);
    MetaFieldClear(&info->meta.description);
    MetaFieldClear(&info->meta.copyright);
    MetaFieldClear(&info->meta.trademark);
    MetaFieldClear(&info->meta.licenseDescription);
    MetaFieldClear(&info->meta.vendorUrl);
    MetaFieldClear(&info->meta.designerUrl);
    MetaFieldClear(&info->meta.licenseUrl);

#define SET_ID(field, id)                                                          \
    do {                                                                           \
        e = PickDefaultEntry(info, (id));                                        \
        MetaFieldSet(&info->meta.field, e, (id));                                \
    } while (0)

    SET_ID(copyright, RoboFontNameIdCopyright);
    SET_ID(familyName, RoboFontNameIdFamily);
    SET_ID(subfamilyName, RoboFontNameIdSubfamily);
    SET_ID(fullName, RoboFontNameIdFull);
    SET_ID(version, RoboFontNameIdVersion);
    SET_ID(postscriptName, RoboFontNameIdPostscript);
    SET_ID(trademark, RoboFontNameIdTrademark);
    SET_ID(manufacturer, RoboFontNameIdManufacturer);
    SET_ID(designer, RoboFontNameIdDesigner);
    SET_ID(description, RoboFontNameIdDescription);
    SET_ID(vendorUrl, RoboFontNameIdVendorUrl);
    SET_ID(designerUrl, RoboFontNameIdDesignerUrl);
    SET_ID(licenseDescription, RoboFontNameIdLicense);
    SET_ID(licenseUrl, RoboFontNameIdLicenseUrl);
    SET_ID(typographicFamilyName, RoboFontNameIdTypoFamily);
    SET_ID(typographicSubfamilyName, RoboFontNameIdTypoSubfamily);
    SET_ID(wwsFamilyName, RoboFontNameIdWwsFamily);
    SET_ID(wwsSubfamilyName, RoboFontNameIdWwsSubfamily);
#undef SET_ID

    if (info->meta.version.avail == RoboFontNamePresent) {
        ParseVersionString(info->meta.version.utf8, &info->meta.versionParsed);
    }
    if (info->meta.postscriptName.avail == RoboFontNamePresent) {
        info->meta.postscriptNameStructurallyOk =
            (uint8_t)PsNameOk(info->meta.postscriptName.utf8,
                                info->meta.postscriptName.utf8Len);
        if (!info->meta.postscriptNameStructurallyOk) {
            info->meta.postscriptName.avail = RoboFontNameMalformed;
        }
    }
}

static Result BuildFromNameTable(const FontNameTable *nt,
                                           FontSpan fullSpan,
                                           const I18nLocale *preferredOpt,
                                           FontNameInfo **out)
{
    FontNameInfo *info;
    char **langTags = NULL;
    uint16_t langTagCount = 0u;
    uint16_t i;
    Result res;
    (void)preferredOpt;

    *out = NULL;
    if (!nt) {
        return RoboInvalidArgument;
    }
    info = (FontNameInfo *)RoboCalloc(1u, sizeof(*info));
    if (!info) {
        return RoboOutOfMemory;
    }
    info->refs = 1u;

    res = LoadLangTags(fullSpan, nt, &langTags, &langTagCount);
    if (res != RoboOk) {
        FontNameInfoRelease(info);
        return res;
    }

    if (nt->count > 0u) {
        info->entries =
            (FontNameEntry *)RoboCalloc(nt->count, sizeof(*info->entries));
        info->ownedStrings = (char **)RoboCalloc(nt->count, sizeof(char *));
        if (!info->entries || !info->ownedStrings) {
            FreeLangTags(langTags, langTagCount);
            FontNameInfoRelease(info);
            return RoboOutOfMemory;
        }
    }

    for (i = 0u; i < nt->count; i++) {
        const FontNameRecord *rec = &nt->records[i];
        FontNameEntry *e = &info->entries[info->entryCount];
        char *decoded = NULL;
        size_t dlen = 0u;
        FontNameAvail st = RoboFontNameUnsupported;
        const char *ltag = NULL;

        e->platformId = rec->platformId;
        e->encodingId = rec->encodingId;
        e->languageId = rec->languageId;
        e->nameId = rec->nameId;
        e->utf8 = "";
        e->utf8Len = 0u;

        if (rec->languageId >= 0x8000u) {
            uint16_t idx = (uint16_t)(rec->languageId - 0x8000u);
            if (idx < langTagCount && langTags && langTags[idx]) {
                ltag = langTags[idx];
            }
        }
        FillLocaleForRecord(rec->platformId, rec->languageId, ltag, e->localeTag,
                               sizeof(e->localeTag), &e->hasLocale);

        if (info->totalDecoded >= RoboFontNameMaxTotalDecoded) {
            e->decodeStatus = RoboFontNameUnsupported;
            info->entryCount++;
            continue;
        }

        res = DecodeRecordBytes(rec->platformId, rec->encodingId,
                                  nt->storage.data + rec->offset, rec->length, &decoded,
                                  &dlen, &st);
        if (res == RoboOutOfMemory) {
            FreeLangTags(langTags, langTagCount);
            FontNameInfoRelease(info);
            return res;
        }
        e->decodeStatus = st;
        if (st == RoboFontNamePresent && decoded) {
            if (info->totalDecoded + dlen > RoboFontNameMaxTotalDecoded) {
                RoboFree(decoded);
                e->decodeStatus = RoboFontNameUnsupported;
            } else {
                info->ownedStrings[info->entryCount] = decoded;
                e->utf8 = decoded;
                e->utf8Len = dlen;
                info->totalDecoded += dlen;
            }
        } else {
            RoboFree(decoded);
        }
        info->entryCount++;
    }

    FreeLangTags(langTags, langTagCount);
    BuildMetadata(info);
    *out = info;
    return RoboOk;
}

Result FontNameInfoFromSpan(FontSpan nameSpan,
                                            const I18nLocale *preferredOpt,
                                            FontNameInfo **out)
{
    FontNameTable nt;
    Result res;
    if (!out) {
        return RoboInvalidArgument;
    }
    *out = NULL;
    memset(&nt, 0, sizeof(nt));
    res = FontNameParse(nameSpan, &nt);
    if (res != RoboOk) {
        return res;
    }
    res = BuildFromNameTable(&nt, nameSpan, preferredOpt, out);
    FontNameTableClear(&nt);
    return res;
}

Result FontNameInfoCreate(FontSfnt *sfnt,
                                         FontTt *ttOpt,
                                         const I18nLocale *preferredOpt,
                                         FontNameInfo **out)
{
    FontSpan span;
    const FontNameTable *nt;
    if (!sfnt || !out) {
        return RoboInvalidArgument;
    }
    *out = NULL;
    if (FontSfntTableBytes(sfnt, RoboFontTagName, &span) != RoboOk) {
        return RoboNotFound;
    }
    if (ttOpt) {
        nt = FontTtName(ttOpt);
        if (nt) {
            return BuildFromNameTable(nt, span, preferredOpt, out);
        }
    }
    return FontNameInfoFromSpan(span, preferredOpt, out);
}

void FontNameInfoRetain(FontNameInfo *info)
{
    if (info) {
        info->refs++;
    }
}

void FontNameInfoRelease(FontNameInfo *info)
{
    uint32_t i;
    if (!info) {
        return;
    }
    if (info->refs == 0u) {
        return;
    }
    info->refs--;
    if (info->refs != 0u) {
        return;
    }
    if (info->ownedStrings) {
        for (i = 0u; i < info->entryCount; i++) {
            RoboFree(info->ownedStrings[i]);
        }
        RoboFree(info->ownedStrings);
    }
    for (i = 0u; i < info->metaScratchN; i++) {
        RoboFree(info->metaScratch[i]);
    }
    RoboFree(info->entries);
    RoboFree(info);
}

uint32_t FontNameInfoEntryCount(const FontNameInfo *info)
{
    return info ? info->entryCount : 0u;
}

const FontNameEntry *FontNameInfoEntry(const FontNameInfo *info,
                                                        uint32_t index)
{
    if (!info || index >= info->entryCount) {
        return NULL;
    }
    return &info->entries[index];
}

Result FontNameInfoLookup(const FontNameInfo *info,
                                         uint16_t nameId,
                                         const I18nLocale *preferredOpt,
                                         const FontNameEntry **outEntry,
                                         FontNameLookupKind *outKind)
{
    uint32_t i;
    const FontNameEntry *best = NULL;
    FontNameLookupKind kind = RoboFontNameLookupNone;
    int bestScore = -1;

    if (!info || !outEntry) {
        return RoboInvalidArgument;
    }
    *outEntry = NULL;
    if (outKind) {
        *outKind = RoboFontNameLookupNone;
    }

    /* Pass 1: exact locale */
    if (preferredOpt && preferredOpt->language[0]) {
        for (i = 0u; i < info->entryCount; i++) {
            const FontNameEntry *e = &info->entries[i];
            I18nLocale loc;
            uint8_t has = 0u;
            if (e->nameId != nameId || e->decodeStatus != RoboFontNamePresent ||
                !e->hasLocale) {
                continue;
            }
            LocaleFromTag(e->localeTag, &loc, &has);
            if (has && I18nLocaleEqual(preferredOpt, &loc)) {
                *outEntry = e;
                if (outKind) {
                    *outKind = RoboFontNameLookupExactLocale;
                }
                return RoboOk;
            }
        }
        /* Pass 2: language match */
        for (i = 0u; i < info->entryCount; i++) {
            const FontNameEntry *e = &info->entries[i];
            I18nLocale loc;
            uint8_t has = 0u;
            if (e->nameId != nameId || e->decodeStatus != RoboFontNamePresent ||
                !e->hasLocale) {
                continue;
            }
            LocaleFromTag(e->localeTag, &loc, &has);
            if (has &&
                I18nLocaleMatches(preferredOpt, &loc) >= RoboI18nMatchLanguage) {
                int sc = PlatformScore(e->platformId, e->encodingId, e->languageId);
                if (!best || sc > bestScore) {
                    best = e;
                    bestScore = sc;
                    kind = RoboFontNameLookupLanguage;
                }
            }
        }
        if (best) {
            *outEntry = best;
            if (outKind) {
                *outKind = kind;
            }
            return RoboOk;
        }
    }

    /* Pass 3/4: platform preference / default */
    best = NULL;
    bestScore = -1;
    for (i = 0u; i < info->entryCount; i++) {
        const FontNameEntry *e = &info->entries[i];
        int sc;
        if (e->nameId != nameId || e->decodeStatus != RoboFontNamePresent) {
            continue;
        }
        sc = PlatformScore(e->platformId, e->encodingId, e->languageId);
        if (!best || sc > bestScore) {
            best = e;
            bestScore = sc;
            kind = (sc >= 40) ? RoboFontNameLookupPlatformPref
                              : RoboFontNameLookupDefault;
        }
    }
    if (!best) {
        return RoboNotFound;
    }
    *outEntry = best;
    if (outKind) {
        *outKind = kind;
    }
    return RoboOk;
}

const FontMetadata *FontNameInfoMetadata(const FontNameInfo *info)
{
    return info ? &info->meta : NULL;
}

Result FontMetadataPreferredFamily(const FontMetadata *meta,
                                                  FontMetaField *out)
{
    if (!meta || !out) {
        return RoboInvalidArgument;
    }
    if (meta->typographicFamilyName.avail == RoboFontNamePresent) {
        *out = meta->typographicFamilyName;
        return RoboOk;
    }
    if (meta->wwsFamilyName.avail == RoboFontNamePresent) {
        *out = meta->wwsFamilyName;
        out->avail = RoboFontNameFallback;
        return RoboOk;
    }
    if (meta->familyName.avail == RoboFontNamePresent) {
        *out = meta->familyName;
        out->avail = RoboFontNameFallback;
        return RoboOk;
    }
    MetaFieldClear(out);
    return RoboNotFound;
}

Result FontMetadataPreferredSubfamily(const FontMetadata *meta,
                                                     FontMetaField *out)
{
    if (!meta || !out) {
        return RoboInvalidArgument;
    }
    if (meta->typographicSubfamilyName.avail == RoboFontNamePresent) {
        *out = meta->typographicSubfamilyName;
        return RoboOk;
    }
    if (meta->wwsSubfamilyName.avail == RoboFontNamePresent) {
        *out = meta->wwsSubfamilyName;
        out->avail = RoboFontNameFallback;
        return RoboOk;
    }
    if (meta->subfamilyName.avail == RoboFontNamePresent) {
        *out = meta->subfamilyName;
        out->avail = RoboFontNameFallback;
        return RoboOk;
    }
    MetaFieldClear(out);
    return RoboNotFound;
}

Result FontNameFuzzParse(const uint8_t *data, size_t size)
{
    FontSource *src = NULL;
    FontSfnt *sfnt = NULL;
    FontNameInfo *info = NULL;
    FontSpan span;
    Result res;

    if (!data || size == 0u) {
        return RoboOk;
    }
    /* Try as SFNT first */
    if (FontSourceCreateMemoryCopy(data, size, &src) == RoboOk) {
        if (FontSfntParse(src, 0, 0, &sfnt) == RoboOk) {
            (void)FontNameInfoCreate(sfnt, NULL, NULL, &info);
            if (info) {
                FontNameInfoRelease(info);
            }
            FontSfntRelease(sfnt);
            FontSourceRelease(src);
            return RoboOk;
        }
        FontSourceRelease(src);
    }
    /* Raw name table */
    span.data = data;
    span.size = size;
    res = FontNameInfoFromSpan(span, NULL, &info);
    if (info) {
        FontNameInfoRelease(info);
    }
    (void)res;
    return RoboOk;
}
