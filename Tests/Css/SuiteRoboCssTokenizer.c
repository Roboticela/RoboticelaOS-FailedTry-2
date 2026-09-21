/*
 * RoboCSS tokenizer stream suite (Phase 9.2): the machine around the tokens.
 *
 * The other four tokenizer suites assert what a token *is*. This one asserts
 * how the stream behaves — peek, skip, reset, EOF, the record of the first
 * problem, and the two properties a caller relies on and cannot see: that
 * lexing allocates nothing at all, and that a token stays valid after the
 * tokenizer that produced it is gone.
 */

#include "RoboCssTestSupport.h"

static void TestTokenizeStreamEofForEmptyInput(void)
{
    CssSource source = RoboCssCleared;
    CssTokenizer tokenizer = RoboCssCleared;
    CssToken token = RoboCssCleared;
    CssPosition position;

    CssSourceOfString(&source, "");
    RoboCheckEqInt(CssSourceLength(&source), 0u);
    RoboCheckEqInt(CssTokenizerInit(&tokenizer, &source, NULL), RoboOk);

    RoboCheckEqInt(CssTokenizerAtEnd(&tokenizer), 1);
    RoboCheckEqInt(CssTokenizerRemaining(&tokenizer), 0u);
    RoboCheckEqInt(CssTokenizerTokensProduced(&tokenizer), 0u);

    RoboCheckEqInt(CssTokenizerNext(&tokenizer, &token, NULL), RoboOk);
    RoboCheckEqInt(token.type, RoboCssTokenEof);
    RoboCheckEqInt(token.range.begin, 0u);
    RoboCheckEqInt(token.range.end, 0u);
    RoboCheck(CssSliceIsEmpty(token.text));
    RoboCheck(CssTokenIsTerminator(&token));
    RoboCheckEqInt(CssTokenizerTokensProduced(&tokenizer), 1u);
    RoboCheckEqInt(CssTokenizerOffset(&tokenizer), 0u);

    /* EOF is a value, not a state: asking again answers the same thing at the
     * same place, forever, and never moves the cursor. */
    RoboCheckEqInt(CssTokenizerNext(&tokenizer, &token, NULL), RoboOk);
    RoboCheckEqInt(token.type, RoboCssTokenEof);
    RoboCheckEqInt(CssTokenizerOffset(&tokenizer), 0u);
    RoboCheckEqInt(CssTokenizerPosition(&tokenizer, &position, NULL), RoboOk);
    RoboCheckEqInt(position.offset, 0u);
    RoboCheckEqInt(position.line, 1u);
    RoboCheckEqInt(position.column, 1u);
    CssTokenizerDispose(&tokenizer);
}

static void TestTokenizeStreamPeekIsStable(void)
{
    CssSource source = RoboCssCleared;
    CssTokenizer tokenizer = RoboCssCleared;
    CssToken first = RoboCssCleared;
    CssToken again = RoboCssCleared;
    CssToken consumed = RoboCssCleared;
    CssSource empty = RoboCssCleared;

    CssSourceOfString(&source, "ab");
    RoboCheckEqInt(CssTokenizerInit(&tokenizer, &source, NULL), RoboOk);
    RoboCheckEqInt(CssTokenizerHasPeek(&tokenizer), 0);

    RoboCheckEqInt(CssTokenizerPeek(&tokenizer, &first, NULL), RoboOk);
    RoboCheckEqInt(first.type, RoboCssTokenIdent);
    RoboCheckEqInt(tokenizer.offset, 2u);
    RoboCheckEqInt(CssTokenizerHasPeek(&tokenizer), 1);
    /* Peeking is idempotent and does not advance the cursor past the token. */
    RoboCheckEqInt(CssTokenizerPeek(&tokenizer, &again, NULL), RoboOk);
    RoboCheckEqInt(again.type, first.type);
    RoboCheckEqInt(again.range.begin, first.range.begin);
    RoboCheckEqInt(again.range.end, first.range.end);
    RoboCheckEqInt(CssTokenizerTokensProduced(&tokenizer), 0u);

    RoboCheckEqInt(CssTokenizerNext(&tokenizer, &consumed, NULL), RoboOk);
    RoboCheckEqInt(consumed.type, RoboCssTokenIdent);
    RoboCheckEqInt(consumed.range.end, 2u);
    RoboCheckEqInt(CssTokenizerHasPeek(&tokenizer), 0);
    RoboCheckEqInt(CssTokenizerTokensProduced(&tokenizer), 1u);
    RoboCheckEqInt(CssTokenizerNext(&tokenizer, &consumed, NULL), RoboOk);
    RoboCheckEqInt(consumed.type, RoboCssTokenEof);

    /* A disposed tokenizer peeks nothing and claims to be at the end. */
    CssTokenizerDispose(&tokenizer);
    RoboCheckEqInt(CssTokenizerAtEnd(&tokenizer), 1);
    RoboCheckEqInt(CssTokenizerHasPeek(&tokenizer), 0);
    RoboCheckEqInt(CssTokenizerNext(&tokenizer, &consumed, NULL),
                      RoboInvalidArgument);

    CssSourceOfString(&empty, "x");
    RoboCheckEqInt(CssTokenizerInit(&tokenizer, &source, NULL), RoboOk);
    RoboCheckEqInt(CssTokenizerPeek(&tokenizer, &first, NULL), RoboOk);
    /* Re-initializing onto another source drops the lookahead rather than
     * handing back a token belonging to the previous source. */
    RoboCheckEqInt(CssTokenizerInit(&tokenizer, &empty, NULL), RoboOk);
    RoboCheckEqInt(CssTokenizerHasPeek(&tokenizer), 0);
    RoboCheckEqInt(CssTokenizerPeek(&tokenizer, &again, NULL), RoboOk);
    RoboCheckEqInt(again.type, RoboCssTokenIdent);
    RoboCheckEqInt(again.range.end, 1u);
    CssTokenizerDispose(&tokenizer);
}

static void TestTokenizeStreamSkipConsumes(void)
{
    CssSource source = RoboCssCleared;
    CssTokenizer tokenizer = RoboCssCleared;
    CssToken token = RoboCssCleared;

    CssSourceOfString(&source, "a b");
    RoboCheckEqInt(CssTokenizerInit(&tokenizer, &source, NULL), RoboOk);

    /* skip with nothing peeked produces and discards. */
    RoboCheckEqInt(CssTokenizerSkip(&tokenizer, NULL), RoboOk);
    RoboCheckEqInt(CssTokenizerTokensProduced(&tokenizer), 1u);
    RoboCheckEqInt(tokenizer.offset, 1u);

    /* skip with a peeked token drops the lookahead without re-reading. */
    RoboCheckEqInt(CssTokenizerPeek(&tokenizer, &token, NULL), RoboOk);
    RoboCheckEqInt(token.type, RoboCssTokenWhitespace);
    RoboCheckEqInt(CssTokenizerSkip(&tokenizer, NULL), RoboOk);
    RoboCheckEqInt(CssTokenizerHasPeek(&tokenizer), 0);
    RoboCheckEqInt(CssTokenizerTokensProduced(&tokenizer), 2u);

    RoboCheckEqInt(CssTokenizerNext(&tokenizer, &token, NULL), RoboOk);
    RoboCheckEqInt(token.type, RoboCssTokenIdent);
    /* Skipping the EOF token is legal and leaves the stream at rest. */
    RoboCheckEqInt(CssTokenizerSkip(&tokenizer, NULL), RoboOk);
    RoboCheckEqInt(CssTokenizerAtEnd(&tokenizer), 1);
    RoboCheckEqInt(CssTokenizerSkip(&tokenizer, NULL), RoboOk);
    RoboCheckEqInt(CssTokenizerOffset(&tokenizer), 3u);
    CssTokenizerDispose(&tokenizer);
}

static void TestTokenizeStreamSkipTrivia(void)
{
    CssSource source = RoboCssCleared;
    CssTokenizer tokenizer = RoboCssCleared;
    CssToken token = RoboCssCleared;
    CssSource triviaOnly = RoboCssCleared;

    CssSourceOfString(&source, "  /* c */\n  a /* tail */");
    RoboCheckEqInt(CssTokenizerInit(&tokenizer, &source, NULL), RoboOk);
    RoboCheckEqInt(CssTokenizerSkipTrivia(&tokenizer, NULL), RoboOk);
    RoboCheckEqInt(CssTokenizerPeek(&tokenizer, &token, NULL), RoboOk);
    RoboCheckEqInt(token.type, RoboCssTokenIdent);
    RoboCheckEqInt(token.range.begin, 12u);
    /* The trivia it stepped over is still counted as consumed. */
    RoboCheckEqInt(CssTokenizerTokensProduced(&tokenizer), 3u);

    RoboCheckEqInt(CssTokenizerNext(&tokenizer, &token, NULL), RoboOk);
    RoboCheckEqInt(token.type, RoboCssTokenIdent);
    RoboCheckEqInt(CssTokenizerSkipTrivia(&tokenizer, NULL), RoboOk);
    RoboCheckEqInt(CssTokenizerPeek(&tokenizer, &token, NULL), RoboOk);
    RoboCheckEqInt(token.type, RoboCssTokenEof);
    /* Repeated calls stay at EOF rather than spinning. */
    RoboCheckEqInt(CssTokenizerSkipTrivia(&tokenizer, NULL), RoboOk);
    RoboCheckEqInt(CssTokenizerPeek(&tokenizer, &token, NULL), RoboOk);
    RoboCheckEqInt(token.type, RoboCssTokenEof);
    CssTokenizerDispose(&tokenizer);

    CssSourceOfString(&triviaOnly, "/**/ \t\n");
    RoboCheckEqInt(CssTokenizerInit(&tokenizer, &triviaOnly, NULL), RoboOk);
    RoboCheckEqInt(CssTokenizerSkipTrivia(&tokenizer, NULL), RoboOk);
    RoboCheckEqInt(CssTokenizerPeek(&tokenizer, &token, NULL), RoboOk);
    RoboCheckEqInt(token.type, RoboCssTokenEof);
    RoboCheckEqInt(CssTokenizerAtEnd(&tokenizer), 1);
    CssTokenizerDispose(&tokenizer);
}

static void TestTokenizeStreamResetRelexes(void)
{
    CssSource source = RoboCssCleared;
    CssTokenizer tokenizer = RoboCssCleared;
    CssTokenLog first;
    CssTokenLog second;
    CssToken token = RoboCssCleared;
    char kindsFirst[256];
    char kindsSecond[256];

    CssSourceOfString(&source, "p { color: red; } /* c */");
    CssLex(&source, &first);
    RoboCheckEqInt(first.hasError, 0);

    RoboCheckEqInt(CssTokenizerInit(&tokenizer, &source, NULL), RoboOk);
    while (!CssTokenizerAtEnd(&tokenizer)) {
        RoboCheckEqInt(CssTokenizerNext(&tokenizer, &token, NULL), RoboOk);
    }
    RoboCheck(CssTokenizerTokensProduced(&tokenizer) > 0u);
    RoboCheckEqInt(CssTokenizerReset(&tokenizer, NULL), RoboOk);
    RoboCheckEqInt(CssTokenizerOffset(&tokenizer), 0u);
    RoboCheckEqInt(CssTokenizerTokensProduced(&tokenizer), 0u);
    RoboCheckEqInt(CssTokenizerHasError(&tokenizer), 0);
    RoboCheckEqInt(CssTokenizerHasPeek(&tokenizer), 0);
    RoboCheckEqInt(CssTokenizerAtEnd(&tokenizer), 0);

    RoboCheckEqInt(CssTokenizerNext(&tokenizer, &token, NULL), RoboOk);
    RoboCheckEqInt(token.type, first.tokens[0].type);
    RoboCheckEqInt(token.range.end, first.tokens[0].range.end);

    /* Reset clears the recorded error too, so the same bytes report the same
     * first problem a second time. */
    {
        CssSource malformed = RoboCssCleared;
        CssError error = RoboCssCleared;

        CssSourceOfString(&malformed, "/* unterminated");
        RoboCheckEqInt(CssTokenizerInit(&tokenizer, &malformed, NULL), RoboOk);
        RoboCheckEqInt(CssTokenizerNext(&tokenizer, &token, NULL), RoboOk);
        RoboCheckEqInt(CssTokenizerHasError(&tokenizer), 1);
        RoboCheckEqInt(CssTokenizerTakeError(&tokenizer, &error), 1);
        RoboCheckEqInt(error.kind, RoboCssErrorKindUnexpectedEof);
        RoboCheckEqInt(CssTokenizerHasError(&tokenizer), 0);
        RoboCheckEqInt(CssTokenizerReset(&tokenizer, NULL), RoboOk);
        RoboCheckEqInt(CssTokenizerHasError(&tokenizer), 0);
        RoboCheckEqInt(CssTokenizerNext(&tokenizer, &token, NULL), RoboOk);
        RoboCheckEqInt(CssTokenizerHasError(&tokenizer), 1);
        CssTokenizerDispose(&tokenizer);
    }

    CssLex(&source, &second);
    CssLogKinds(&first, kindsFirst, sizeof(kindsFirst));
    CssLogKinds(&second, kindsSecond, sizeof(kindsSecond));
    RoboCheckStrEq(kindsFirst, kindsSecond);
    RoboCheckEqInt(first.count, second.count);
}

static void TestTokenizeStreamArgumentsRejected(void)
{
    CssSource source = RoboCssCleared;
    CssSource unconfigured = RoboCssCleared;
    CssTokenizer tokenizer = RoboCssCleared;
    CssToken token = RoboCssCleared;
    CssPosition position;

    CssSourceOfString(&source, "a");
    CssSourceInit(&unconfigured);

    RoboCheckEqInt(CssTokenizerInit(NULL, &source, NULL), RoboInvalidArgument);
    RoboCheckEqInt(CssTokenizerInit(&tokenizer, NULL, NULL), RoboInvalidArgument);
    RoboCheckEqInt(CssTokenizerReset(NULL, NULL), RoboInvalidArgument);
    RoboCheckEqInt(CssTokenizerNext(NULL, &token, NULL), RoboInvalidArgument);
    RoboCheckEqInt(CssTokenizerPeek(NULL, &token, NULL), RoboInvalidArgument);
    RoboCheckEqInt(CssTokenizerSkip(NULL, NULL), RoboInvalidArgument);
    RoboCheckEqInt(CssTokenizerSkipTrivia(NULL, NULL), RoboInvalidArgument);
    RoboCheckEqInt(CssTokenizerPosition(NULL, &position, NULL),
                      RoboInvalidArgument);

    /* An EMPTY source is legal (a style sheet may be empty); a source nobody
     * configured is not — there are no bytes to be a cursor over. */
    RoboCheckEqInt(CssTokenizerInit(&tokenizer, &unconfigured, NULL),
                      RoboOk);
    RoboCheckEqInt(CssTokenizerNext(&tokenizer, &token, NULL), RoboOk);
    RoboCheckEqInt(token.type, RoboCssTokenEof);
    CssTokenizerDispose(&tokenizer);

    /* A disposed tokenizer answers INVALID_ARGUMENT rather than reading a NULL
     * source, and dispose is idempotent. */
    RoboCheckEqInt(CssTokenizerInit(&tokenizer, &source, NULL), RoboOk);
    CssTokenizerDispose(&tokenizer);
    CssTokenizerDispose(&tokenizer);
    RoboCheckEqInt(CssTokenizerSource(&tokenizer), NULL);
    RoboCheckEqInt(CssTokenizerNext(&tokenizer, &token, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(CssTokenizerReset(&tokenizer, NULL), RoboInvalidArgument);
    RoboCheckEqInt(CssTokenizerOffset(&tokenizer), 0u);
    RoboCheckEqInt(CssTokenizerRemaining(&tokenizer), 0u);
    RoboCheckEqInt(CssTokenizerTokensProduced(&tokenizer), 0u);
    RoboCheckEqInt(CssTokenizerHasError(&tokenizer), 0);
    RoboCheck(CssTokenizerError(&tokenizer) == NULL);
    RoboCheckEqInt(CssTokenizerTakeError(&tokenizer, NULL), 0);

    /* NULL accessors answer rather than crash. */
    RoboCheckEqInt(CssTokenizerOffset(NULL), 0u);
    RoboCheckEqInt(CssTokenizerRemaining(NULL), 0u);
    RoboCheckEqInt(CssTokenizerAtEnd(NULL), 1);
    RoboCheckEqInt(CssTokenizerTokensProduced(NULL), 0u);
    RoboCheckEqInt(CssTokenizerHasPeek(NULL), 0);
    RoboCheckEqInt(CssTokenizerHasError(NULL), 0);
    RoboCheck(CssTokenizerError(NULL) == NULL);
    RoboCheck(CssTokenizerSource(NULL) == NULL);
    CssTokenizerDispose(NULL);
}

static void TestTokenizeStreamFirstErrorWins(void)
{
    CssSource source = RoboCssCleared;
    CssTokenizer tokenizer = RoboCssCleared;
    CssError first = RoboCssCleared;
    CssError second = RoboCssCleared;
    CssError taken = RoboCssCleared;
    CssPosition position;

    CssSourceOfString(&source, "a");
    RoboCheckEqInt(CssTokenizerInit(&tokenizer, &source, NULL), RoboOk);

    RoboCheckEqInt(CssTokenizerFail(&tokenizer, RoboCssErrorKindNone,
                                              RoboOk, "not a problem"), 0);
    RoboCheckEqInt(CssTokenizerHasError(&tokenizer), 0);

    position = CssPositionAt(0u);
    RoboCheckEqInt(CssErrorSet(&first, RoboCssErrorKindInvalidToken,
                                         RoboInvalidArgument, &position,
                                         "first"),
                      RoboInvalidArgument);
    RoboCheckEqInt(CssErrorSet(&second, RoboCssErrorKindUnexpectedEof,
                                         RoboInvalidArgument, &position,
                                         "second"),
                      RoboInvalidArgument);

    RoboCheckEqInt(CssTokenizerRecordError(&tokenizer, &first), 1);
    RoboCheckEqInt(CssTokenizerRecordError(&tokenizer, &second), 0);
    RoboCheckEqInt(CssTokenizerFail(&tokenizer, RoboCssErrorKindMalformedRule,
                                              RoboInvalidArgument, "third"), 0);
    RoboCheck(CssTokenizerError(&tokenizer) != NULL);
    RoboCheckEqInt(CssTokenizerError(&tokenizer)->kind,
                      RoboCssErrorKindInvalidToken);
    RoboCheckStrEq(CssTokenizerError(&tokenizer)->message, "first");

    RoboCheckEqInt(CssTokenizerTakeError(&tokenizer, &taken), 1);
    RoboCheckStrEq(taken.message, "first");
    RoboCheckEqInt(CssTokenizerHasError(&tokenizer), 0);
    /* Taking the record hands it out; the next problem is then a first problem. */
    RoboCheckEqInt(CssTokenizerRecordError(&tokenizer, &second), 1);
    RoboCheckEqInt(CssTokenizerTakeError(&tokenizer, &taken), 1);
    RoboCheckEqInt(taken.kind, RoboCssErrorKindUnexpectedEof);

    RoboCheckEqInt(CssTokenizerRecordError(NULL, &first), 0);
    RoboCheckEqInt(CssTokenizerRecordError(&tokenizer, NULL), 0);
    RoboCheckEqInt(CssTokenizerFail(NULL, RoboCssErrorKindInvalidToken,
                                              RoboInvalidArgument, "x"), 0);
    CssTokenizerDispose(&tokenizer);
}

static void TestTokenizeStreamFailPositionsAtCursor(void)
{
    CssSource source = RoboCssCleared;
    CssTokenizer tokenizer = RoboCssCleared;
    CssToken token = RoboCssCleared;
    const CssError *error;

    CssSourceOfString(&source, "aa\nbb");
    RoboCheckEqInt(CssTokenizerInit(&tokenizer, &source, NULL), RoboOk);
    RoboCheckEqInt(CssTokenizerNext(&tokenizer, &token, NULL), RoboOk);
    RoboCheckEqInt(CssTokenizerNext(&tokenizer, &token, NULL), RoboOk);
    RoboCheckEqInt(token.type, RoboCssTokenWhitespace);
    /* The cursor is past the newline, on the second line. */
    RoboCheckEqInt(CssTokenizerOffset(&tokenizer), 3u);
    RoboCheckEqInt(CssTokenizerFail(&tokenizer, RoboCssErrorKindMalformedRule,
                                              RoboInvalidArgument, "here"), 1);
    error = CssTokenizerError(&tokenizer);
    RoboCheck(error != NULL);
    RoboCheckEqInt(error->position.offset, 3u);
    RoboCheckEqInt(error->position.line, 2u);
    RoboCheckEqInt(error->position.column, 1u);
    /* The recorded error never stops the stream. */
    RoboCheckEqInt(CssTokenizerNext(&tokenizer, &token, NULL), RoboOk);
    RoboCheckEqInt(token.type, RoboCssTokenIdent);
    CssTokenizerDispose(&tokenizer);
}

static void TestTokenizeStreamCountsAndRemaining(void)
{
    CssSource source = RoboCssCleared;
    CssTokenizer tokenizer = RoboCssCleared;
    CssToken token = RoboCssCleared;
    size_t previousRemaining;
    size_t steps = 0u;

    CssSourceOfString(&source, "a{b:c}");
    RoboCheckEqInt(CssTokenizerInit(&tokenizer, &source, NULL), RoboOk);
    previousRemaining = CssTokenizerRemaining(&tokenizer);
    RoboCheckEqInt(previousRemaining, 6u);

    while (CssTokenizerRemaining(&tokenizer) > 0u) {
        RoboCheckEqInt(CssTokenizerNext(&tokenizer, &token, NULL), RoboOk);
        RoboCheck(CssTokenizerRemaining(&tokenizer) < previousRemaining);
        previousRemaining = CssTokenizerRemaining(&tokenizer);
        steps++;
        if (steps > 32u) {
            break;
        }
    }
    /* Six one-byte tokens — every byte here is one token, which is what makes
     * `remaining` strictly fall on each step — and then EOF, which is produced
     * on the next call rather than by the loop: EOF is not a byte. */
    RoboCheckEqInt(steps, 6u);
    RoboCheckEqInt(CssTokenizerAtEnd(&tokenizer), 1);
    RoboCheckEqInt(CssTokenizerRemaining(&tokenizer), 0u);
    RoboCheckEqInt(CssTokenizerTokensProduced(&tokenizer), 6u);
    RoboCheckEqInt(CssTokenizerNext(&tokenizer, &token, NULL), RoboOk);
    RoboCheckEqInt(token.type, RoboCssTokenEof);
    RoboCheckEqInt(CssTokenizerTokensProduced(&tokenizer), 7u);
    RoboCheckEqInt(CssTokenizerRemaining(&tokenizer), 0u);
    CssTokenizerDispose(&tokenizer);
}

static void TestTokenizeStreamAllocatesNothing(void)
{
    static const char *kSheet =
        "/* a sheet */\n"
        "@media screen and (min-width: 40em) {\n"
        "  .card, article > .title { color: #123456; }\n"
        "}\n"
        "body {\n"
        "  margin: 0 auto; padding: 1.5rem 10px 0 2em;\n"
        "  content: \"a\\\"b\"; background: url(img/logo.png) no-repeat;\n"
        "}\n"
        "p::first-line { font: italic 12pt/1.4 'Caf\\xE9'; }\n";

    CssSource source = RoboCssCleared;
    CssTokenLog log;
    int weEnabled;
    size_t baseline;
    size_t after;

    CssSourceOfString(&source, kSheet);
    weEnabled = CssAllocatorEnsureEnabled();
    baseline = MemDebugLiveCount();
    CssLex(&source, &log);
    after = MemDebugLiveCount();
    CssAllocatorRestore(weEnabled);

    RoboCheck(log.count > 40u);
    RoboCheckEqInt(log.truncated, 0);
    RoboCheckEqInt(log.eofSeen, 1);
    RoboCheckEqInt(log.hasError, 0);
    /* The tokenizer is a value on the stack and owns nothing: a whole sheet
     * lexes with the allocator's live count untouched. */
    RoboCheckEqInt(baseline, after);
}

static void TestTokenizeStreamTokensOutliveTokenizer(void)
{
    CssSource source = RoboCssCleared;
    CssTokenLog log;
    const CssToken *token;
    uint8_t value[8];
    size_t length = 0u;

    CssSourceOfString(&source, "color");
    /* CssLex disposes its tokenizer before returning. */
    CssLex(&source, &log);
    token = CssTokenAt(&log, 0u);
    RoboCheck(token != NULL);
    RoboCheckEqInt(token->type, RoboCssTokenIdent);
    RoboCheck(CssTokenValueEqualsAscii(token, "color"));
    RoboCheckEqInt(CssTokenValueCopy(token, value, sizeof(value), &length, NULL),
                      RoboOk);
    RoboCheckEqInt(length, 5u);
    RoboCheckEqInt(memcmp(value, "color", 5u), 0);

    /* A token is a plain value: copying it copies the whole thing, and the copy
     * is independent of the original. */
    {
        CssToken copy = RoboCssCleared;

        RoboCheckEqInt(CssTokenCopy(token, &copy, NULL), RoboOk);
        RoboCheckEqInt(copy.type, RoboCssTokenIdent);
        RoboCheck(CssTokenValueEqualsAscii(&copy, "color"));
        RoboCheckEqInt(copy.range.begin, token->range.begin);
        RoboCheckEqInt(copy.range.end, token->range.end);
    }
}

static void TestTokenizeStreamReuseAcrossSources(void)
{
    CssSource one = RoboCssCleared;
    CssSource two = RoboCssCleared;
    CssSource copy = RoboCssCleared;
    CssTokenizer tokenizer = RoboCssCleared;
    CssToken token = RoboCssCleared;
    char kindsA[128];
    char kindsB[128];
    char kindsC[128];
    CssTokenLog logA;
    CssTokenLog logB;
    CssTokenLog logC;

    CssSourceOfString(&one, "a:1;");
    CssSourceOfString(&two, "b:2;");

    RoboCheckEqInt(CssTokenizerInit(&tokenizer, &one, NULL), RoboOk);
    RoboCheckEqInt(CssTokenizerNext(&tokenizer, &token, NULL), RoboOk);
    RoboCheckStrEq(CssTokenTypeName(token.type), "ident");
    /* Re-initializing onto a second source is a supported reuse, not a leak. */
    RoboCheckEqInt(CssTokenizerInit(&tokenizer, &two, NULL), RoboOk);
    RoboCheckEqInt(CssTokenizerOffset(&tokenizer), 0u);
    RoboCheckEqInt(CssTokenizerNext(&tokenizer, &token, NULL), RoboOk);
    RoboCheckEqInt(token.text.bytes[0], (uint8_t)'b');
    CssTokenizerDispose(&tokenizer);

    /* The same bytes through an owned copy produce the same token stream. */
    RoboCheckEqInt(CssSourceCopy(&copy, CssSourceBytes(&one),
                                           CssSourceLength(&one), NULL),
                      RoboOk);
    RoboCheckEqInt(CssSourceIsOwned(&copy), 1);

    CssLex(&one, &logA);
    CssLex(&copy, &logB);
    CssLex(&two, &logC);
    CssLogKinds(&logA, kindsA, sizeof(kindsA));
    CssLogKinds(&logB, kindsB, sizeof(kindsB));
    CssLogKinds(&logC, kindsC, sizeof(kindsC));
    RoboCheckStrEq(kindsA, kindsB);
    RoboCheckEqInt(logA.count, logB.count);
    RoboCheckStrEq(kindsA, "ident|colon|number|semicolon|eof");
    RoboCheckStrEq(kindsC, "ident|colon|number|semicolon|eof");
    CSS_CHECK_DOUBLE(logA.tokens[2].number, 1.0);
    CSS_CHECK_DOUBLE(logC.tokens[2].number, 2.0);
    CssSourceDispose(&copy);
}

/**
 * The Phase 9.1 record-lifetime rule, restated for the records Phase 9.2 adds.
 *
 * A by-value record has no constructor to forget to call, which is what makes
 * `RoboCssCleared` the whole of the rule — and it is a rule with teeth: every
 * configuration entry point releases what it holds *before* it writes, so a
 * record that holds nothing is safe and a record that holds garbage is not.
 * What the suite can check is the safe half, and this is it: the cleared state
 * is the all-zero pattern, a used record returns to it, and the disposes are
 * idempotent. This is the property the tokenizing suites rely on when they
 * declare a tokenizer with the macro instead of initialising it.
 */
static void TestTokenizeRecordsCleared(void)
{
    CssTokenizer declared = RoboCssCleared;
    CssTokenizer used = RoboCssCleared;
    CssTokenizer zeroed;
    CssToken token = RoboCssCleared;
    CssSource source = RoboCssCleared;
    CssSource copy = RoboCssCleared;

    memset(&zeroed, 0, sizeof(zeroed));

    /* The macro and the pattern are the same state, which is the claim the
     * macro's documentation makes and the one every "clear this record first"
     * call site depends on. */
    RoboCheckEqInt(memcmp(&declared, &zeroed, sizeof(zeroed)), 0);

    /* A record that has been used comes back to that same state, so a used
     * record and a fresh one are interchangeable as a starting point. */
    CssSourceOfString(&source, "a{}");
    RoboCheckEqInt(CssTokenizerInit(&used, &source, NULL), RoboOk);
    RoboCheckEqInt(CssTokenizerNext(&used, &token, NULL), RoboOk);
    CssTokenizerDispose(&used);
    RoboCheckEqInt(memcmp(&used, &zeroed, sizeof(zeroed)), 0);
    /* Which is what makes a second dispose, and a dispose of something never
     * configured, ordinary no-ops rather than hazards. */
    CssTokenizerDispose(&used);
    CssTokenizerDispose(&declared);
    RoboCheckEqInt(memcmp(&declared, &zeroed, sizeof(zeroed)), 0);

    /* A disposed tokenizer answers for itself rather than reading a source it
     * no longer has. */
    RoboCheckEqInt(CssTokenizerNext(&declared, &token, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(CssTokenizerAtEnd(&declared), 1);

    /* The configuration paths release before they write, so each of them is
     * legal on a record holding nothing — including the one that allocates and
     * therefore has something to release on every call after the first. */
    RoboCheckEqInt(CssSourceCopy(&copy, (const uint8_t *)"a{}", 3u, NULL), RoboOk);
    RoboCheckEqInt(CssSourceIsOwned(&copy), 1);
    RoboCheckEqInt(CssSourceCopy(&copy, (const uint8_t *)"b{}", 3u, NULL), RoboOk);
    RoboCheckEqInt(CssSourceLength(&copy), 3u);
    RoboCheckEqInt(CssSourceSetString(&copy, "c{}", NULL), RoboOk);
    RoboCheckEqInt(CssSourceIsOwned(&copy), 0);
    CssSourceDispose(&copy);
    CssSourceDispose(&copy);
    RoboCheckEqInt(copy.kind, RoboCssSourceKindNone);
}

void TestRegisterCssTokenizer(void);

void TestRegisterCssTokenizer(void)
{
    TestRegister("css.tokenize.stream_empty_input",
                       TestTokenizeStreamEofForEmptyInput);
    TestRegister("css.tokenize.stream_peek_is_stable",
                       TestTokenizeStreamPeekIsStable);
    TestRegister("css.tokenize.stream_skip_consumes",
                       TestTokenizeStreamSkipConsumes);
    TestRegister("css.tokenize.stream_skip_trivia",
                       TestTokenizeStreamSkipTrivia);
    TestRegister("css.tokenize.stream_reset_relexes",
                       TestTokenizeStreamResetRelexes);
    TestRegister("css.tokenize.stream_arguments_rejected",
                       TestTokenizeStreamArgumentsRejected);
    TestRegister("css.tokenize.stream_first_error_wins",
                       TestTokenizeStreamFirstErrorWins);
    TestRegister("css.tokenize.stream_fail_positions_at_cursor",
                       TestTokenizeStreamFailPositionsAtCursor);
    TestRegister("css.tokenize.stream_counts_and_remaining",
                       TestTokenizeStreamCountsAndRemaining);
    TestRegister("css.tokenize.stream_allocates_nothing",
                       TestTokenizeStreamAllocatesNothing);
    TestRegister("css.tokenize.stream_tokens_outlive_tokenizer",
                       TestTokenizeStreamTokensOutliveTokenizer);
    TestRegister("css.tokenize.stream_reuse_across_sources",
                       TestTokenizeStreamReuseAcrossSources);
    TestRegister("css.tokenize.records_cleared", TestTokenizeRecordsCleared);
}
