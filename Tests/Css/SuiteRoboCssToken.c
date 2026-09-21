/*
 * RoboCSS Phase 9.1 token suite:
 * the token vocabulary, the bracket pairings, and binding a token to a range.
 *
 * Every test here asserts what the *record* is. None of them claims a byte was
 * classified into a token: that is the tokenizer's job in 9.2, and this build
 * has no tokenizer.
 */

#include "RoboCssTestSupport.h"

static void TestCssTokenVocabulary(void)
{
    CssTokenType type;

    for (type = RoboCssTokenNone;
         type < (CssTokenType)RoboCssTokenTypeCount;
         type = (CssTokenType)(type + 1)) {
        const char *name = CssTokenTypeName(type);

        RoboCheck(CssTokenTypeIsKnown(type));
        RoboCheck(name != NULL);
        RoboCheck(strcmp(name, "unknown") != 0);
    }
    RoboCheck(!CssTokenTypeIsKnown((CssTokenType)99));
    RoboCheckStrEq(CssTokenTypeName((CssTokenType)99), "unknown");

    /* The specific spellings other phases will compare against. */
    RoboCheckStrEq(CssTokenTypeName(RoboCssTokenAtKeyword), "at-keyword");
    RoboCheckStrEq(CssTokenTypeName(RoboCssTokenDimension),
                      "dimension");
    RoboCheckStrEq(CssTokenTypeName(RoboCssTokenLeftCurly),
                      "left-curly");
    RoboCheckStrEq(CssTokenTypeName(RoboCssTokenInvalid), "invalid");
}

static void TestCssTokenClassification(void)
{
    RoboCheck(CssTokenTypeIsNumeric(RoboCssTokenNumber));
    RoboCheck(CssTokenTypeIsNumeric(RoboCssTokenPercentage));
    RoboCheck(CssTokenTypeIsNumeric(RoboCssTokenDimension));
    RoboCheck(!CssTokenTypeIsNumeric(RoboCssTokenIdent));
    RoboCheck(!CssTokenTypeIsNumeric(RoboCssTokenString));

    /* Trivia is the two kinds a grammar steps over, and only those. */
    RoboCheck(CssTokenTypeIsIgnorable(RoboCssTokenWhitespace));
    RoboCheck(CssTokenTypeIsIgnorable(RoboCssTokenComment));
    RoboCheck(!CssTokenTypeIsIgnorable(RoboCssTokenColon));
    RoboCheck(!CssTokenTypeIsIgnorable(RoboCssTokenDelim));

    RoboCheck(CssTokenTypeIsBlockStart(RoboCssTokenLeftParen));
    RoboCheck(CssTokenTypeIsBlockStart(RoboCssTokenLeftSquare));
    RoboCheck(CssTokenTypeIsBlockStart(RoboCssTokenLeftCurly));
    RoboCheck(!CssTokenTypeIsBlockStart(RoboCssTokenComma));
    RoboCheck(CssTokenTypeIsBlockEnd(RoboCssTokenRightCurly));
    RoboCheck(!CssTokenTypeIsBlockEnd(RoboCssTokenLeftCurly));

    /* A token cannot be both, and the six brackets split three and three. */
    {
        int starts = 0;
        int ends = 0;
        CssTokenType type;

        for (type = RoboCssTokenNone;
             type < (CssTokenType)RoboCssTokenTypeCount;
             type = (CssTokenType)(type + 1)) {
            int isStart = CssTokenTypeIsBlockStart(type);
            int isEnd = CssTokenTypeIsBlockEnd(type);

            RoboCheck(!(isStart && isEnd));
            starts += isStart;
            ends += isEnd;
        }
        RoboCheckEqInt(starts, 3);
        RoboCheckEqInt(ends, 3);
    }
}

static void TestCssTokenBracketPairs(void)
{
    CssTokenType close = RoboCssTokenNone;
    CssTokenType open = RoboCssTokenNone;

    RoboCheck(CssTokenTypeMatchingClose(RoboCssTokenLeftParen, &close));
    RoboCheckEqInt(close, RoboCssTokenRightParen);
    RoboCheck(CssTokenTypeMatchingClose(RoboCssTokenLeftSquare, &close));
    RoboCheckEqInt(close, RoboCssTokenRightSquare);
    RoboCheck(CssTokenTypeMatchingClose(RoboCssTokenLeftCurly, &close));
    RoboCheckEqInt(close, RoboCssTokenRightCurly);

    /* A non-bracket sets the output back to NONE and answers no. */
    close = RoboCssTokenRightParen;
    RoboCheck(!CssTokenTypeMatchingClose(RoboCssTokenIdent, &close));
    RoboCheckEqInt(close, RoboCssTokenNone);

    RoboCheck(CssTokenTypeMatchingOpen(RoboCssTokenRightParen, &open));
    RoboCheckEqInt(open, RoboCssTokenLeftParen);
    RoboCheck(CssTokenTypeMatchingOpen(RoboCssTokenRightSquare, &open));
    RoboCheckEqInt(open, RoboCssTokenLeftSquare);
    RoboCheck(CssTokenTypeMatchingOpen(RoboCssTokenRightCurly, &open));
    RoboCheckEqInt(open, RoboCssTokenLeftCurly);
    RoboCheck(!CssTokenTypeMatchingOpen(RoboCssTokenColon, &open));

    /* The pairing is symmetric for all six brackets, and the output is
     * optional (the answer is then only the return value). */
    {
        CssTokenType type;

        for (type = RoboCssTokenNone;
             type < (CssTokenType)RoboCssTokenTypeCount;
             type = (CssTokenType)(type + 1)) {
            CssTokenType other;

            if (!CssTokenTypeMatchingClose(type, &other)) {
                continue;
            }
            RoboCheck(CssTokenTypeMatchingOpen(other, NULL));
            RoboCheck(CssTokenTypeMatchingClose(type, NULL));
        }
    }
}

static void TestCssTokenHashTypes(void)
{
    RoboCheck(CssHashTypeIsKnown(RoboCssHashTypeId));
    RoboCheck(CssHashTypeIsKnown(RoboCssHashTypeNone));
    RoboCheck(!CssHashTypeIsKnown((CssHashType)9));
    RoboCheckStrEq(CssHashTypeName(RoboCssHashTypeId), "id");
    RoboCheckStrEq(CssHashTypeName(RoboCssHashTypeUnrestricted),
                      "unrestricted");
    RoboCheckStrEq(CssHashTypeName((CssHashType)9), "unknown");
}

static void TestCssTokenMakeBindsRange(void)
{
    CssSource source = RoboCssCleared;
    CssToken token = RoboCssCleared;

    CssFixture();
    CssSourceOfString(&source, "padding: 10px;");

    RoboCheckEqInt(CssTokenMake(&token, RoboCssTokenIdent, &source,
                                          CssRangeSpan(0u, 7u), NULL),
                      RoboOk);
    RoboCheckEqInt(token.type, RoboCssTokenIdent);
    RoboCheckEqInt(token.range.begin, 0u);
    RoboCheckEqInt(token.range.end, 7u);
    RoboCheckEqInt(CssTokenText(&token).length, 7u);
    RoboCheck(CssTokenEqualsAscii(&token, "padding"));

    /* A token constructed here is bound, not classified: the numeric fields are
     * what a tokenizer will fill, and a tokenizer does not exist yet. */
    RoboCheckEqInt(token.number, 0);
    RoboCheckEqInt(token.isInteger, 0);
    RoboCheckEqInt(token.hasSign, 0);
    RoboCheck(CssTokenText(&token).bytes ==
               CssSourceBytes(&source));

    RoboCheckEqInt(CssTokenMake(&token, RoboCssTokenDimension, &source,
                                          CssRangeSpan(9u, 4u), NULL),
                      RoboOk);
    RoboCheck(CssTokenEqualsAscii(&token, "10px"));
    RoboCheckEqInt(token.number, 0);
    RoboCheck(CssSliceIsEmpty(token.unit));
    RoboCheck(CssSliceIsEmpty(token.repr));

    /* Case-insensitive comparison is the one CSS keyword matching uses. */
    RoboCheckEqInt(CssTokenMake(&token, RoboCssTokenIdent, &source,
                                          CssRangeSpan(0u, 7u), NULL),
                      RoboOk);
    RoboCheck(CssTokenEqualsAsciiCi(&token, "PADDING"));
    RoboCheck(!CssTokenEqualsAscii(&token, "PADDING"));
    RoboCheck(!CssTokenEqualsAscii(&token, "paddin"));
    RoboCheck(!CssTokenEqualsAscii(&token, "padding!"));
}

static void TestCssTokenMakeRejects(void)
{
    CssSource source = RoboCssCleared;
    CssToken token = RoboCssCleared;
    CssDiag diag;

    CssFixture();
    CssSourceOfString(&source, "abc");
    CssTokenInit(&token);

    CssDiagReset(&diag);
    RoboCheckEqInt(CssTokenMake(&token, (CssTokenType)99, &source,
                                          CssRangeSpan(0u, 1u), &diag),
                      RoboInvalidArgument);
    RoboCheckEqInt(diag.domain, RoboCssDomainToken);
    /* The token is untouched: a refused construction leaves no half-token. */
    RoboCheckEqInt(token.type, RoboCssTokenNone);

    RoboCheckEqInt(CssTokenMake(&token, RoboCssTokenIdent, &source,
                                          CssRangeSpan(0u, 99u), NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(CssTokenMake(&token, RoboCssTokenIdent, &source,
                                          (CssRange){2u, 1u}, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(CssTokenMake(&token, RoboCssTokenIdent, NULL,
                                          CssRangeSpan(0u, 1u), NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(CssTokenMake(NULL, RoboCssTokenIdent, &source,
                                          CssRangeSpan(0u, 1u), NULL),
                      RoboInvalidArgument);
}

static void TestCssTokenEof(void)
{
    CssSource source = RoboCssCleared;
    CssToken token = RoboCssCleared;

    CssFixture();
    CssSourceOfString(&source, "a{b}");

    RoboCheckEqInt(CssTokenMakeEof(&token, &source, 4u, NULL), RoboOk);
    RoboCheckEqInt(token.type, RoboCssTokenEof);
    RoboCheck(CssTokenIsTerminator(&token));
    RoboCheck(!CssTokenIsNone(&token));
    /* EOF carries no bytes, whatever an empty range would have resolved to. */
    RoboCheck(CssSliceIsEmpty(CssTokenText(&token)));

    /* Past the end is refused; the end itself is the valid EOF offset. */
    RoboCheckEqInt(CssTokenMakeEof(&token, &source, 5u, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(CssTokenMakeEof(&token, NULL, 0u, NULL),
                      RoboInvalidArgument);

    /* An empty source has exactly one valid EOF offset: 0. */
    CssSourceDispose(&source);
    CssSourceOfString(&source, "");
    RoboCheckEqInt(CssTokenMakeEof(&token, &source, 0u, NULL), RoboOk);
    RoboCheckEqInt(token.type, RoboCssTokenEof);
    RoboCheckEqInt(CssTokenMakeEof(&token, &source, 1u, NULL),
                      RoboInvalidArgument);

    CssSourceDispose(&source);
}

static void TestCssTokenLifecycle(void)
{
    CssSource source = RoboCssCleared;
    CssToken token = RoboCssCleared;
    CssToken copy = RoboCssCleared;
    int weEnabled;
    size_t baseline;

    CssFixture();
    weEnabled = CssAllocatorEnsureEnabled();
    CssSourceOfString(&source, "color:red");
    baseline = MemDebugLiveCount();

    CssTokenInit(&token);
    RoboCheck(CssTokenIsNone(&token));
    RoboCheck(CssTokenIsTerminator(&token));
    RoboCheckEqInt(token.type, RoboCssTokenNone);
    RoboCheck(CssSliceIsAbsent(token.text));

    {
        int i;

        for (i = 0; i < 32; i++) {
            /* A token owns nothing, so make/copy/dispose must leave the
             * allocator untouched. That is the claim, and this is the check. */
            RoboCheckEqInt(CssTokenMake(&token, RoboCssTokenIdent,
                                                 &source, CssRangeSpan(0u, 5u),
                                                 NULL),
                              RoboOk);
            RoboCheckEqInt(CssTokenCopy(&token, &copy, NULL), RoboOk);
            RoboCheckEqInt(copy.type, RoboCssTokenIdent);
            RoboCheck(CssTokenEqualsAscii(&copy, "color"));
            CssTokenDispose(&copy);
            RoboCheck(CssTokenIsNone(&copy));
            RoboCheckEqInt((int)MemDebugLiveCount(), (int)baseline);
        }
    }

    RoboCheckEqInt(CssTokenCopy(&token, &copy, NULL), RoboOk);
    /* Copying onto itself is a no-op rather than a self-overwrite. */
    RoboCheckEqInt(CssTokenCopy(&copy, &copy, NULL), RoboOk);
    RoboCheck(CssTokenEqualsAscii(&copy, "color"));

    CssTokenDispose(&token);
    CssTokenDispose(NULL);
    RoboCheckEqInt((int)MemDebugLiveCount(), (int)baseline);
    RoboCheckEqInt(CssTokenCopy(NULL, &copy, NULL), RoboInvalidArgument);
    RoboCheckEqInt(CssTokenCopy(&copy, NULL, NULL), RoboInvalidArgument);
    RoboCheck(CssTokenIsNone(NULL));
    RoboCheck(CssTokenIsTerminator(NULL));
    RoboCheck(CssSliceIsAbsent(CssTokenText(NULL)));
    RoboCheck(!CssTokenEqualsAscii(NULL, "color"));
    RoboCheck(!CssTokenEqualsAsciiCi(NULL, "COLOR"));

    CssAllocatorRestore(weEnabled);
    CssSourceDispose(&source);
}

void TestRegisterCssToken(void);

void TestRegisterCssToken(void)
{
    TestRegister("css.token.vocabulary", TestCssTokenVocabulary);
    TestRegister("css.token.classification", TestCssTokenClassification);
    TestRegister("css.token.bracket_pairs", TestCssTokenBracketPairs);
    TestRegister("css.token.hash_types", TestCssTokenHashTypes);
    TestRegister("css.token.make_binds_range", TestCssTokenMakeBindsRange);
    TestRegister("css.token.make_rejects", TestCssTokenMakeRejects);
    TestRegister("css.token.eof", TestCssTokenEof);
    TestRegister("css.token.lifecycle", TestCssTokenLifecycle);
}
