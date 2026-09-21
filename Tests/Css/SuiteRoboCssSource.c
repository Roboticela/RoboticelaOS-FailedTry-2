/*
 * RoboCSS Phase 9.1 source suite:
 * configuration, ownership, positions, ranges, byte description.
 */

#include "RoboCssTestSupport.h"

static void TestCssSourceKinds(void)
{
    CssSource source = RoboCssCleared;
    const char *text = CSS_FIXTURE_PARAGRAPH;

    CssFixture();
    CssSourceInit(&source);
    RoboCheckEqInt(source.kind, RoboCssSourceKindNone);
    RoboCheck(!CssSourceIsConfigured(&source));
    RoboCheck(CssSourceIsEmpty(&source));
    RoboCheck(CssSourceBytes(&source) == NULL);
    RoboCheckEqInt(CssSourceLength(&source), 0u);
    RoboCheck(!CssSourceIsOwned(&source));

    RoboCheckEqInt(CssSourceSetEmpty(&source, NULL), RoboOk);
    RoboCheckEqInt(source.kind, RoboCssSourceKindEmpty);
    RoboCheck(CssSourceIsConfigured(&source));
    RoboCheck(CssSourceIsEmpty(&source));
    RoboCheck(!CssSourceIsOwned(&source));

    RoboCheckEqInt(CssSourceSetString(&source, text, NULL), RoboOk);
    RoboCheckEqInt(source.kind, RoboCssSourceKindString);
    /* A borrowed source points at the caller's bytes: no copy was made. */
    RoboCheck(CssSourceBytes(&source) == (const uint8_t *)text);
    RoboCheckEqInt(CssSourceLength(&source), strlen(text));
    RoboCheck(!CssSourceIsOwned(&source));

    /* A NULL string is an explicit empty source, not an error. */
    RoboCheckEqInt(CssSourceSetString(&source, NULL, NULL), RoboOk);
    RoboCheckEqInt(source.kind, RoboCssSourceKindEmpty);

    RoboCheckEqInt(CssSourceSetMemory(&source,
                                                 (const uint8_t *)text,
                                                 strlen(text), NULL),
                      RoboOk);
    RoboCheckEqInt(source.kind, RoboCssSourceKindMemory);

    /* A zero-length memory source is empty, not a borrow of nothing. */
    RoboCheckEqInt(CssSourceSetMemory(&source, NULL, 0u, NULL), RoboOk);
    RoboCheckEqInt(source.kind, RoboCssSourceKindEmpty);
    RoboCheckEqInt(CssSourceSetMemory(&source, (const uint8_t *)text, 0u,
                                                 NULL),
                      RoboOk);
    RoboCheckEqInt(source.kind, RoboCssSourceKindEmpty);

    CssSourceDispose(&source);
    RoboCheckEqInt(source.kind, RoboCssSourceKindNone);
}

static void TestCssSourceCopyOwnsBytes(void)
{
    char buffer[64];
    CssSource source = RoboCssCleared;

    CssFixture();
    strcpy(buffer, CSS_FIXTURE_CLASS);
    CssSourceInit(&source);

    RoboCheckEqInt(CssSourceCopy(&source, (const uint8_t *)buffer,
                                           strlen(buffer), NULL),
                      RoboOk);
    RoboCheckEqInt(source.kind, RoboCssSourceKindCopy);
    RoboCheck(CssSourceIsOwned(&source));
    RoboCheck(CssSourceBytes(&source) != (const uint8_t *)buffer);

    /* Damage the caller's buffer: the copy must be unaffected, which is the
     * entire reason the owned kind exists. */
    buffer[0] = 'X';
    RoboCheckEqInt(CssSourceBytes(&source)[0], (uint8_t)'.');
    RoboCheckEqInt(CssSourceLength(&source), strlen(CSS_FIXTURE_CLASS));

    /* Dispose is idempotent and repeated disposal is not a double free. */
    CssSourceDispose(&source);
    RoboCheckEqInt(source.kind, RoboCssSourceKindNone);
    CssSourceDispose(&source);
    RoboCheckEqInt(source.kind, RoboCssSourceKindNone);
    CssSourceDispose(NULL);
}

static void TestCssSourceReconfigureReleases(void)
{
    char buffer[64];
    CssSource source = RoboCssCleared;
    int weEnabled;
    size_t baseline;

    CssFixture();
    weEnabled = CssAllocatorEnsureEnabled();
    strcpy(buffer, CSS_FIXTURE_BODY);
    CssSourceInit(&source);

    baseline = MemDebugLiveCount();
    {
        int i;

        for (i = 0; i < 8; i++) {
            /* Copy, then overwrite with a borrow, then copy again: every path
             * that replaces content must release what the previous one owned. */
            RoboCheckEqInt(CssSourceCopy(&source, (const uint8_t *)buffer,
                                                   strlen(buffer), NULL),
                              RoboOk);
            RoboCheckEqInt(CssSourceSetString(&source, buffer, NULL),
                              RoboOk);
            RoboCheckEqInt(CssSourceCopy(&source, (const uint8_t *)buffer,
                                                   strlen(buffer), NULL),
                              RoboOk);
            RoboCheckEqInt(CssSourceSetEmpty(&source, NULL), RoboOk);
        }
    }
    CssSourceDispose(&source);
    RoboCheckEqInt((int)MemDebugLiveCount(), (int)baseline);
    CssAllocatorRestore(weEnabled);
}

static void TestCssSourceRejectsBadArguments(void)
{
    CssSource source = RoboCssCleared;
    CssDiag diag;

    CssFixture();
    CssSourceInit(&source);
    RoboCheckEqInt(CssSourceSetEmpty(&source, NULL), RoboOk);

    /* NULL bytes with a length is a caller mistake, and the source is left as
     * it was rather than half-configured. */
    CssDiagReset(&diag);
    RoboCheckEqInt(CssSourceSetMemory(&source, NULL, 4u, &diag),
                      RoboInvalidArgument);
    RoboCheckEqInt(diag.domain, RoboCssDomainSource);
    RoboCheckEqInt(source.kind, RoboCssSourceKindEmpty);
    RoboCheckEqInt(CssSourceSetMemory(&source, NULL, 4u, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(CssSourceCopy(&source, NULL, 4u, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(source.kind, RoboCssSourceKindEmpty);

    /* Every entry point tolerates a NULL source, and says so. */
    RoboCheckEqInt(CssSourceSetEmpty(NULL, NULL), RoboInvalidArgument);
    RoboCheckEqInt(CssSourceSetMemory(NULL, NULL, 0u, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(CssSourceSetString(NULL, "x", NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(CssSourceCopy(NULL, NULL, 0u, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(CssSourceSetName(NULL, "x", NULL), RoboInvalidArgument);
    RoboCheck(CssSourceName(NULL) == NULL);
    RoboCheck(CssSourceBytes(NULL) == NULL);
    RoboCheckEqInt(CssSourceLength(NULL), 0u);
    RoboCheck(CssSourceIsEmpty(NULL));
    RoboCheck(!CssSourceIsOwned(NULL));
    RoboCheck(!CssSourceIsConfigured(NULL));
    RoboCheck(CssSourceSlice(NULL).bytes == NULL);
}

static void TestCssSourceLabels(void)
{
    CssSource source = RoboCssCleared;
    char longLabel[RoboCssNameMax + 32];
    size_t i;

    CssFixture();
    CssSourceInit(&source);
    RoboCheckStrEq(CssSourceName(&source), "");

    RoboCheckEqInt(CssSourceSetName(&source, "Sheet 1", NULL), RoboOk);
    RoboCheckStrEq(CssSourceName(&source), "Sheet 1");

    /* A label is diagnostics: too long is truncated, never refused. */
    for (i = 0u; i < sizeof(longLabel) - 1u; i++) {
        longLabel[i] = 'L';
    }
    longLabel[sizeof(longLabel) - 1u] = '\0';
    RoboCheckEqInt(CssSourceSetName(&source, longLabel, NULL), RoboOk);
    RoboCheckEqInt(strlen(CssSourceName(&source)), (size_t)RoboCssNameMax - 1u);

    /* A cleared content path keeps the label: the source still names itself. */
    RoboCheckEqInt(CssSourceSetEmpty(&source, NULL), RoboOk);
    RoboCheckEqInt(strlen(CssSourceName(&source)), (size_t)RoboCssNameMax - 1u);

    /* Disposing clears everything, including the label. */
    CssSourceDispose(&source);
    RoboCheckStrEq(CssSourceName(&source), "");
}

static void TestCssSourcePositionsLf(void)
{
    CssSource source = RoboCssCleared;
    CssPosition position;

    CssFixture();
    CssSourceOfString(&source, "p {\n  color: red;\n}\n");

    RoboCheckEqInt(CssSourcePosition(&source, 0u, &position), RoboOk);
    RoboCheckEqInt(position.line, 1u);
    RoboCheckEqInt(position.column, 1u);

    RoboCheckEqInt(CssSourcePosition(&source, 2u, &position), RoboOk);
    RoboCheckEqInt(position.line, 1u);
    RoboCheckEqInt(position.column, 3u);

    /* The offset of the LF itself is still on line 1... */
    RoboCheckEqInt(CssSourcePosition(&source, 3u, &position), RoboOk);
    RoboCheckEqInt(position.line, 1u);
    RoboCheckEqInt(position.column, 4u);
    /* ...and one past it is the start of line 2. */
    RoboCheckEqInt(CssSourcePosition(&source, 4u, &position), RoboOk);
    RoboCheckEqInt(position.line, 2u);
    RoboCheckEqInt(position.column, 1u);
    RoboCheckEqInt(CssSourcePosition(&source, 6u, &position), RoboOk);
    RoboCheckEqInt(position.line, 2u);
    RoboCheckEqInt(position.column, 3u);

    /* The end offset is a valid position: the cursor reaching the end. */
    RoboCheckEqInt(CssSourcePosition(&source, strlen("p {\n  color: red;\n}\n"),
                                               &position),
                      RoboOk);
    RoboCheckEqInt(position.line, 4u);
    RoboCheckEqInt(position.column, 1u);

    RoboCheckEqInt(CssSourcePosition(&source, 99u, &position),
                      RoboInvalidArgument);
    RoboCheckEqInt(CssSourcePosition(NULL, 0u, &position),
                      RoboInvalidArgument);
    RoboCheckEqInt(CssSourcePosition(&source, 0u, NULL),
                      RoboInvalidArgument);
}

static void TestCssSourcePositionsCrlfAndCr(void)
{
    CssSource source = RoboCssCleared;
    CssPosition position;

    CssFixture();
    /* CRLF is one break, so the second line starts at offset 3. */
    CssSourceOfString(&source, "a\r\nb");
    RoboCheckEqInt(CssSourceLineCount(&source), 2u);
    RoboCheckEqInt(CssSourcePosition(&source, 3u, &position), RoboOk);
    RoboCheckEqInt(position.line, 2u);
    RoboCheckEqInt(position.column, 1u);
    RoboCheckEqInt(CssSourcePosition(&source, 4u, &position), RoboOk);
    RoboCheckEqInt(position.line, 2u);
    RoboCheckEqInt(position.column, 2u);

    /* A lone CR is also a break. */
    CssSourceDispose(&source);
    CssSourceOfString(&source, "a\rb");
    RoboCheckEqInt(CssSourceLineCount(&source), 2u);
    RoboCheckEqInt(CssSourcePosition(&source, 2u, &position), RoboOk);
    RoboCheckEqInt(position.line, 2u);
    RoboCheckEqInt(position.column, 1u);

    /* A source with no break at all is still one line. */
    CssSourceDispose(&source);
    CssSourceOfString(&source, "abc");
    RoboCheckEqInt(CssSourceLineCount(&source), 1u);
    RoboCheckEqInt(CssSourcePosition(&source, 3u, &position), RoboOk);
    RoboCheckEqInt(position.line, 1u);
    RoboCheckEqInt(position.column, 4u);

    /* Empty: no lines to count, but offset 0 is still a position. */
    CssSourceDispose(&source);
    RoboCheckEqInt(CssSourceLineCount(&source), 0u);
    RoboCheckEqInt(CssSourcePosition(&source, 0u, &position), RoboOk);
    RoboCheckEqInt(position.line, 1u);
    RoboCheckEqInt(position.column, 1u);

    CssSourceDispose(&source);
}

static void TestCssSourcePositionsCountScalars(void)
{
    CssSource source = RoboCssCleared;
    CssPosition position;

    CssFixture();
    /* Three two-byte scalars then a newline: the column after them is 4, not 7,
     * because a column number has to mean the same thing in every language. */
    CssSourceOfString(&source, "caf\xC3\xA9##\nnext");
    RoboCheckEqInt(CssSourcePosition(&source, 7u, &position), RoboOk);
    RoboCheckEqInt(position.line, 1u);
    RoboCheckEqInt(position.column, 7u);

    CssSourceDispose(&source);
    CssSourceOfString(&source, "\xD8\xA7\xD9\x84\nx");
    RoboCheckEqInt(CssSourcePosition(&source, 4u, &position), RoboOk);
    RoboCheckEqInt(position.line, 1u);
    RoboCheckEqInt(position.column, 3u);
    RoboCheckEqInt(CssSourcePosition(&source, 5u, &position), RoboOk);
    RoboCheckEqInt(position.line, 2u);
    RoboCheckEqInt(position.column, 1u);

    /* An emoji is one scalar even at four bytes, and a variation selector adds
     * one more — this phase counts scalars, it does not segment graphemes. */
    CssSourceDispose(&source);
    CssSourceOfString(&source, "\xF0\x9F\x8E\xA8\xEF\xB8\x8F!");
    RoboCheckEqInt(CssSourceScalarCount(&source), 3u);
    RoboCheckEqInt(CssSourcePosition(&source, 7u, &position), RoboOk);
    RoboCheckEqInt(position.column, 3u);

    CssSourceDispose(&source);
}

static void TestCssSourceRanges(void)
{
    CssSource source = RoboCssCleared;
    CssSlice slice;
    const char *text = CSS_FIXTURE_ID;

    CssFixture();
    CssSourceOfString(&source, text);

    RoboCheck(CssSourceRangeIsValid(&source, CssRangeSpan(0u, 1u)));
    RoboCheck(CssSourceRangeIsValid(&source, CssRangeAt(0u)));
    /* The empty range at the very end is valid: that is the cursor at EOF. */
    RoboCheck(CssSourceRangeIsValid(&source, CssRangeAt(strlen(text))));
    RoboCheck(!CssSourceRangeIsValid(&source,
                                               CssRangeSpan(0u, strlen(text) + 1u)));
    RoboCheck(!CssSourceRangeIsValid(&source,
                                               (CssRange){5u, 2u}));
    RoboCheck(!CssSourceRangeIsValid(NULL, CssRangeAt(0u)));

    /* CSS_FIXTURE_ID is "#title { ... }": offsets 1..5 are the id selector's
     * own name, which is the span a selector would later keep. */
    RoboCheckEqInt(CssSourceRangeSlice(&source, CssRangeSpan(1u, 5u),
                                                  &slice),
                      RoboOk);
    RoboCheckEqInt(slice.length, 5u);
    RoboCheck(CssSliceEqualsAscii(slice, "title"));
    /* The slice borrows the source: it points into the source's own bytes at
     * the range's begin, so reading it back from here still sees the source. */
    RoboCheck(slice.bytes == CssSourceBytes(&source) + 1u);

    /* A valid empty range yields an empty slice, not an absent one. */
    RoboCheckEqInt(CssSourceRangeSlice(&source, CssRangeAt(3u),
                                                  &slice),
                      RoboOk);
    RoboCheck(CssSliceIsEmpty(slice));

    RoboCheckEqInt(CssSourceRangeSlice(&source,
                                                  CssRangeSpan(0u, 999u), &slice),
                      RoboInvalidArgument);
    RoboCheckEqInt(CssSourceRangeSlice(&source, CssRangeAt(0u), NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(CssSourceRangeSlice(NULL, CssRangeAt(0u), &slice),
                      RoboInvalidArgument);
    RoboCheck(CssSliceIsEmpty(slice));

    /* Offsets: 0 through length inclusive. */
    RoboCheck(CssSourceOffsetIsValid(&source, 0u));
    RoboCheck(CssSourceOffsetIsValid(&source, strlen(text)));
    RoboCheck(!CssSourceOffsetIsValid(&source, strlen(text) + 1u));
    RoboCheck(!CssSourceOffsetIsValid(NULL, 0u));

    CssSourceDispose(&source);
}

static void TestCssSourceUtf8Description(void)
{
    CssSource source = RoboCssCleared;

    CssFixture();
    CssSourceOfString(&source, "body { color: red; }");
    RoboCheck(CssSourceIsValidUtf8(&source));
    RoboCheckEqInt(CssSourceScalarCount(&source), strlen("body { color: red; }"));

    /* Ill-formed bytes are reported and preserved, never repaired. */
    CssSourceDispose(&source);
    {
        static const uint8_t bad[] = {'a', 0xC3u, 0x28u, 'b'};
        int weEnabled = CssAllocatorEnsureEnabled();
        size_t baseline = MemDebugLiveCount();

        RoboCheckEqInt(CssSourceSetMemory(&source, bad, sizeof(bad), NULL),
                          RoboOk);
        RoboCheck(!CssSourceIsValidUtf8(&source));
        /* An ill-formed byte is one position, so the count stays meaningful. */
        RoboCheckEqInt(CssSourceScalarCount(&source), 4u);
        RoboCheckEqInt(CssSourceBytes(&source)[1], 0xC3u);
        RoboCheckEqInt((int)MemDebugLiveCount(), (int)baseline);
        CssAllocatorRestore(weEnabled);
    }

    /* Overlong, surrogate and out-of-range forms are all ill-formed. */
    CssSourceDispose(&source);
    {
        static const uint8_t overlong[] = {0xC0u, 0x80u};
        static const uint8_t surrogate[] = {0xEDu, 0xA0u, 0x80u};
        static const uint8_t tooBig[] = {0xF5u, 0x80u, 0x80u, 0x80u};

        RoboCheckEqInt(CssSourceSetMemory(&source, overlong,
                                                     sizeof(overlong), NULL),
                          RoboOk);
        RoboCheck(!CssSourceIsValidUtf8(&source));
        RoboCheckEqInt(CssSourceScalarCount(&source), 2u);

        RoboCheckEqInt(CssSourceSetMemory(&source, surrogate,
                                                     sizeof(surrogate), NULL),
                          RoboOk);
        RoboCheck(!CssSourceIsValidUtf8(&source));

        RoboCheckEqInt(CssSourceSetMemory(&source, tooBig,
                                                     sizeof(tooBig), NULL),
                          RoboOk);
        RoboCheck(!CssSourceIsValidUtf8(&source));
    }

    /* A well-formed four-byte scalar is valid and counts as one. */
    CssSourceDispose(&source);
    CssSourceOfString(&source, "\xF0\x9F\x98\x80");
    RoboCheck(CssSourceIsValidUtf8(&source));
    RoboCheckEqInt(CssSourceScalarCount(&source), 1u);

    CssSourceDispose(&source);
    RoboCheck(CssSourceIsValidUtf8(&source));
    RoboCheckEqInt(CssSourceScalarCount(&source), 0u);
}

static void TestCssSourcePreservesUnicode(void)
{
    size_t i;

    CssFixture();
    for (i = 0u; i < CSS_SAMPLE_COUNT; i++) {
        static const char prefix[] = "/* ";
        static const char suffix[] = " */\np { color: red; }\n";
        char rule[512];
        char copied[512];
        CssSource borrowed = RoboCssCleared;
        CssSource owned = RoboCssCleared;
        size_t ruleLength;
        size_t j;
        int same = 1;

        /* A stylesheet written in each script family — the sample in a comment,
         * which is exactly how non-ASCII bytes most often appear in CSS. The
         * bytes must come back byte-for-byte, borrowed or copied. */
        ruleLength = 0u;
        memcpy(rule, prefix, strlen(prefix));
        ruleLength += strlen(prefix);
        memcpy(rule + ruleLength, cssSamples[i].text, strlen(cssSamples[i].text));
        ruleLength += strlen(cssSamples[i].text);
        memcpy(rule + ruleLength, suffix, strlen(suffix));
        ruleLength += strlen(suffix);
        rule[ruleLength] = '\0';

        CssSourceOfString(&borrowed, rule);
        RoboCheck(CssSourceIsValidUtf8(&borrowed));
        RoboCheckEqInt(CssSourceLength(&borrowed), ruleLength);
        for (j = 0u; j < ruleLength; j++) {
            if (CssSourceBytes(&borrowed)[j] != (uint8_t)rule[j]) {
                same = 0;
            }
        }
        RoboCheckMsg(same, cssSamples[i].label);

        CssSourceInit(&owned);
        RoboCheckEqInt(CssSourceCopy(&owned, (const uint8_t *)rule,
                                               ruleLength, NULL),
                          RoboOk);
        RoboCheck(CssSourceIsOwned(&owned));
        RoboCheckEqInt(CssSourceLength(&owned),
                          CssSourceLength(&borrowed));
        for (j = 0u; j < ruleLength; j++) {
            copied[j] = (char)CssSourceBytes(&owned)[j];
        }
        copied[ruleLength] = '\0';
        RoboCheckStrEq(copied, rule);

        /* A column count is a scalar count, so a multi-byte sample must not
         * inflate it the way a byte count would. */
        RoboCheck(CssSourceScalarCount(&owned) <=
                   CssSourceLength(&owned));
        RoboCheckEqInt(CssSourceScalarCount(&owned),
                          CssSourceScalarCount(&borrowed));

        CssSourceDispose(&owned);
        CssSourceDispose(&borrowed);
    }
}

void TestRegisterCssSource(void);

void TestRegisterCssSource(void)
{
    TestRegister("css.source.kinds", TestCssSourceKinds);
    TestRegister("css.source.copy_owns_bytes", TestCssSourceCopyOwnsBytes);
    TestRegister("css.source.reconfigure_releases",
                       TestCssSourceReconfigureReleases);
    TestRegister("css.source.rejects_bad_arguments",
                       TestCssSourceRejectsBadArguments);
    TestRegister("css.source.labels", TestCssSourceLabels);
    TestRegister("css.source.positions_lf", TestCssSourcePositionsLf);
    TestRegister("css.source.positions_crlf_and_cr",
                       TestCssSourcePositionsCrlfAndCr);
    TestRegister("css.source.positions_scalars",
                       TestCssSourcePositionsCountScalars);
    TestRegister("css.source.ranges", TestCssSourceRanges);
    TestRegister("css.source.utf8_description", TestCssSourceUtf8Description);
    TestRegister("css.source.preserves_unicode",
                       TestCssSourcePreservesUnicode);
}
