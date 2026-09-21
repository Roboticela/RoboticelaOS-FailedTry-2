/*
 * RoboString unit suite (Goal 3.2).
 */

#include "RoboContainers/RoboString.h"
#include "RoboMem/RoboMemDebug.h"
#include "RoboTest.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static void TestStringEmpty(void)
{
    String s;

    RoboCheckEqInt(StringInit(NULL), RoboInvalidArgument);
    RoboCheckEqInt(StringInit(&s), RoboOk);
    RoboCheck(StringEmpty(&s));
    RoboCheckEqInt(RoboStringSize(&s), 0);
    RoboCheck(StringData(&s) != NULL);
    RoboCheckStrEq(StringData(&s), "");
    RoboCheck(StringEmpty(NULL));
    StringDestroy(&s);
    StringDestroy(NULL);
}

static void TestStringAscii(void)
{
    String s;

    RoboCheckEqInt(StringFromCstr(&s, "Hello"), RoboOk);
    RoboCheckEqInt(RoboStringSize(&s), 5);
    RoboCheckStrEq(StringData(&s), "Hello");
    RoboCheck(StringCapacity(&s) >= 5);
    StringDestroy(&s);
}

static void TestStringUtf8Multibyte(void)
{
    String s;
    /* "café" in UTF-8: c a f c3 a9 — 5 bytes, 4 characters */
    const char *cafe = "caf\xC3\xA9";

    RoboCheckEqInt(StringFromCstr(&s, cafe), RoboOk);
    RoboCheckEqInt(RoboStringSize(&s), 5);
    RoboCheckEqInt((unsigned char)StringData(&s)[3], 0xC3);
    RoboCheckEqInt((unsigned char)StringData(&s)[4], 0xA9);
    /* Documented: size is bytes, not Unicode scalar/grapheme count. */
    RoboCheck(RoboStringSize(&s) != 4);
    StringDestroy(&s);
}

static void TestStringAppendAndChar(void)
{
    String s;

    RoboCheckEqInt(StringInit(&s), RoboOk);
    RoboCheckEqInt(StringAppendCstr(&s, "ab"), RoboOk);
    RoboCheckEqInt(StringAppendChar(&s, 'c'), RoboOk);
    RoboCheckEqInt(StringAppend(&s, "de", 2), RoboOk);
    RoboCheckStrEq(StringData(&s), "abcde");
    RoboCheckEqInt(RoboStringSize(&s), 5);
    StringDestroy(&s);
}

static void TestStringSetClearReserve(void)
{
    String s;
    size_t cap;

    RoboCheckEqInt(StringInit(&s), RoboOk);
    RoboCheckEqInt(StringReserve(&s, 64), RoboOk);
    RoboCheckEqInt(StringCapacity(&s), 64);
    RoboCheckEqInt(StringSetCstr(&s, "xyz"), RoboOk);
    RoboCheckStrEq(StringData(&s), "xyz");
    RoboCheckEqInt(StringCapacity(&s), 64);

    cap = StringCapacity(&s);
    StringClear(&s);
    StringClear(NULL);
    RoboCheck(StringEmpty(&s));
    RoboCheckStrEq(StringData(&s), "");
    RoboCheckEqInt(StringCapacity(&s), (int)cap);

    RoboCheckEqInt(StringSet(&s, "hi", 2), RoboOk);
    RoboCheckEqInt(StringSetCstr(&s, NULL), RoboOk); /* → empty */
    RoboCheck(StringEmpty(&s));
    StringDestroy(&s);
}

static void TestStringCompare(void)
{
    String a;
    String b;
    String c;

    RoboCheckEqInt(StringFromCstr(&a, "abc"), RoboOk);
    RoboCheckEqInt(StringFromCstr(&b, "abc"), RoboOk);
    RoboCheckEqInt(StringFromCstr(&c, "abd"), RoboOk);

    RoboCheckEqInt(StringCompare(&a, &b), 0);
    RoboCheck(StringCompare(&a, &c) < 0);
    RoboCheck(StringCompare(&c, &a) > 0);
    RoboCheckEqInt(StringCompare(NULL, NULL), 0);
    RoboCheck(StringCompare(NULL, &a) < 0);

    /* Embedded NUL: size-aware compare */
    RoboCheckEqInt(StringSet(&a, "a\0b", 3), RoboOk);
    RoboCheckEqInt(StringSet(&b, "a\0c", 3), RoboOk);
    RoboCheck(StringCompare(&a, &b) < 0);
    RoboCheckEqInt(RoboStringSize(&a), 3);

    StringDestroy(&a);
    StringDestroy(&b);
    StringDestroy(&c);
}

static void TestStringLargeGrowth(void)
{
    String s;
    size_t i;
    size_t prevCap;

    RoboCheckEqInt(StringInit(&s), RoboOk);
    prevCap = StringCapacity(&s);
    for (i = 0; i < 200; i++) {
        RoboCheckEqInt(StringAppendChar(&s, (unsigned char)('a' + (i % 26))),
                          RoboOk);
    }
    RoboCheckEqInt(RoboStringSize(&s), 200);
    RoboCheck(StringCapacity(&s) >= 200);
    RoboCheck(StringCapacity(&s) > prevCap);
    RoboCheckEqInt(StringData(&s)[200], 0);
    StringDestroy(&s);
}

static void TestStringOomAndNull(void)
{
    String s;

    MemDebugFailNext(1);
    RoboCheckEqInt(StringInit(&s), RoboOutOfMemory);
    MemDebugFailNext(0);

    RoboCheckEqInt(StringInit(&s), RoboOk);
    MemDebugFailNext(1);
    RoboCheckEqInt(StringAppendCstr(&s, "grow-me-please"), RoboOutOfMemory);
    RoboCheck(StringEmpty(&s));

    MemDebugFailNext(1);
    RoboCheckEqInt(StringReserve(&s, 128), RoboOutOfMemory);
    MemDebugFailNext(0);

    RoboCheckEqInt(StringSet(&s, NULL, 1), RoboInvalidArgument);
    RoboCheckEqInt(StringAppend(&s, NULL, 1), RoboInvalidArgument);
    RoboCheckEqInt(StringAppendCstr(&s, NULL), RoboOk);
    RoboCheckEqInt(StringReserve(NULL, 8), RoboInvalidArgument);
    RoboCheck(StringData(NULL) == NULL);
    StringDestroy(&s);

    RoboCheckEqInt(StringFromCstr(&s, NULL), RoboOk);
    RoboCheck(StringEmpty(&s));
    RoboCheckStrEq(StringData(&s), "");
    StringDestroy(&s);
}

static void TestStringInvalidUtf8Opaque(void)
{
    String s;
    /* Lone continuation byte — invalid UTF-8, stored as opaque bytes. */
    const unsigned char bad[] = {0x80, 0x41};

    RoboCheckEqInt(StringInit(&s), RoboOk);
    RoboCheckEqInt(StringSet(&s, bad, 2), RoboOk);
    RoboCheckEqInt(RoboStringSize(&s), 2);
    RoboCheckEqInt((unsigned char)StringData(&s)[0], 0x80);
    RoboCheckEqInt((unsigned char)StringData(&s)[1], 0x41);
    RoboCheckEqInt(StringData(&s)[2], 0);
    StringDestroy(&s);
}

static void TestStringOverflow(void)
{
    String s;

    RoboCheckEqInt(StringInit(&s), RoboOk);
    /* Force overflow path: reserve SIZE_MAX should fail (need SIZE_MAX+1 alloc). */
    RoboCheckEqInt(StringReserve(&s, SIZE_MAX), RoboOutOfMemory);
    StringDestroy(&s);
}

void TestRegisterString(void)
{
    TestRegister("string.empty", TestStringEmpty);
    TestRegister("string.ascii", TestStringAscii);
    TestRegister("string.utf8_multibyte", TestStringUtf8Multibyte);
    TestRegister("string.append_char", TestStringAppendAndChar);
    TestRegister("string.set_clear_reserve", TestStringSetClearReserve);
    TestRegister("string.compare", TestStringCompare);
    TestRegister("string.large_growth", TestStringLargeGrowth);
    TestRegister("string.oom_null", TestStringOomAndNull);
    TestRegister("string.invalid_utf8", TestStringInvalidUtf8Opaque);
    TestRegister("string.overflow", TestStringOverflow);
}
