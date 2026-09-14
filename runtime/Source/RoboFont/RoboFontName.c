#include "RoboFont/RoboFontName.h"

#include "RoboFont/RoboFontBinary.h"
#include "RoboFont/RoboFontSource.h"
#include "RoboMem/RoboMem.h"
#include "RoboUnicode/RoboUnicode.h"

#include <string.h>

struct robo_font_name_info {
    uint32_t refs;
    uint32_t entry_count;
    robo_font_name_entry_t *entries;
    char **owned_strings; /* parallel to entries; may be NULL slot */
    size_t total_decoded;
    robo_font_metadata_t meta;
    /* metadata string storage (separate from entry strings when shared via pointer) */
    char *meta_scratch[20];
    uint32_t meta_scratch_n;
};

/* MacRoman 0x80..0xFF → Unicode (Apple Mac OS Roman). */
static const uint16_t k_mac_roman_hi[128] = {
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
} win_lcid_map_t;

static const win_lcid_map_t k_win_lcid[] = {
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
} mac_lang_map_t;

static const mac_lang_map_t k_mac_lang[] = {
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

static void locale_from_tag(const char *tag, robo_i18n_locale_t *out, uint8_t *has)
{
    *has = 0u;
    robo_i18n_locale_clear(out);
    if (!tag || !tag[0]) {
        return;
    }
    if (robo_i18n_locale_parse_canonical(tag, strlen(tag), out) == ROBO_OK) {
        *has = 1u;
    }
}

static void map_win_lang(uint16_t lang_id, char *tag_out, size_t tag_cap, uint8_t *has)
{
    size_t i;
    *has = 0u;
    if (!tag_out || tag_cap == 0u) {
        return;
    }
    tag_out[0] = '\0';
    if (lang_id >= 0x8000u) {
        return; /* format-1 lang tag handled separately */
    }
    for (i = 0u; i < sizeof(k_win_lcid) / sizeof(k_win_lcid[0]); i++) {
        if (k_win_lcid[i].lcid == lang_id) {
            size_t n = strlen(k_win_lcid[i].tag);
            if (n + 1u > tag_cap) {
                return;
            }
            memcpy(tag_out, k_win_lcid[i].tag, n + 1u);
            *has = 1u;
            return;
        }
    }
    /* primary language only from LCID low 10 bits when unknown region */
    {
        uint16_t primary = (uint16_t)(lang_id & 0x3ffu);
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
            if (n + 1u <= tag_cap) {
                memcpy(tag_out, p, n + 1u);
                *has = 1u;
            }
        }
    }
}

static void map_mac_lang(uint16_t lang_id, char *tag_out, size_t tag_cap, uint8_t *has)
{
    size_t i;
    *has = 0u;
    if (!tag_out || tag_cap == 0u) {
        return;
    }
    tag_out[0] = '\0';
    for (i = 0u; i < sizeof(k_mac_lang) / sizeof(k_mac_lang[0]); i++) {
        if (k_mac_lang[i].mid == lang_id) {
            size_t n = strlen(k_mac_lang[i].tag);
            if (n + 1u > tag_cap) {
                return;
            }
            memcpy(tag_out, k_mac_lang[i].tag, n + 1u);
            *has = 1u;
            return;
        }
    }
}

static robo_result_t decode_utf16be(const uint8_t *bytes,
                                    size_t len,
                                    char **out_utf8,
                                    size_t *out_len)
{
    robo_uchar16_t *u16;
    size_t nunits;
    size_t ui;
    size_t utf8_len = 0u;
    char *buf;
    robo_unicode_status_t st;

    *out_utf8 = NULL;
    *out_len = 0u;
    if ((len & 1u) != 0u) {
        return ROBO_PARSE_ERROR;
    }
    nunits = len / 2u;
    u16 = (robo_uchar16_t *)robo_calloc(nunits == 0u ? 1u : nunits, sizeof(*u16));
    if (!u16) {
        return ROBO_OUT_OF_MEMORY;
    }
    for (ui = 0u; ui < nunits; ui++) {
        u16[ui] = (robo_uchar16_t)(((uint16_t)bytes[ui * 2u] << 8) |
                                   (uint16_t)bytes[ui * 2u + 1u]);
    }
    st = robo_unicode_utf16_to_utf8(u16, nunits, NULL, 0u, &utf8_len, ROBO_UNICODE_STRICT);
    if (st != ROBO_UNICODE_OK && st != ROBO_UNICODE_NO_SPACE) {
        robo_free(u16);
        return ROBO_PARSE_ERROR;
    }
    if (utf8_len > ROBO_FONT_NAME_MAX_STRING) {
        robo_free(u16);
        return ROBO_PARSE_ERROR;
    }
    buf = (char *)robo_calloc(utf8_len + 1u, 1u);
    if (!buf) {
        robo_free(u16);
        return ROBO_OUT_OF_MEMORY;
    }
    st = robo_unicode_utf16_to_utf8(u16, nunits, buf, utf8_len + 1u, &utf8_len,
                                    ROBO_UNICODE_STRICT);
    robo_free(u16);
    if (st != ROBO_UNICODE_OK) {
        robo_free(buf);
        return ROBO_PARSE_ERROR;
    }
    buf[utf8_len] = '\0';
    *out_utf8 = buf;
    *out_len = utf8_len;
    return ROBO_OK;
}

static robo_result_t decode_mac_roman(const uint8_t *bytes,
                                      size_t len,
                                      char **out_utf8,
                                      size_t *out_len)
{
    /* Worst case: each byte → 3 UTF-8 bytes (BMP) */
    size_t cap = len * 3u + 1u;
    char *buf;
    size_t w = 0u;
    size_t i;
    if (len > ROBO_FONT_NAME_MAX_STRING) {
        return ROBO_PARSE_ERROR;
    }
    buf = (char *)robo_calloc(cap, 1u);
    if (!buf) {
        return ROBO_OUT_OF_MEMORY;
    }
    for (i = 0u; i < len; i++) {
        uint32_t cp = bytes[i];
        size_t n = 0u;
        if (cp >= 0x80u) {
            cp = k_mac_roman_hi[cp - 0x80u];
        }
        if (robo_unicode_utf8_encode((robo_uchar32_t)cp, buf + w, cap - w, &n) !=
            ROBO_UNICODE_OK) {
            robo_free(buf);
            return ROBO_PARSE_ERROR;
        }
        w += n;
    }
    buf[w] = '\0';
    *out_utf8 = buf;
    *out_len = w;
    return ROBO_OK;
}

static robo_result_t decode_record_bytes(uint16_t platform,
                                         uint16_t encoding,
                                         const uint8_t *bytes,
                                         size_t len,
                                         char **out_utf8,
                                         size_t *out_len,
                                         robo_font_name_avail_t *status)
{
    *out_utf8 = NULL;
    *out_len = 0u;
    *status = ROBO_FONT_NAME_UNSUPPORTED;

    if (platform == ROBO_FONT_NAME_PLATFORM_WINDOWS &&
        (encoding == ROBO_FONT_NAME_WIN_ENC_UNICODE_BMP ||
         encoding == ROBO_FONT_NAME_WIN_ENC_UNICODE_FULL ||
         encoding == ROBO_FONT_NAME_WIN_ENC_SYMBOL)) {
        robo_result_t r = decode_utf16be(bytes, len, out_utf8, out_len);
        if (r == ROBO_OK) {
            *status = ROBO_FONT_NAME_PRESENT;
            return ROBO_OK;
        }
        if (r == ROBO_OUT_OF_MEMORY) {
            return r;
        }
        *status = ROBO_FONT_NAME_MALFORMED;
        return ROBO_OK;
    }
    if (platform == ROBO_FONT_NAME_PLATFORM_UNICODE) {
        /* Unicode platform: UTF-16BE for encoding 3/4 (UTF-16) and commonly 0–2 */
        if (encoding <= 4u) {
            robo_result_t r = decode_utf16be(bytes, len, out_utf8, out_len);
            if (r == ROBO_OK) {
                *status = ROBO_FONT_NAME_PRESENT;
                return ROBO_OK;
            }
            if (r == ROBO_OUT_OF_MEMORY) {
                return r;
            }
            *status = ROBO_FONT_NAME_MALFORMED;
            return ROBO_OK;
        }
        return ROBO_OK;
    }
    if (platform == ROBO_FONT_NAME_PLATFORM_MACINTOSH &&
        encoding == ROBO_FONT_NAME_MAC_ENC_ROMAN) {
        robo_result_t r = decode_mac_roman(bytes, len, out_utf8, out_len);
        if (r == ROBO_OK) {
            *status = ROBO_FONT_NAME_PRESENT;
            return ROBO_OK;
        }
        if (r == ROBO_OUT_OF_MEMORY) {
            return r;
        }
        *status = ROBO_FONT_NAME_MALFORMED;
        return ROBO_OK;
    }
    return ROBO_OK;
}

static void fill_locale_for_record(uint16_t platform,
                                   uint16_t language_id,
                                   const char *lang_tag_override,
                                   char *tag_out,
                                   size_t tag_cap,
                                   uint8_t *has)
{
    *has = 0u;
    tag_out[0] = '\0';
    if (lang_tag_override && lang_tag_override[0]) {
        size_t n = strlen(lang_tag_override);
        if (n + 1u <= tag_cap) {
            memcpy(tag_out, lang_tag_override, n + 1u);
            *has = 1u;
        }
        return;
    }
    if (platform == ROBO_FONT_NAME_PLATFORM_WINDOWS) {
        map_win_lang(language_id, tag_out, tag_cap, has);
    } else if (platform == ROBO_FONT_NAME_PLATFORM_MACINTOSH) {
        map_mac_lang(language_id, tag_out, tag_cap, has);
    }
}

static int platform_score(uint16_t platform, uint16_t encoding, uint16_t language)
{
    int s = 0;
    if (platform == ROBO_FONT_NAME_PLATFORM_WINDOWS &&
        (encoding == ROBO_FONT_NAME_WIN_ENC_UNICODE_BMP ||
         encoding == ROBO_FONT_NAME_WIN_ENC_UNICODE_FULL)) {
        s = 40;
        if (language == 0x0409u) {
            s += 10;
        }
    } else if (platform == ROBO_FONT_NAME_PLATFORM_UNICODE) {
        s = 30;
    } else if (platform == ROBO_FONT_NAME_PLATFORM_MACINTOSH &&
               encoding == ROBO_FONT_NAME_MAC_ENC_ROMAN) {
        s = 10;
    } else if (platform == ROBO_FONT_NAME_PLATFORM_WINDOWS) {
        s = 5;
    }
    return s;
}

static int ps_name_ok(const char *s, size_t n)
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

static void parse_version_string(const char *s, robo_font_version_parsed_t *out)
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
static robo_result_t load_lang_tags(robo_font_span_t span,
                                    const robo_font_name_table_t *nt,
                                    char ***out_tags,
                                    uint16_t *out_count)
{
    robo_font_reader_t r;
    uint16_t lang_count = 0u;
    uint16_t i;
    char **tags;
    size_t after_records;

    *out_tags = NULL;
    *out_count = 0u;
    if (nt->format != 1u) {
        return ROBO_OK;
    }
    after_records = 6u + (size_t)nt->count * 12u;
    if (span.size < after_records + 2u) {
        return ROBO_OK; /* tolerate missing */
    }
    robo_font_reader_init_span(&r, span);
    r.pos = after_records;
    if (robo_font_read_u16be(&r, &lang_count) != ROBO_OK) {
        return ROBO_OK;
    }
    if (lang_count > ROBO_FONT_NAME_MAX_LANG_TAGS) {
        return ROBO_PARSE_ERROR;
    }
    if (span.size < after_records + 2u + (size_t)lang_count * 4u) {
        return ROBO_PARSE_ERROR;
    }
    if (lang_count == 0u) {
        return ROBO_OK;
    }
    tags = (char **)robo_calloc(lang_count, sizeof(char *));
    if (!tags) {
        return ROBO_OUT_OF_MEMORY;
    }
    for (i = 0u; i < lang_count; i++) {
        uint16_t length = 0u, offset = 0u;
        char *decoded = NULL;
        size_t dlen = 0u;
        robo_font_name_avail_t st;
        if (robo_font_read_u16be(&r, &length) != ROBO_OK ||
            robo_font_read_u16be(&r, &offset) != ROBO_OK) {
            break;
        }
        if (robo_font_checked_range(nt->storage.size, offset, length) != ROBO_OK) {
            continue;
        }
        /* Language tags are UTF-16BE */
        if (decode_record_bytes(ROBO_FONT_NAME_PLATFORM_UNICODE, 3u,
                                nt->storage.data + offset, length, &decoded, &dlen,
                                &st) == ROBO_OK &&
            st == ROBO_FONT_NAME_PRESENT && decoded) {
            tags[i] = decoded;
        }
    }
    *out_tags = tags;
    *out_count = lang_count;
    return ROBO_OK;
}

static void free_lang_tags(char **tags, uint16_t n)
{
    uint16_t i;
    if (!tags) {
        return;
    }
    for (i = 0u; i < n; i++) {
        robo_free(tags[i]);
    }
    robo_free(tags);
}

static void meta_field_clear(robo_font_meta_field_t *f)
{
    f->utf8 = NULL;
    f->utf8_len = 0u;
    f->avail = ROBO_FONT_NAME_UNAVAILABLE;
    f->source_name_id = 0xffffu;
}

static void meta_field_set(robo_font_meta_field_t *f,
                           const robo_font_name_entry_t *e,
                           uint16_t id)
{
    if (!e || e->decode_status != ROBO_FONT_NAME_PRESENT || !e->utf8) {
        meta_field_clear(f);
        return;
    }
    f->utf8 = e->utf8;
    f->utf8_len = e->utf8_len;
    f->avail = ROBO_FONT_NAME_PRESENT;
    f->source_name_id = id;
}

static const robo_font_name_entry_t *pick_default_entry(const robo_font_name_info_t *info,
                                                        uint16_t name_id)
{
    const robo_font_name_entry_t *best = NULL;
    int best_score = -1;
    uint32_t i;
    for (i = 0u; i < info->entry_count; i++) {
        const robo_font_name_entry_t *e = &info->entries[i];
        int sc;
        if (e->name_id != name_id || e->decode_status != ROBO_FONT_NAME_PRESENT) {
            continue;
        }
        sc = platform_score(e->platform_id, e->encoding_id, e->language_id);
        if (!best || sc > best_score) {
            best = e;
            best_score = sc;
        }
        /* equal score: keep lower index (first assigned best) */
    }
    return best;
}

static void build_metadata(robo_font_name_info_t *info)
{
    const robo_font_name_entry_t *e;
    memset(&info->meta, 0, sizeof(info->meta));
    meta_field_clear(&info->meta.family_name);
    meta_field_clear(&info->meta.subfamily_name);
    meta_field_clear(&info->meta.full_name);
    meta_field_clear(&info->meta.postscript_name);
    meta_field_clear(&info->meta.version);
    meta_field_clear(&info->meta.typographic_family_name);
    meta_field_clear(&info->meta.typographic_subfamily_name);
    meta_field_clear(&info->meta.wws_family_name);
    meta_field_clear(&info->meta.wws_subfamily_name);
    meta_field_clear(&info->meta.manufacturer);
    meta_field_clear(&info->meta.designer);
    meta_field_clear(&info->meta.description);
    meta_field_clear(&info->meta.copyright);
    meta_field_clear(&info->meta.trademark);
    meta_field_clear(&info->meta.license_description);
    meta_field_clear(&info->meta.vendor_url);
    meta_field_clear(&info->meta.designer_url);
    meta_field_clear(&info->meta.license_url);

#define SET_ID(field, id)                                                          \
    do {                                                                           \
        e = pick_default_entry(info, (id));                                        \
        meta_field_set(&info->meta.field, e, (id));                                \
    } while (0)

    SET_ID(copyright, ROBO_FONT_NAME_ID_COPYRIGHT);
    SET_ID(family_name, ROBO_FONT_NAME_ID_FAMILY);
    SET_ID(subfamily_name, ROBO_FONT_NAME_ID_SUBFAMILY);
    SET_ID(full_name, ROBO_FONT_NAME_ID_FULL);
    SET_ID(version, ROBO_FONT_NAME_ID_VERSION);
    SET_ID(postscript_name, ROBO_FONT_NAME_ID_POSTSCRIPT);
    SET_ID(trademark, ROBO_FONT_NAME_ID_TRADEMARK);
    SET_ID(manufacturer, ROBO_FONT_NAME_ID_MANUFACTURER);
    SET_ID(designer, ROBO_FONT_NAME_ID_DESIGNER);
    SET_ID(description, ROBO_FONT_NAME_ID_DESCRIPTION);
    SET_ID(vendor_url, ROBO_FONT_NAME_ID_VENDOR_URL);
    SET_ID(designer_url, ROBO_FONT_NAME_ID_DESIGNER_URL);
    SET_ID(license_description, ROBO_FONT_NAME_ID_LICENSE);
    SET_ID(license_url, ROBO_FONT_NAME_ID_LICENSE_URL);
    SET_ID(typographic_family_name, ROBO_FONT_NAME_ID_TYPO_FAMILY);
    SET_ID(typographic_subfamily_name, ROBO_FONT_NAME_ID_TYPO_SUBFAMILY);
    SET_ID(wws_family_name, ROBO_FONT_NAME_ID_WWS_FAMILY);
    SET_ID(wws_subfamily_name, ROBO_FONT_NAME_ID_WWS_SUBFAMILY);
#undef SET_ID

    if (info->meta.version.avail == ROBO_FONT_NAME_PRESENT) {
        parse_version_string(info->meta.version.utf8, &info->meta.version_parsed);
    }
    if (info->meta.postscript_name.avail == ROBO_FONT_NAME_PRESENT) {
        info->meta.postscript_name_structurally_ok =
            (uint8_t)ps_name_ok(info->meta.postscript_name.utf8,
                                info->meta.postscript_name.utf8_len);
        if (!info->meta.postscript_name_structurally_ok) {
            info->meta.postscript_name.avail = ROBO_FONT_NAME_MALFORMED;
        }
    }
}

static robo_result_t build_from_name_table(const robo_font_name_table_t *nt,
                                           robo_font_span_t full_span,
                                           const robo_i18n_locale_t *preferred_opt,
                                           robo_font_name_info_t **out)
{
    robo_font_name_info_t *info;
    char **lang_tags = NULL;
    uint16_t lang_tag_count = 0u;
    uint16_t i;
    robo_result_t res;
    (void)preferred_opt;

    *out = NULL;
    if (!nt) {
        return ROBO_INVALID_ARGUMENT;
    }
    info = (robo_font_name_info_t *)robo_calloc(1u, sizeof(*info));
    if (!info) {
        return ROBO_OUT_OF_MEMORY;
    }
    info->refs = 1u;

    res = load_lang_tags(full_span, nt, &lang_tags, &lang_tag_count);
    if (res != ROBO_OK) {
        robo_font_name_info_release(info);
        return res;
    }

    if (nt->count > 0u) {
        info->entries =
            (robo_font_name_entry_t *)robo_calloc(nt->count, sizeof(*info->entries));
        info->owned_strings = (char **)robo_calloc(nt->count, sizeof(char *));
        if (!info->entries || !info->owned_strings) {
            free_lang_tags(lang_tags, lang_tag_count);
            robo_font_name_info_release(info);
            return ROBO_OUT_OF_MEMORY;
        }
    }

    for (i = 0u; i < nt->count; i++) {
        const robo_font_name_record_t *rec = &nt->records[i];
        robo_font_name_entry_t *e = &info->entries[info->entry_count];
        char *decoded = NULL;
        size_t dlen = 0u;
        robo_font_name_avail_t st = ROBO_FONT_NAME_UNSUPPORTED;
        const char *ltag = NULL;

        e->platform_id = rec->platform_id;
        e->encoding_id = rec->encoding_id;
        e->language_id = rec->language_id;
        e->name_id = rec->name_id;
        e->utf8 = "";
        e->utf8_len = 0u;

        if (rec->language_id >= 0x8000u) {
            uint16_t idx = (uint16_t)(rec->language_id - 0x8000u);
            if (idx < lang_tag_count && lang_tags && lang_tags[idx]) {
                ltag = lang_tags[idx];
            }
        }
        fill_locale_for_record(rec->platform_id, rec->language_id, ltag, e->locale_tag,
                               sizeof(e->locale_tag), &e->has_locale);

        if (info->total_decoded >= ROBO_FONT_NAME_MAX_TOTAL_DECODED) {
            e->decode_status = ROBO_FONT_NAME_UNSUPPORTED;
            info->entry_count++;
            continue;
        }

        res = decode_record_bytes(rec->platform_id, rec->encoding_id,
                                  nt->storage.data + rec->offset, rec->length, &decoded,
                                  &dlen, &st);
        if (res == ROBO_OUT_OF_MEMORY) {
            free_lang_tags(lang_tags, lang_tag_count);
            robo_font_name_info_release(info);
            return res;
        }
        e->decode_status = st;
        if (st == ROBO_FONT_NAME_PRESENT && decoded) {
            if (info->total_decoded + dlen > ROBO_FONT_NAME_MAX_TOTAL_DECODED) {
                robo_free(decoded);
                e->decode_status = ROBO_FONT_NAME_UNSUPPORTED;
            } else {
                info->owned_strings[info->entry_count] = decoded;
                e->utf8 = decoded;
                e->utf8_len = dlen;
                info->total_decoded += dlen;
            }
        } else {
            robo_free(decoded);
        }
        info->entry_count++;
    }

    free_lang_tags(lang_tags, lang_tag_count);
    build_metadata(info);
    *out = info;
    return ROBO_OK;
}

robo_result_t robo_font_name_info_from_span(robo_font_span_t name_span,
                                            const robo_i18n_locale_t *preferred_opt,
                                            robo_font_name_info_t **out)
{
    robo_font_name_table_t nt;
    robo_result_t res;
    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out = NULL;
    memset(&nt, 0, sizeof(nt));
    res = robo_font_name_parse(name_span, &nt);
    if (res != ROBO_OK) {
        return res;
    }
    res = build_from_name_table(&nt, name_span, preferred_opt, out);
    robo_font_name_table_clear(&nt);
    return res;
}

robo_result_t robo_font_name_info_create(robo_font_sfnt_t *sfnt,
                                         robo_font_tt_t *tt_opt,
                                         const robo_i18n_locale_t *preferred_opt,
                                         robo_font_name_info_t **out)
{
    robo_font_span_t span;
    const robo_font_name_table_t *nt;
    if (!sfnt || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out = NULL;
    if (robo_font_sfnt_table_bytes(sfnt, ROBO_FONT_TAG_NAME, &span) != ROBO_OK) {
        return ROBO_NOT_FOUND;
    }
    if (tt_opt) {
        nt = robo_font_tt_name(tt_opt);
        if (nt) {
            return build_from_name_table(nt, span, preferred_opt, out);
        }
    }
    return robo_font_name_info_from_span(span, preferred_opt, out);
}

void robo_font_name_info_retain(robo_font_name_info_t *info)
{
    if (info) {
        info->refs++;
    }
}

void robo_font_name_info_release(robo_font_name_info_t *info)
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
    if (info->owned_strings) {
        for (i = 0u; i < info->entry_count; i++) {
            robo_free(info->owned_strings[i]);
        }
        robo_free(info->owned_strings);
    }
    for (i = 0u; i < info->meta_scratch_n; i++) {
        robo_free(info->meta_scratch[i]);
    }
    robo_free(info->entries);
    robo_free(info);
}

uint32_t robo_font_name_info_entry_count(const robo_font_name_info_t *info)
{
    return info ? info->entry_count : 0u;
}

const robo_font_name_entry_t *robo_font_name_info_entry(const robo_font_name_info_t *info,
                                                        uint32_t index)
{
    if (!info || index >= info->entry_count) {
        return NULL;
    }
    return &info->entries[index];
}

robo_result_t robo_font_name_info_lookup(const robo_font_name_info_t *info,
                                         uint16_t name_id,
                                         const robo_i18n_locale_t *preferred_opt,
                                         const robo_font_name_entry_t **out_entry,
                                         robo_font_name_lookup_kind_t *out_kind)
{
    uint32_t i;
    const robo_font_name_entry_t *best = NULL;
    robo_font_name_lookup_kind_t kind = ROBO_FONT_NAME_LOOKUP_NONE;
    int best_score = -1;

    if (!info || !out_entry) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out_entry = NULL;
    if (out_kind) {
        *out_kind = ROBO_FONT_NAME_LOOKUP_NONE;
    }

    /* Pass 1: exact locale */
    if (preferred_opt && preferred_opt->language[0]) {
        for (i = 0u; i < info->entry_count; i++) {
            const robo_font_name_entry_t *e = &info->entries[i];
            robo_i18n_locale_t loc;
            uint8_t has = 0u;
            if (e->name_id != name_id || e->decode_status != ROBO_FONT_NAME_PRESENT ||
                !e->has_locale) {
                continue;
            }
            locale_from_tag(e->locale_tag, &loc, &has);
            if (has && robo_i18n_locale_equal(preferred_opt, &loc)) {
                *out_entry = e;
                if (out_kind) {
                    *out_kind = ROBO_FONT_NAME_LOOKUP_EXACT_LOCALE;
                }
                return ROBO_OK;
            }
        }
        /* Pass 2: language match */
        for (i = 0u; i < info->entry_count; i++) {
            const robo_font_name_entry_t *e = &info->entries[i];
            robo_i18n_locale_t loc;
            uint8_t has = 0u;
            if (e->name_id != name_id || e->decode_status != ROBO_FONT_NAME_PRESENT ||
                !e->has_locale) {
                continue;
            }
            locale_from_tag(e->locale_tag, &loc, &has);
            if (has &&
                robo_i18n_locale_matches(preferred_opt, &loc) >= ROBO_I18N_MATCH_LANGUAGE) {
                int sc = platform_score(e->platform_id, e->encoding_id, e->language_id);
                if (!best || sc > best_score) {
                    best = e;
                    best_score = sc;
                    kind = ROBO_FONT_NAME_LOOKUP_LANGUAGE;
                }
            }
        }
        if (best) {
            *out_entry = best;
            if (out_kind) {
                *out_kind = kind;
            }
            return ROBO_OK;
        }
    }

    /* Pass 3/4: platform preference / default */
    best = NULL;
    best_score = -1;
    for (i = 0u; i < info->entry_count; i++) {
        const robo_font_name_entry_t *e = &info->entries[i];
        int sc;
        if (e->name_id != name_id || e->decode_status != ROBO_FONT_NAME_PRESENT) {
            continue;
        }
        sc = platform_score(e->platform_id, e->encoding_id, e->language_id);
        if (!best || sc > best_score) {
            best = e;
            best_score = sc;
            kind = (sc >= 40) ? ROBO_FONT_NAME_LOOKUP_PLATFORM_PREF
                              : ROBO_FONT_NAME_LOOKUP_DEFAULT;
        }
    }
    if (!best) {
        return ROBO_NOT_FOUND;
    }
    *out_entry = best;
    if (out_kind) {
        *out_kind = kind;
    }
    return ROBO_OK;
}

const robo_font_metadata_t *robo_font_name_info_metadata(const robo_font_name_info_t *info)
{
    return info ? &info->meta : NULL;
}

robo_result_t robo_font_metadata_preferred_family(const robo_font_metadata_t *meta,
                                                  robo_font_meta_field_t *out)
{
    if (!meta || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (meta->typographic_family_name.avail == ROBO_FONT_NAME_PRESENT) {
        *out = meta->typographic_family_name;
        return ROBO_OK;
    }
    if (meta->wws_family_name.avail == ROBO_FONT_NAME_PRESENT) {
        *out = meta->wws_family_name;
        out->avail = ROBO_FONT_NAME_FALLBACK;
        return ROBO_OK;
    }
    if (meta->family_name.avail == ROBO_FONT_NAME_PRESENT) {
        *out = meta->family_name;
        out->avail = ROBO_FONT_NAME_FALLBACK;
        return ROBO_OK;
    }
    meta_field_clear(out);
    return ROBO_NOT_FOUND;
}

robo_result_t robo_font_metadata_preferred_subfamily(const robo_font_metadata_t *meta,
                                                     robo_font_meta_field_t *out)
{
    if (!meta || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (meta->typographic_subfamily_name.avail == ROBO_FONT_NAME_PRESENT) {
        *out = meta->typographic_subfamily_name;
        return ROBO_OK;
    }
    if (meta->wws_subfamily_name.avail == ROBO_FONT_NAME_PRESENT) {
        *out = meta->wws_subfamily_name;
        out->avail = ROBO_FONT_NAME_FALLBACK;
        return ROBO_OK;
    }
    if (meta->subfamily_name.avail == ROBO_FONT_NAME_PRESENT) {
        *out = meta->subfamily_name;
        out->avail = ROBO_FONT_NAME_FALLBACK;
        return ROBO_OK;
    }
    meta_field_clear(out);
    return ROBO_NOT_FOUND;
}

robo_result_t robo_font_name_fuzz_parse(const uint8_t *data, size_t size)
{
    robo_font_source_t *src = NULL;
    robo_font_sfnt_t *sfnt = NULL;
    robo_font_name_info_t *info = NULL;
    robo_font_span_t span;
    robo_result_t res;

    if (!data || size == 0u) {
        return ROBO_OK;
    }
    /* Try as SFNT first */
    if (robo_font_source_create_memory_copy(data, size, &src) == ROBO_OK) {
        if (robo_font_sfnt_parse(src, 0, 0, &sfnt) == ROBO_OK) {
            (void)robo_font_name_info_create(sfnt, NULL, NULL, &info);
            if (info) {
                robo_font_name_info_release(info);
            }
            robo_font_sfnt_release(sfnt);
            robo_font_source_release(src);
            return ROBO_OK;
        }
        robo_font_source_release(src);
    }
    /* Raw name table */
    span.data = data;
    span.size = size;
    res = robo_font_name_info_from_span(span, NULL, &info);
    if (info) {
        robo_font_name_info_release(info);
    }
    (void)res;
    return ROBO_OK;
}
