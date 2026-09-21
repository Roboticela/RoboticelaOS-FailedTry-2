/*
 * RoboFont SFNT container suite (Goal 5.2).
 *
 * Synthetic fixtures only — no Chromium / system fonts.
 */

#include "RoboFont/RoboFontBinary.h"
#include "RoboFont/RoboFontSfnt.h"
#include "RoboFont/RoboFontSource.h"
#include "RoboTest.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static void PutU16be(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

static void PutU32be(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static void WriteSearchFields(uint8_t *hdr, uint16_t numTables)
{
    uint16_t pow2 = 1u;
    uint16_t entry = 0u;
    uint16_t search;
    uint16_t shift;
    while ((uint16_t)(pow2 * 2u) <= numTables) {
        pow2 = (uint16_t)(pow2 * 2u);
        entry++;
    }
    search = (uint16_t)(pow2 * 16u);
    shift = (uint16_t)(numTables * 16u - search);
    PutU16be(hdr + 4, numTables);
    PutU16be(hdr + 6, search);
    PutU16be(hdr + 8, entry);
    PutU16be(hdr + 10, shift);
}

typedef struct {
    uint32_t tag;
    const uint8_t *data;
    uint32_t length;
} SynthTable;

/*
 * Build a minimal SFNT: header + directory + table payloads (4-byte padded gaps).
 * Returns total size written into @p out (must be large enough).
 */
/*
 * @p offsetBase is added to every table offset (0 for single fonts; face
 * absolute base for TTC members, where offsets are file-absolute).
 */
static size_t BuildSfnt(uint8_t *out,
                         size_t outCap,
                         uint32_t version,
                         const SynthTable *tables,
                         uint32_t n,
                         uint32_t offsetBase)
{
    size_t dirEnd;
    size_t cursor;
    size_t need;
    uint32_t i;

    dirEnd = 12u + (size_t)n * 16u;
    need = dirEnd;
    for (i = 0u; i < n; i++) {
        size_t padded = ((size_t)tables[i].length + 3u) & ~(size_t)3u;
        if (tables[i].length != 0u) {
            need += padded;
        }
    }
    if (need > outCap) {
        return 0u;
    }
    memset(out, 0, need);
    PutU32be(out, version);
    WriteSearchFields(out, (uint16_t)n);

    cursor = dirEnd;
    for (i = 0u; i < n; i++) {
        uint32_t offset;
        uint32_t checksum;
        size_t padded;
        uint8_t *rec = out + 12u + (size_t)i * 16u;

        offset = (uint32_t)cursor + offsetBase;
        if (tables[i].length > 0u && tables[i].data) {
            memcpy(out + cursor, tables[i].data, tables[i].length);
        }
        checksum = FontSfntChecksum(
            tables[i].length ? (out + cursor) : NULL, tables[i].length);
        PutU32be(rec, tables[i].tag);
        PutU32be(rec + 4, checksum);
        PutU32be(rec + 8, offset);
        PutU32be(rec + 12, tables[i].length);
        padded = ((size_t)tables[i].length + 3u) & ~(size_t)3u;
        if (tables[i].length != 0u) {
            cursor += padded;
        }
    }
    return cursor;
}

static void TestBinaryReader(void)
{
    static const uint8_t bytes[] = {0x01, 0x02, 0x03, 0x04, 0x80, 0x00, 0xFF, 0xFE,
                                    0xFD, 0xFC};
    FontReader r;
    uint8_t u8 = 0;
    uint16_t u16 = 0;
    int16_t i16 = 0;
    uint32_t u32 = 0;
    int32_t i32 = 0;
    FontSpan slice;

    FontReaderInit(&r, bytes, sizeof(bytes));
    RoboCheckEqInt(FontReadU8(&r, &u8), RoboOk);
    RoboCheckEqInt(u8, 0x01);
    RoboCheckEqInt(FontReadU16be(&r, &u16), RoboOk);
    RoboCheckEqInt(u16, 0x0203);
    RoboCheckEqInt(FontReadU32be(&r, &u32), RoboOk);
    RoboCheckEqInt((int)u32, (int)0x048000FFu);
    RoboCheckEqInt(FontReadI16be(&r, &i16), RoboOk);
    RoboCheckEqInt(i16, (int16_t)0xFEFD);
    RoboCheckEqInt(FontReadI32be(&r, &i32), RoboParseError); /* truncated */

    RoboCheckEqInt(FontCheckedRange(10, 8, 3), RoboParseError);
    RoboCheckEqInt(FontCheckedRange(10, 8, 2), RoboOk);
    RoboCheckEqInt(FontCheckedRange(SIZE_MAX, SIZE_MAX - 1u, 3u),
                      RoboParseError);

    {
        FontSpan parent = {bytes, sizeof(bytes)};
        RoboCheckEqInt(FontSpanSlice(parent, 2, 4, &slice), RoboOk);
        RoboCheck(slice.data == bytes + 2);
        RoboCheckEqInt((int)slice.size, 4);
        RoboCheckEqInt(FontSpanSlice(parent, 8, 8, &slice), RoboParseError);
    }
}

static void TestSfntMinimalTrue(void)
{
    uint8_t buf[128];
    static const uint8_t payload[] = {0xAA, 0xBB, 0xCC, 0xDD};
    SynthTable tables[1];
    size_t n;
    FontSource *src = NULL;
    FontSfnt *sfnt = NULL;
    const FontSfntHeader *hdr;
    FontSpan span;
    FontSfntTableRecord rec;

    tables[0].tag = RoboFontTagCmap;
    tables[0].data = payload;
    tables[0].length = 4u;
    n = BuildSfnt(buf, sizeof(buf), RoboFontSfntVersionTrue, tables, 1u, 0u);
    RoboCheck(n >= 28u);

    RoboCheckEqInt(FontSourceCreateMemoryCopy(buf, n, &src), RoboOk);
    RoboCheckEqInt(FontSfntParse(src, 0, RoboFontSfntFlagsNone, &sfnt),
                      RoboOk);
    hdr = GetFontSfntHeader(sfnt);
    RoboCheck(hdr != NULL);
    RoboCheckEqInt((int)hdr->sfntVersion, (int)RoboFontSfntVersionTrue);
    RoboCheckEqInt(hdr->numTables, 1);
    RoboCheck(FontSfntSearchFieldsConsistent(hdr));
    RoboCheckEqInt((int)FontSfntTableCount(sfnt), 1);
    RoboCheck(FontSfntHasTable(sfnt, RoboFontTagCmap));
    RoboCheck(!FontSfntHasTable(sfnt, RoboFontTagHead));
    RoboCheckEqInt(FontSfntFindTable(sfnt, RoboFontTagCmap, &rec), RoboOk);
    RoboCheckEqInt((int)rec.length, 4);
    RoboCheckEqInt(FontSfntTableBytes(sfnt, RoboFontTagCmap, &span), RoboOk);
    RoboCheckEqInt((int)span.size, 4);
    RoboCheck(memcmp(span.data, payload, 4) == 0);
    RoboCheckEqInt(FontSfntVerifyTableChecksum(sfnt, RoboFontTagCmap),
                      RoboOk);
    RoboCheckEqInt(FontSfntFuzzParse(buf, n), RoboOk);

    FontSfntRelease(sfnt);
    FontSourceRelease(src);
}

static void TestSfntSignatures(void)
{
    uint8_t buf[128];
    static const uint8_t payload[] = {1, 2, 3, 4};
    SynthTable tables[1];
    size_t n;
    FontSource *src = NULL;
    FontSfnt *sfnt = NULL;
    uint32_t versions[3] = {RoboFontSfntVersionTrueTag, RoboFontSfntVersionTyp1,
                            RoboFontSfntVersionOtto};
    int i;

    tables[0].tag = RoboFontTagCff;
    tables[0].data = payload;
    tables[0].length = 4u;

    for (i = 0; i < 3; i++) {
        n = BuildSfnt(buf, sizeof(buf), versions[i], tables, 1u, 0u);
        RoboCheckEqInt(FontSourceCreateMemoryBorrow(buf, n, &src), RoboOk);
        RoboCheckEqInt(FontSfntParse(src, 0, 0, &sfnt), RoboOk);
        RoboCheckEqInt((int)GetFontSfntHeader(sfnt)->sfntVersion, (int)versions[i]);
        FontSfntRelease(sfnt);
        FontSourceRelease(src);
        src = NULL;
        sfnt = NULL;
    }
}

static void TestSfntMultiTableEnum(void)
{
    uint8_t buf[256];
    static const uint8_t a[] = {0x11, 0x22, 0x33, 0x44};
    static const uint8_t b[] = {0x55, 0x66, 0x77};
    static const uint8_t c[] = {0x01};
    SynthTable tables[3];
    size_t n;
    FontSource *src = NULL;
    FontSfnt *sfnt = NULL;
    FontSfntTableRecord rec;
    uint32_t i;
    char tagbuf[5];

    tables[0].tag = RoboFontTagName;
    tables[0].data = a;
    tables[0].length = 4u;
    tables[1].tag = RoboFontTagMaxp;
    tables[1].data = b;
    tables[1].length = 3u;
    tables[2].tag = RoboFontTagOs2;
    tables[2].data = c;
    tables[2].length = 1u;

    n = BuildSfnt(buf, sizeof(buf), RoboFontSfntVersionTrue, tables, 3u, 0u);
    RoboCheckEqInt(FontSourceCreateMemoryCopy(buf, n, &src), RoboOk);
    RoboCheckEqInt(
        FontSfntParse(src, 0, RoboFontSfntFlagsVerifyChecksums, &sfnt),
        RoboOk);
    RoboCheckEqInt((int)FontSfntTableCount(sfnt), 3);

    /* Enumeration is sorted by tag. */
    for (i = 0u; i < 3u; i++) {
        RoboCheckEqInt(FontSfntTableAt(sfnt, i, &rec), RoboOk);
        if (i > 0u) {
            FontSfntTableRecord prev;
            RoboCheckEqInt(FontSfntTableAt(sfnt, i - 1u, &prev), RoboOk);
            RoboCheck(prev.tag < rec.tag);
        }
        RoboCheckEqInt(FontTagFormat(rec.tag, tagbuf, sizeof(tagbuf)), RoboOk);
        RoboCheckEqInt((int)strlen(tagbuf), 4);
    }
    RoboCheckEqInt(FontSfntTableAt(sfnt, 3, &rec), RoboNotFound);
    RoboCheck(FontSfntHasTable(sfnt, RoboFontTagOs2));
    RoboCheckEqInt(FontSfntChecksum(b, 3),
                      ((uint32_t)0x55 << 24) | ((uint32_t)0x66 << 16) |
                          ((uint32_t)0x77 << 8));

    FontSfntRelease(sfnt);
    FontSourceRelease(src);
}

static void TestSfntZeroLengthTable(void)
{
    uint8_t buf[128];
    static const uint8_t payload[] = {9, 8, 7, 6};
    SynthTable tables[2];
    size_t n;
    FontSource *src = NULL;
    FontSfnt *sfnt = NULL;
    FontSpan span;

    tables[0].tag = RoboFontTagLoca;
    tables[0].data = NULL;
    tables[0].length = 0u;
    tables[1].tag = RoboFontTagGlyf;
    tables[1].data = payload;
    tables[1].length = 4u;

    n = BuildSfnt(buf, sizeof(buf), RoboFontSfntVersionTrue, tables, 2u, 0u);
    RoboCheckEqInt(FontSourceCreateMemoryBorrow(buf, n, &src), RoboOk);
    RoboCheckEqInt(FontSfntParse(src, 0, 0, &sfnt), RoboOk);
    RoboCheckEqInt(FontSfntTableBytes(sfnt, RoboFontTagLoca, &span), RoboOk);
    RoboCheckEqInt((int)span.size, 0);
    RoboCheckEqInt(FontSfntTableBytes(sfnt, RoboFontTagGlyf, &span), RoboOk);
    RoboCheck(memcmp(span.data, payload, 4) == 0);

    FontSfntRelease(sfnt);
    FontSourceRelease(src);
}

static void TestSfntMalformed(void)
{
    FontSource *src = NULL;
    FontSfnt *sfnt = NULL;
    uint8_t trunc[8] = {0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x10};
    uint8_t badSig[12];
    uint8_t buf[128];
    static const uint8_t payload[] = {1, 2, 3, 4};
    SynthTable tables[1];
    size_t n;

    /* Truncated header */
    RoboCheckEqInt(FontSourceCreateMemoryBorrow(trunc, sizeof(trunc), &src),
                      RoboOk);
    RoboCheckEqInt(FontSfntParse(src, 0, 0, &sfnt), RoboParseError);
    RoboCheck(sfnt == NULL);
    FontSourceRelease(src);

    /* Invalid signature */
    memset(badSig, 0, sizeof(badSig));
    PutU32be(badSig, 0x12345678u);
    PutU16be(badSig + 4, 1);
    RoboCheckEqInt(FontSourceCreateMemoryBorrow(badSig, sizeof(badSig), &src),
                      RoboOk);
    RoboCheckEqInt(FontSfntParse(src, 0, 0, &sfnt), RoboNotSupported);
    FontSourceRelease(src);

    /* Truncated directory (claims 2 tables, only room for header) */
    {
        uint8_t shortDir[12];
        memset(shortDir, 0, sizeof(shortDir));
        PutU32be(shortDir, RoboFontSfntVersionTrue);
        PutU16be(shortDir + 4, 2);
        PutU16be(shortDir + 6, 32);
        PutU16be(shortDir + 8, 1);
        PutU16be(shortDir + 10, 0);
        RoboCheckEqInt(
            FontSourceCreateMemoryBorrow(shortDir, sizeof(shortDir), &src),
            RoboOk);
        RoboCheckEqInt(FontSfntParse(src, 0, 0, &sfnt), RoboParseError);
        FontSourceRelease(src);
    }

    /* Excessive table count (directory would overflow source) */
    {
        uint8_t huge[12];
        memset(huge, 0, sizeof(huge));
        PutU32be(huge, RoboFontSfntVersionTrue);
        PutU16be(huge + 4, 4095);
        RoboCheckEqInt(FontSourceCreateMemoryBorrow(huge, sizeof(huge), &src),
                          RoboOk);
        RoboCheckEqInt(FontSfntParse(src, 0, 0, &sfnt), RoboParseError);
        FontSourceRelease(src);
    }

    /* Table outside source */
    tables[0].tag = RoboFontTagHead;
    tables[0].data = payload;
    tables[0].length = 4u;
    n = BuildSfnt(buf, sizeof(buf), RoboFontSfntVersionTrue, tables, 1u, 0u);
    /* Corrupt length to extend past end */
    PutU32be(buf + 12 + 12, 0xFFFFFFFFu);
    RoboCheckEqInt(FontSourceCreateMemoryCopy(buf, n, &src), RoboOk);
    RoboCheckEqInt(FontSfntParse(src, 0, 0, &sfnt), RoboParseError);
    FontSourceRelease(src);

    /* Unaligned offset */
    n = BuildSfnt(buf, sizeof(buf), RoboFontSfntVersionTrue, tables, 1u, 0u);
    PutU32be(buf + 12 + 8, 29u); /* not multiple of 4 */
    PutU32be(buf + 12 + 12, 1u);
    RoboCheckEqInt(FontSourceCreateMemoryCopy(buf, n, &src), RoboOk);
    RoboCheckEqInt(FontSfntParse(src, 0, 0, &sfnt), RoboParseError);
    FontSourceRelease(src);

    /* Duplicate tags */
    {
        SynthTable dup[2];
        dup[0].tag = RoboFontTagCmap;
        dup[0].data = payload;
        dup[0].length = 4u;
        dup[1].tag = RoboFontTagCmap;
        dup[1].data = payload;
        dup[1].length = 4u;
        n = BuildSfnt(buf, sizeof(buf), RoboFontSfntVersionTrue, dup, 2u, 0u);
        RoboCheckEqInt(FontSourceCreateMemoryCopy(buf, n, &src), RoboOk);
        RoboCheckEqInt(FontSfntParse(src, 0, 0, &sfnt), RoboParseError);
        FontSourceRelease(src);
    }

    /* Overlapping regions */
    {
        SynthTable ov[2];
        ov[0].tag = RoboFontTagHhea;
        ov[0].data = payload;
        ov[0].length = 4u;
        ov[1].tag = RoboFontTagHmtx;
        ov[1].data = payload;
        ov[1].length = 4u;
        n = BuildSfnt(buf, sizeof(buf), RoboFontSfntVersionTrue, ov, 2u, 0u);
        {
            uint32_t o0 =
                ((uint32_t)buf[12 + 8] << 24) | ((uint32_t)buf[12 + 9] << 16) |
                ((uint32_t)buf[12 + 10] << 8) | (uint32_t)buf[12 + 11];
            PutU32be(buf + 12 + 16 + 8, o0);
            PutU32be(buf + 12 + 16 + 12, 4u);
        }
        RoboCheckEqInt(FontSourceCreateMemoryCopy(buf, n, &src), RoboOk);
        RoboCheckEqInt(FontSfntParse(src, 0, 0, &sfnt), RoboParseError);
        FontSourceRelease(src);
    }

    /* Checksum mismatch with verify flag */
    n = BuildSfnt(buf, sizeof(buf), RoboFontSfntVersionTrue, tables, 1u, 0u);
    PutU32be(buf + 12 + 4, 0xDEADBEEFu);
    RoboCheckEqInt(FontSourceCreateMemoryCopy(buf, n, &src), RoboOk);
    RoboCheckEqInt(
        FontSfntParse(src, 0, RoboFontSfntFlagsVerifyChecksums, &sfnt),
        RoboParseError);
    /* Without flag, still parses */
    RoboCheckEqInt(FontSfntParse(src, 0, RoboFontSfntFlagsNone, &sfnt),
                      RoboOk);
    RoboCheckEqInt(FontSfntVerifyTableChecksum(sfnt, RoboFontTagHead),
                      RoboParseError);
    FontSfntRelease(sfnt);
    FontSourceRelease(src);

    /* Fuzz entry must not crash on garbage */
    RoboCheck(FontSfntFuzzParse((const uint8_t *)"garbage!!", 9) != RoboOk);
    RoboCheckEqInt(FontSfntFuzzParse(NULL, 0), RoboInvalidArgument);
}

static void TestSfntTtcBasic(void)
{
    /* ttcf + version 1.0 + 1 font + offset to SFNT */
    uint8_t buf[160];
    static const uint8_t payload[] = {0xDE, 0xAD, 0xBE, 0xEF};
    SynthTable tables[1];
    size_t faceSize;
    size_t faceOff = 16u; /* after ttcf header + 1 offset */
    FontSource *src = NULL;
    FontSfnt *sfnt = NULL;

    memset(buf, 0, sizeof(buf));
    PutU32be(buf, RoboFontSfntVersionTtcf);
    PutU32be(buf + 4, 0x00010000u);
    PutU32be(buf + 8, 1u);
    PutU32be(buf + 12, (uint32_t)faceOff);

    tables[0].tag = RoboFontTagPost;
    tables[0].data = payload;
    tables[0].length = 4u;
    /* Table offsets in a collection are absolute from the file start. */
    faceSize = BuildSfnt(buf + faceOff, sizeof(buf) - faceOff,
                           RoboFontSfntVersionTrue, tables, 1u, (uint32_t)faceOff);
    RoboCheck(faceSize > 0u);

    RoboCheckEqInt(
        FontSourceCreateMemoryCopy(buf, faceOff + faceSize, &src), RoboOk);
    RoboCheckEqInt(FontSfntParse(src, 0, 0, &sfnt), RoboOk);
    RoboCheck(FontSfntHasTable(sfnt, RoboFontTagPost));
    FontSfntRelease(sfnt);
    sfnt = NULL;
    RoboCheckEqInt(FontSfntParse(src, 1, 0, &sfnt), RoboNotFound);
    RoboCheck(sfnt == NULL);
    FontSourceRelease(src);
}

static void TestSfntLifetime(void)
{
    uint8_t buf[128];
    static const uint8_t payload[] = {1, 2, 3, 4};
    SynthTable tables[1];
    size_t n;
    FontSource *src = NULL;
    FontSfnt *sfnt = NULL;

    tables[0].tag = RoboFontTagGsub;
    tables[0].data = payload;
    tables[0].length = 4u;
    n = BuildSfnt(buf, sizeof(buf), RoboFontSfntVersionOtto, tables, 1u, 0u);
    RoboCheckEqInt(FontSourceCreateMemoryCopy(buf, n, &src), RoboOk);
    RoboCheckEqInt(FontSfntParse(src, 0, 0, &sfnt), RoboOk);
    FontSourceRelease(src); /* SFNT still holds a ref */
    RoboCheck(FontSfntSource(sfnt) != NULL);
    RoboCheckEqInt((int)FontSourceSize(FontSfntSource(sfnt)), (int)n);
    FontSfntRetain(sfnt);
    FontSfntRelease(sfnt);
    FontSfntRelease(sfnt);
    FontSfntRelease(NULL);
}

void TestRegisterFontSfnt(void)
{
    TestRegister("font.sfnt.binary_reader", TestBinaryReader);
    TestRegister("font.sfnt.minimal_true", TestSfntMinimalTrue);
    TestRegister("font.sfnt.signatures", TestSfntSignatures);
    TestRegister("font.sfnt.multi_table", TestSfntMultiTableEnum);
    TestRegister("font.sfnt.zero_length", TestSfntZeroLengthTable);
    TestRegister("font.sfnt.malformed", TestSfntMalformed);
    TestRegister("font.sfnt.ttc", TestSfntTtcBasic);
    TestRegister("font.sfnt.lifetime", TestSfntLifetime);
}
