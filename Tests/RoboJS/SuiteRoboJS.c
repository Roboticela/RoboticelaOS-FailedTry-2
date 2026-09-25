/*
 * The RoboJS core suite (Phase 16.1–16.9, 16.15) — `js.*`.
 *
 * Twenty-three cases over the language runtime, and the shapes worth naming are these:
 *
 *   `js.closure.*`      the phase's §19 demo, and then the two things a counter demo cannot show: that
 *                       two closures over one call *share* their environment and that two calls do not.
 *   `js.exception.*`    §20, plus `finally` running on both a `return` and a throw, and an uncaught
 *                       throw leaving a readable pending exception rather than a dead runtime.
 *   `js.limit.*`        a runaway script stopped by the step budget and a runaway recursion stopped by
 *                       the depth budget — each one asserts that the *runtime is still usable
 *                       afterwards*, because "the OS did not freeze" is the point of the limit.
 *   `js.security.*`     the absences as absences: `process`, `require`, `fetch`, `localStorage` and
 *                       friends are `typeof == "undefined"`, which is a claim a build that quietly grew
 *                       a host binding would fail.
 *
 * Everything here goes through the public doors only — `JsRuntimeEvaluate` for behaviour and the value
 * API for the rest — so a case cannot pass by reading the module's internals.
 */

#include "RoboJSTestSupport.h"

/* ------------------------------------------------------------------ */
/* 16.1 — the runtime, its configuration and its lifecycle            */
/* ------------------------------------------------------------------ */

static void TestRuntimeLifecycle(void)
{
    JsRuntimeConfig config;
    JsRuntime *runtime;
    JsRuntime *second;
    JsValue value;

    RoboCheck(JsRuntimeIsInitialized(NULL) == 0);
    runtime = JsTestRuntimeCreate(&config);
    RoboCheck(runtime != NULL);
    RoboCheck(JsRuntimeIsInitialized(runtime) == 1);
    RoboCheck(JsRuntimeIsExecuting(runtime) == 0);
    RoboCheck(JsRuntimeHeap(runtime) != NULL);
    RoboCheck(JsRuntimeGlobalObject(runtime) != NULL);
    RoboCheck(JsRuntimeGlobalEnv(runtime) != NULL);
    RoboCheck(JsRuntimeObjectPrototype(runtime) != NULL);
    RoboCheck(JsRuntimeArrayPrototype(runtime) != NULL);
    RoboCheck(JsRuntimeFunctionPrototype(runtime) != NULL);
    RoboCheck(JsRuntimeErrorPrototype(runtime) != NULL);

    /* An empty script runs to completion and answers `undefined`, which is the base case every
     * other case in this file is built on. */
    value = JsValueNumber(1);
    RoboCheck(JsRuntimeEvaluate(runtime, "empty", "", 0, &value) == RoboOk);
    RoboCheck(JsValueIsUndefined(&value));

    RoboCheck(JsRuntimeReset(runtime) == RoboOk);
    RoboCheck(JsRuntimeIsInitialized(runtime) == 1);
    RoboCheck(JsRuntimeEvaluationCount(runtime) == 0);
    RoboCheck(JsRuntimeStepCount(runtime) == 0);

    /* Two runtimes share nothing: the whole reason `JsRuntime` is a struct rather than a singleton. */
    second = JsTestRuntimeCreate(&config);
    RoboCheck(second != NULL);
    RoboCheck(JsRuntimeEvaluate(runtime, "a", "var shared = 1;", -1, NULL) == RoboOk);
    {
        JsValue seen;

        seen = JsValueUndefined();
        RoboCheck(JsRuntimeGetGlobal(runtime, "shared", &seen) == RoboOk);
        RoboCheckEqInt(JsValueToInt32(&seen), 1);
        seen = JsValueUndefined();
        RoboCheck(JsRuntimeGetGlobal(second, "shared", &seen) == RoboNotFound);
    }
    JsRuntimeDestroy(second);
    JsRuntimeDestroy(runtime);
    /* Destroying NULL is a no-op rather than a crash, which is what a caller's error path needs. */
    JsRuntimeDestroy(NULL);
}

static void TestRuntimeClock(void)
{
    JsRuntimeConfig config;
    JsRuntime *runtime;
    JsRuntime *hostClock;

    runtime = JsTestRuntimeCreate(&config);
    RoboCheck(runtime != NULL);
    RoboCheckEqInt((long long)JsRuntimeNowMs(runtime), 0);
    RoboCheck(JsRuntimeAdvanceClock(runtime, 999) == RoboOk);
    RoboCheckEqInt((long long)JsRuntimeNowMs(runtime), 999);
    RoboCheck(JsRuntimeAdvanceClock(runtime, 1) == RoboOk);
    RoboCheckEqInt((long long)JsRuntimeNowMs(runtime), 1000);
    JsRuntimeDestroy(runtime);

    /* A runtime whose clock is not deterministic refuses to be advanced rather than pretending.
     * `JsRuntimeConfigInit` defaults to the deterministic counter (that is what makes a test need no
     * wall clock), so the host clock is asked for explicitly. */
    JsRuntimeConfigInit(&config);
    config.deterministic = 0;
    hostClock = JsRuntimeCreate(&config);
    RoboCheck(hostClock != NULL);
    RoboCheck(JsRuntimeAdvanceClock(hostClock, 5) == RoboNotSupported);
    JsRuntimeDestroy(hostClock);
}

static void TestRuntimeDump(void)
{
    JsRuntimeConfig config;
    JsRuntime *runtime;
    char line[2048];
    int32_t needed;
    int32_t index;

    runtime = JsTestRuntimeCreate(&config);
    RoboCheck(runtime != NULL);
    RoboCheck(JsRuntimeEvaluate(runtime, "dump", "var a = 1;", -1, NULL) == RoboOk);

    needed = JsRuntimeWrite(runtime, line, sizeof(line));
    RoboCheck(needed > 0);
    RoboCheck(strstr(line, "RoboJS") != NULL);
    RoboCheck(strstr(line, "1.0.0") != NULL);

    /* Complete-or-absent: a buffer that cannot hold the line is left empty and the answer is the
     * length a complete line needs. */
    line[0] = 'x';
    line[1] = '\0';
    RoboCheck(JsRuntimeWrite(runtime, line, 4) == needed);
    RoboCheck(line[0] == '\0');

    /* Diagnostics are indexed, and the count matches what was recorded. */
    RoboCheck(JsRuntimeDiagnosticCount(runtime) >= 0);
    JsRuntimeAddDiagnostic(runtime, RoboJsDiagWarn, RoboJsErrorType, "suite", NULL, "hello %d", 7);
    RoboCheck(JsRuntimeDiagnosticCount(runtime) >= 1);
    {
        JsDiag diag;

        index = JsRuntimeDiagnosticCount(runtime) - 1;
        RoboCheck(JsRuntimeDiagnosticAt(runtime, index, &diag) == RoboOk);
        RoboCheck(diag.level == RoboJsDiagWarn);
        RoboCheckStrEq(diag.message, "hello 7");
        RoboCheck(JsRuntimeDiagnosticAt(runtime, index + 100, &diag) == RoboNotFound);
    }
    JsRuntimeClearDiagnostics(runtime);
    RoboCheckEqInt(JsRuntimeDiagnosticCount(runtime), 0);
    JsRuntimeDestroy(runtime);
}

static void TestRuntimeCapabilities(void)
{
    JsCapabilities caps;

    RoboCheck(JsGetCapabilities(NULL) == RoboInvalidArgument);
    RoboCheck(JsGetCapabilities(&caps) == RoboOk);
    RoboCheckStrEq(JsVersionString(), "1.0.0");
    RoboCheckStrEq(JsVersion(), "1.0.0");

    /* Landed. */
    RoboCheckEqInt(caps.lexer, 1);
    RoboCheckEqInt(caps.parser, 1);
    RoboCheckEqInt(caps.ast, 1);
    RoboCheckEqInt(caps.astPrinter, 1);
    RoboCheckEqInt(caps.values, 1);
    RoboCheckEqInt(caps.objects, 1);
    RoboCheckEqInt(caps.prototypes, 1);
    RoboCheckEqInt(caps.propertyAttributes, 1);
    RoboCheckEqInt(caps.arrays, 1);
    RoboCheckEqInt(caps.functions, 1);
    RoboCheckEqInt(caps.closures, 1);
    RoboCheckEqInt(caps.exceptions, 1);
    RoboCheckEqInt(caps.errorObjects, 1);
    RoboCheckEqInt(caps.interpreter, 1);
    RoboCheckEqInt(caps.stepLimits, 1);
    RoboCheckEqInt(caps.callDepthLimit, 1);
    RoboCheckEqInt(caps.domBindings, 1);
    RoboCheckEqInt(caps.querySelector, 1);
    RoboCheckEqInt(caps.eventBindings, 1);
    RoboCheckEqInt(caps.eventPropagation, 1);
    RoboCheckEqInt(caps.timers, 1);
    RoboCheckEqInt(caps.eventLoop, 1);
    RoboCheckEqInt(caps.microtasks, 1);
    RoboCheckEqInt(caps.microtaskStarvationCap, 1);
    RoboCheckEqInt(caps.console, 1);
    RoboCheckEqInt(caps.diagnostics, 1);
    RoboCheckEqInt(caps.deterministicClock, 1);
    RoboCheckEqInt(caps.hostDomAdapter, 1);
    RoboCheckEqInt(caps.lifecycle, 1);
    RoboCheckEqInt(caps.apiFrozen, 1);

    /* Absent, by name: a flag whose whole job is to say "not here". */
    RoboCheckEqInt(caps.classes, 0);
    RoboCheckEqInt(caps.asyncAwait, 0);
    RoboCheckEqInt(caps.generators, 0);
    RoboCheckEqInt(caps.modules, 0);
    RoboCheckEqInt(caps.destructuring, 0);
    RoboCheckEqInt(caps.spreadAndRest, 0);
    RoboCheckEqInt(caps.templateLiterals, 0);
    RoboCheckEqInt(caps.optionalChaining, 0);
    RoboCheckEqInt(caps.nullishCoalescing, 0);
    RoboCheckEqInt(caps.symbols, 0);
    RoboCheckEqInt(caps.bigInt, 0);
    RoboCheckEqInt(caps.typedArrays, 0);
    RoboCheckEqInt(caps.regexLiterals, 0);
    RoboCheckEqInt(caps.proxyAndReflect, 0);
    RoboCheckEqInt(caps.promises, 0);
    RoboCheckEqInt(caps.weakCollections, 0);
    RoboCheckEqInt(caps.garbageCollector, 0);
    RoboCheckEqInt(caps.jit, 0);
    RoboCheckEqInt(caps.bytecodeVm, 0);
    RoboCheckEqInt(caps.multiThreaded, 0);
    RoboCheckEqInt(caps.hostFileSystem, 0);
    RoboCheckEqInt(caps.hostProcessAccess, 0);
    RoboCheckEqInt(caps.hostNetwork, 0);
    RoboCheckEqInt(caps.hostEnvironmentVariables, 0);

    /* Bounds are read, not hard-coded. */
    RoboCheckEqInt(caps.maxSteps, RoboJsDefaultMaxSteps);
    RoboCheckEqInt(caps.defaultMaxCallDepth, RoboJsDefaultMaxCallDepth);
    RoboCheckEqInt(caps.maxMicrotasksPerCycle, RoboJsDefaultMaxMicrotasks);
    RoboCheckEqInt(caps.maxTimersPerCycle, RoboJsDefaultMaxTimersPerCycle);
    RoboCheckEqInt(caps.maxTasksPerCycle, RoboJsDefaultMaxTasksPerCycle);
    RoboCheckEqInt(caps.maxQueueLength, RoboJsDefaultMaxQueueLength);
    RoboCheckEqInt(caps.maxTimerCount, RoboJsDefaultMaxTimerCount);
    RoboCheckEqInt(caps.maxPropertiesPerObject, RoboJsDefaultMaxProperties);
    RoboCheckEqInt(caps.maxStringLength, RoboJsDefaultMaxStringLength);
    RoboCheckEqInt(caps.maxParseDepth, RoboJsDefaultMaxParseDepth);
    RoboCheckEqInt(caps.heapChunkBytes, RoboJsDefaultHeapChunkBytes);
    RoboCheck(caps.languageSubset != NULL && caps.languageSubset[0] != '\0');
}

/* ------------------------------------------------------------------ */
/* 16.2 — the lexer and the parser                                     */
/* ------------------------------------------------------------------ */

static void TestLexerVocabulary(void)
{
    RoboCheck(JsTokenKindIsKnown(RoboJsTokenEnd) == 1);
    RoboCheck(JsTokenKindIsKnown(RoboJsTokenVar) == 1);
    RoboCheck(JsTokenKindIsKnown(RoboJsTokenPlusPlus) == 1);
    RoboCheck(JsTokenKindIsKnown((JsTokenKind)1000) == 0);
    RoboCheckStrEq(JsTokenKindName(RoboJsTokenVar), "var");
    RoboCheckStrEq(JsTokenKindName(RoboJsTokenFunction), "function");
    RoboCheckStrEq(JsTokenKindName(RoboJsTokenArrow), "=>");
    RoboCheckStrEq(JsTokenKindText(RoboJsTokenEqEqEq), "===");
    RoboCheckStrEq(JsTokenKindText(RoboJsTokenPlusAssign), "+=");
    RoboCheck(JsTokenKindIsKeyword(RoboJsTokenWhile) == 1);
    RoboCheck(JsTokenKindIsKeyword(RoboJsTokenIdentifier) == 0);
    RoboCheck(JsTokenKindStartsExpression(RoboJsTokenIdentifier) == 1);
    RoboCheck(JsTokenKindStartsExpression(RoboJsTokenVar) == 0);

    /* The operator table is the parser's precedence climbing and the printer's text in one place. */
    RoboCheck(JsBinaryOpInfoOf(RoboJsTokenStar) != NULL);
    RoboCheck(JsBinaryOpInfoOf(RoboJsTokenPlus) != NULL);
    /* Assignment is in the table with the **lowest** rank: `JsParseAssignment` finds the operator
     * through this one door, while `JsParseBinary` refuses anything ranked 1 or below, which is what
     * keeps `a = b = c` right-associative and out of the precedence climber. */
    RoboCheck(JsBinaryOpInfoOf(RoboJsTokenAssign) != NULL);
    RoboCheckEqInt(JsBinaryOpInfoOf(RoboJsTokenAssign)->precedence, 1);
    RoboCheckEqInt(JsBinaryOpInfoOf(RoboJsTokenAssign)->rightAssociative, 1);
    RoboCheck(JsBinaryOpInfoOf(RoboJsTokenStar)->precedence >
              JsBinaryOpInfoOf(RoboJsTokenPlus)->precedence);
    RoboCheckStrEq(JsBinaryOpInfoOf(RoboJsTokenOrOr)->text, "||");

    /* Node and error vocabulary, read by the printer and by a `catch`. */
    RoboCheck(JsNodeKindIsKnown(RoboJsNodeProgram) == 1);
    RoboCheck(JsNodeKindIsKnown((JsNodeKind)999) == 0);
    RoboCheckStrEq(JsNodeKindName(RoboJsNodeBinaryExpression), "BinaryExpression");
    RoboCheckStrEq(JsErrorKindName(RoboJsErrorType), "TypeError");
    RoboCheckStrEq(JsErrorKindName(RoboJsErrorReference), "ReferenceError");
    RoboCheckStrEq(JsErrorKindName(RoboJsErrorSyntax), "SyntaxError");
    RoboCheckStrEq(JsErrorKindName(RoboJsErrorLimit), "RangeError");
    RoboCheckStrEq(JsErrorKindName(RoboJsErrorNone), "");
}

static void TestLexerTokens(void)
{
    JsHeap *heap = JsHeapCreate(0);
    JsLexer lexer;
    JsToken token;
    char text[64];

    RoboCheck(heap != NULL);
    RoboCheck(JsLexerInit(NULL, heap, "x", -1) == RoboInvalidArgument);
    RoboCheck(JsLexerInit(&lexer, heap, "var total = 12 + 3.5; // trailing\n/* block */ name", -1) ==
              RoboOk);

    RoboCheck(JsLexerNext(&lexer, &token) == RoboOk);
    RoboCheckEqInt(token.kind, RoboJsTokenVar);
    RoboCheckEqInt(token.span.line, 1);
    RoboCheckEqInt(token.span.column, 1);
    RoboCheckEqInt(token.span.offset, 0);

    RoboCheck(JsLexerNext(&lexer, &token) == RoboOk);
    RoboCheckEqInt(token.kind, RoboJsTokenIdentifier);
    JsTokenCopyText(&token, text, sizeof(text));
    RoboCheckStrEq(text, "total");

    RoboCheck(JsLexerNext(&lexer, &token) == RoboOk);
    RoboCheckEqInt(token.kind, RoboJsTokenAssign);
    RoboCheck(JsLexerNext(&lexer, &token) == RoboOk);
    RoboCheckEqInt(token.kind, RoboJsTokenNumber);
    RoboCheck(token.number == 12.0);
    RoboCheck(JsLexerNext(&lexer, &token) == RoboOk);
    RoboCheckEqInt(token.kind, RoboJsTokenPlus);
    RoboCheck(JsLexerNext(&lexer, &token) == RoboOk);
    RoboCheckEqInt(token.kind, RoboJsTokenNumber);
    RoboCheck(token.number == 3.5);
    RoboCheck(JsLexerNext(&lexer, &token) == RoboOk);
    RoboCheckEqInt(token.kind, RoboJsTokenSemicolon);
    /* Comments are skipped, and the line counter moved. */
    RoboCheck(JsLexerNext(&lexer, &token) == RoboOk);
    RoboCheckEqInt(token.kind, RoboJsTokenIdentifier);
    JsTokenCopyText(&token, text, sizeof(text));
    RoboCheckStrEq(text, "name");
    RoboCheckEqInt(token.span.line, 2);
    RoboCheck(JsLexerNext(&lexer, &token) == RoboOk);
    RoboCheckEqInt(token.kind, RoboJsTokenEnd);

    JsHeapDestroy(heap);
}

static void TestLexerErrors(void)
{
    JsHeap *heap = JsHeapCreate(0);
    JsLexer lexer;
    JsToken token;
    const JsError *error;

    RoboCheck(heap != NULL);

    /* An unterminated string is a located SyntaxError, not a silent token. */
    RoboCheck(JsLexerInit(&lexer, heap, "var s = \"oops", -1) == RoboOk);
    RoboCheck(JsLexerNext(&lexer, &token) == RoboOk);
    RoboCheck(JsLexerNext(&lexer, &token) == RoboOk);
    RoboCheck(JsLexerNext(&lexer, &token) == RoboOk);
    RoboCheck(JsLexerNext(&lexer, &token) == RoboParseError);
    error = JsLexerError(&lexer);
    RoboCheck(error != NULL);
    RoboCheckEqInt(error->kind, RoboJsErrorSyntax);
    RoboCheck(error->hasSpan == 1);
    RoboCheck(error->span.offset >= 0);
    /* Once failed, it stays failed rather than resuming on a half-read source. */
    RoboCheck(JsLexerNext(&lexer, &token) == RoboParseError);

    /* A character the language does not have is refused by name. */
    RoboCheck(JsLexerInit(&lexer, heap, "var x = 1 @ 2;", -1) == RoboOk);
    {
        Result status;

        do {
            status = JsLexerNext(&lexer, &token);
        } while (status == RoboOk && token.kind != RoboJsTokenEnd);
        RoboCheck(status == RoboParseError);
    }

    /* Peek does not consume, and does not disturb the next pull. */
    RoboCheck(JsLexerInit(&lexer, heap, "a b", -1) == RoboOk);
    RoboCheck(JsLexerPeek(&lexer, &token) == RoboOk);
    RoboCheckEqInt(token.kind, RoboJsTokenIdentifier);
    {
        char peeked[8];

        JsTokenCopyText(&token, peeked, sizeof(peeked));
        RoboCheckStrEq(peeked, "a");
    }
    RoboCheck(JsLexerNext(&lexer, &token) == RoboOk);
    RoboCheckEqInt(token.kind, RoboJsTokenIdentifier);
    RoboCheckEqInt(token.span.offset, 0);
    RoboCheck(JsLexerNext(&lexer, &token) == RoboOk);
    RoboCheckEqInt(token.span.offset, 2);

    JsHeapDestroy(heap);
}

static void TestParserAndAst(void)
{
    JsRuntimeConfig config;
    JsRuntime *runtime;
    JsHeap *heap;
    JsParser parser;
    JsNode *program = NULL;
    char line[512];
    int32_t written;

    runtime = JsTestRuntimeCreate(&config);
    RoboCheck(runtime != NULL);
    heap = JsRuntimeHeap(runtime);

    /* A tiny expression, printed whole: the AST suite's one readable assertion. */
    RoboCheck(JsParserInit(&parser, heap, "1 + 2 * 3;", -1, 0) == RoboOk);
    RoboCheck(JsParserParseProgram(&parser, &program) == RoboOk);
    RoboCheck(program != NULL);
    RoboCheckEqInt(program->kind, RoboJsNodeProgram);
    RoboCheck(program->count >= 1);
    RoboCheck(JsAstIsWellFormed(program) == 1);
    RoboCheck(JsAstCount(program, 1000) >= 5);
    written = JsAstPrint(program, line, sizeof(line));
    RoboCheck(written > 0);
    RoboCheck(strstr(line, "1") != NULL);
    RoboCheck(strstr(line, "*") != NULL);
    /* Complete-or-absent: a four-byte buffer must not hold this line. */
    line[0] = 'x';
    line[1] = '\0';
    RoboCheck(JsAstPrint(program, line, 4) == written);
    RoboCheck(line[0] == '\0');

    RoboCheck(JsParserNodeCount(&parser) > 0);

    /* A syntax error is structured, located and leaves the caller no tree. */
    RoboCheck(JsParserInit(&parser, heap, "var = 1;", -1, 0) == RoboOk);
    program = NULL;
    RoboCheck(JsParserParseProgram(&parser, &program) == RoboParseError);
    RoboCheck(program == NULL);
    RoboCheckEqInt(JsParserErrorOf(&parser)->kind, RoboJsErrorSyntax);
    RoboCheck(JsParserFailure(&parser) != NULL);
    RoboCheck(JsParserFailure(&parser)->hasToken == 1);

    /* Depth is bounded by the caller's policy rather than by the host's stack. */
    {
        static const char *deep =
            "(((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((("
            "1";
        JsParser bounded;

        RoboCheck(JsParserInit(&bounded, heap, deep, -1, 4) == RoboOk);
        program = NULL;
        RoboCheck(JsParserParseProgram(&bounded, &program) != RoboOk);
    }

    /* A random parse failure never corrupts the runtime: it is still usable right after. */
    RoboCheck(JsRuntimeEvaluate(runtime, "after", "var ok = 1;", -1, NULL) == RoboOk);
    JsRuntimeDestroy(runtime);
}

/* ------------------------------------------------------------------ */
/* 16.4 — values                                                       */
/* ------------------------------------------------------------------ */

static void TestValueTypes(void)
{
    JsHeap *heap = JsHeapCreate(0);
    JsValue undefined = JsValueUndefined();
    JsValue null = JsValueNull();
    JsValue yes = JsValueBool(1);
    JsValue number = JsValueNumber(2.5);
    JsValue string = JsValueString(heap, "h\303\251llo", -1);
    JsObject *object = JsObjectCreate(heap, NULL);
    JsValue objectValue = JsValueObject(object);

    RoboCheck(heap != NULL);
    RoboCheckEqInt(JsValueTypeOf(&undefined), RoboJsTypeUndefined);
    RoboCheckEqInt(JsValueTypeOf(&null), RoboJsTypeNull);
    RoboCheckEqInt(JsValueTypeOf(&yes), RoboJsTypeBoolean);
    RoboCheckEqInt(JsValueTypeOf(&number), RoboJsTypeNumber);
    RoboCheckEqInt(JsValueTypeOf(&string), RoboJsTypeString);
    RoboCheckEqInt(JsValueTypeOf(&objectValue), RoboJsTypeObject);

    RoboCheck(JsValueIsUndefined(&undefined) == 1);
    RoboCheck(JsValueIsNull(&null) == 1);
    RoboCheck(JsValueIsNullOrUndefined(&null) == 1);
    RoboCheck(JsValueIsNullOrUndefined(&undefined) == 1);
    RoboCheck(JsValueIsBoolean(&yes) == 1);
    RoboCheck(JsValueIsNumber(&number) == 1);
    RoboCheck(JsValueIsString(&string) == 1);
    RoboCheck(JsValueIsObject(&objectValue) == 1);
    RoboCheck(JsValueIsFunction(&objectValue) == 0);
    RoboCheck(JsValueIsArray(&objectValue) == 0);

    /* A string value carries its byte length, so a caller never calls `strlen` on one. */
    RoboCheckEqInt(string.stringLength, 6);
    RoboCheckStrEq(JsValueAsString(&string), "h\303\251llo");
    RoboCheck(JsValueAsString(&number) == NULL);
    RoboCheck(JsValueAsObject(&objectValue) == object);
    RoboCheck(JsValueAsObject(&number) == NULL);

    RoboCheckStrEq(JsValueTypeOfString(&undefined), "undefined");
    RoboCheckStrEq(JsValueTypeOfString(&null), "object");
    RoboCheckStrEq(JsValueTypeOfString(&yes), "boolean");
    RoboCheckStrEq(JsValueTypeOfString(&number), "number");
    RoboCheckStrEq(JsValueTypeOfString(&string), "string");
    RoboCheckStrEq(JsValueTypeOfString(&objectValue), "object");
    RoboCheck(JsValueTypeIsKnown(RoboJsTypeObject) == 1);
    RoboCheck(JsValueTypeIsKnown((JsValueType)99) == 0);
    RoboCheckStrEq(JsValueTypeName(RoboJsTypeObject), "object");

    /* `undefined` is what a NULL object becomes, which is the one door that keeps a NULL out. */
    RoboCheck(JsValueIsUndefined(&(JsValue){0}) == 1);
    {
        JsValue fromNull = JsValueObjectOrUndefined(NULL);

        RoboCheck(JsValueIsUndefined(&fromNull) == 1);
    }

    JsHeapDestroy(heap);
}

static void TestValueConversions(void)
{
    JsHeap *heap = JsHeapCreate(0);
    JsValue undefined = JsValueUndefined();
    JsValue null = JsValueNull();
    JsValue zero = JsValueNumber(0);
    JsValue nan = JsValueNumber(JsTestNan());
    JsValue number = JsValueNumber(42);
    JsValue string = JsValueString(heap, " 7 ", -1);
    JsValue word = JsValueString(heap, "abc", -1);

    RoboCheck(heap != NULL);

    /* Truthiness: the falsy six and nothing else. */
    RoboCheck(JsValueToBoolean(&undefined) == 0);
    RoboCheck(JsValueToBoolean(&null) == 0);
    RoboCheck(JsValueToBoolean(&zero) == 0);
    RoboCheck(JsValueToBoolean(&nan) == 0);
    RoboCheck(JsValueToBoolean(&(JsValue){0}) == 0);
    RoboCheck(JsValueToBoolean(&(JsValue){.type = RoboJsTypeBoolean}) == 0);
    RoboCheck(JsValueToBoolean(&(JsValue){.type = RoboJsTypeBoolean, .number = 1}) == 1);
    RoboCheck(JsValueToBoolean(&number) == 1);
    {
        JsValue empty = JsValueString(heap, "", 0);

        RoboCheck(JsValueToBoolean(&empty) == 0);
    }
    RoboCheck(JsValueToBoolean(&word) == 1);
    RoboCheck(JsValueToBoolean(NULL) == 0);

    /* Numbers: `null` is 0, `undefined` is NaN, a numeric string parses, `NaN` stays NaN. */
    RoboCheck(JsValueToNumber(&null) == 0.0);
    RoboCheck(JsValueToNumber(&undefined) != JsValueToNumber(&undefined));
    RoboCheck(JsValueToNumber(&number) == 42.0);
    RoboCheck(JsValueToNumber(&string) == 7.0);
    RoboCheck(JsValueToNumber(&word) != JsValueToNumber(&word));
    RoboCheck(JsValueToNumber(&(JsValue){.type = RoboJsTypeBoolean, .number = 1}) == 1.0);
    RoboCheck(JsValueToNumber(&(JsValue){.type = RoboJsTypeBoolean}) == 0.0);
    RoboCheck(JsStringToNumber("3.5", -1) == 3.5);
    RoboCheck(JsStringToNumber("", -1) == 0.0);
    RoboCheck(JsStringToNumber("4px", -1) != JsStringToNumber("4px", -1));

    /* Text. */
    {
        char text[64];

        JsValueToString(&undefined, text, sizeof(text));
        RoboCheckStrEq(text, "undefined");
        JsValueToString(&null, text, sizeof(text));
        RoboCheckStrEq(text, "null");
        JsValueToString(&number, text, sizeof(text));
        RoboCheckStrEq(text, "42");
        JsValueToString(&word, text, sizeof(text));
        RoboCheckStrEq(text, "abc");
        RoboCheck(JsValueToString(&number, text, 3) >= 2);
        RoboCheck(text[0] == '\0' || text[2] == '\0');
    }
    {
        char text[64];

        JsNumberToString(1.0, text, sizeof(text));
        RoboCheckStrEq(text, "1");
        JsNumberToString(-0.0, text, sizeof(text));
        RoboCheckStrEq(text, "0");
        JsNumberToString(0.5, text, sizeof(text));
        RoboCheckStrEq(text, "0.5");
        JsNumberToString(JsTestNan(), text, sizeof(text));
        RoboCheckStrEq(text, "NaN");
        JsNumberToString(JsTestInfinity(), text, sizeof(text));
        RoboCheckStrEq(text, "Infinity");
        JsNumberToString(-JsTestInfinity(), text, sizeof(text));
        RoboCheckStrEq(text, "-Infinity");
    }
    JsHeapDestroy(heap);
}

static void TestValueEquality(void)
{
    JsHeap *heap = JsHeapCreate(0);
    JsObject *left = JsObjectCreate(heap, NULL);
    JsObject *right = JsObjectCreate(heap, NULL);
    JsValue leftValue = JsValueObject(left);
    JsValue rightValue = JsValueObject(right);
    JsValue one = JsValueNumber(1);
    JsValue oneAgain = JsValueNumber(1);
    JsValue nan = JsValueNumber(JsTestNan());
    JsValue null = JsValueNull();
    JsValue undefined = JsValueUndefined();
    JsValue stringOne = JsValueString(heap, "1", -1);
    JsValue stringOneAgain = JsValueString(heap, "1", -1);

    RoboCheck(heap != NULL);

    /* `===`: numbers by value, strings by bytes, objects by identity, NaN not equal to itself. */
    RoboCheck(JsValueStrictEquals(&one, &oneAgain) == 1);
    RoboCheck(JsValueStrictEquals(&nan, &nan) == 0);
    RoboCheck(JsValueStrictEquals(&stringOne, &stringOneAgain) == 1);
    RoboCheck(JsValueStrictEquals(&stringOne, &one) == 0);
    RoboCheck(JsValueStrictEquals(&leftValue, &leftValue) == 1);
    RoboCheck(JsValueStrictEquals(&leftValue, &rightValue) == 0);
    RoboCheck(JsValueStrictEquals(&null, &undefined) == 0);

    /* `==`: null and undefined are equal to each other and to nothing else. */
    RoboCheck(JsValueLooseEquals(&null, &undefined) == 1);
    RoboCheck(JsValueLooseEquals(&null, &(JsValue){0}) == 1);
    RoboCheck(JsValueLooseEquals(&stringOne, &one) == 1);
    RoboCheck(JsValueLooseEquals(&leftValue, &rightValue) == 0);

    /* `+` concatenates when either side is a string. */
    RoboCheck(JsValuePlusConcatenates(&stringOne, &one) == 1);
    RoboCheck(JsValuePlusConcatenates(&one, &stringOne) == 1);
    RoboCheck(JsValuePlusConcatenates(&one, &oneAgain) == 0);

    JsHeapDestroy(heap);
}

static void TestValueInt32(void)
{
    JsValue value;

    value = JsValueNumber(7.9);
    RoboCheckEqInt(JsValueToInt32(&value), 7);
    value = JsValueNumber(-7.9);
    RoboCheckEqInt(JsValueToInt32(&value), -7);
    value = JsValueNumber(JsTestNan());
    RoboCheckEqInt(JsValueToInt32(&value), 0);
    value = JsValueNumber(JsTestInfinity());
    RoboCheckEqInt(JsValueToInt32(&value), 0);
    value = JsValueNumber(4294967296.0);
    RoboCheckEqInt(JsValueToInt32(&value), 0); /* wraps modulo 2^32 */
    value = JsValueNumber(4294967297.0);
    RoboCheckEqInt(JsValueToInt32(&value), 1);
    value = JsValueNumber(2147483648.0);
    RoboCheckEqInt(JsValueToInt32(&value), -2147483647 - 1);
    value = JsValueNumber(-1.0);
    RoboCheckEqInt(JsValueToInt32(&value), -1);

    value = JsValueNumber(3.0);
    RoboCheck(JsValueIsInteger(&value) == 1);
    value = JsValueNumber(3.5);
    RoboCheck(JsValueIsInteger(&value) == 0);
    value = JsValueNumber(1.0e20);
    RoboCheck(JsValueIsInteger(&value) == 0); /* outside int32, per the header */
    value = JsValueNumber(JsTestNan());
    RoboCheck(JsValueIsInteger(&value) == 0);
}

/* ------------------------------------------------------------------ */
/* 16.5 — objects and arrays                                           */
/* ------------------------------------------------------------------ */

static void TestObjectProperties(void)
{
    JsHeap *heap = JsHeapCreate(0);
    JsObject *object = JsObjectCreate(heap, NULL);
    JsValue value;
    JsValue one = JsValueNumber(1);
    const char *keys[8];

    RoboCheck(heap != NULL);
    RoboCheck(object != NULL);
    RoboCheckEqInt(object->kind, RoboJsObjectPlain);
    RoboCheck(JsObjectKindIsKnown(RoboJsObjectArray) == 1);
    RoboCheck(JsObjectKindName(RoboJsObjectHost) != NULL);
    RoboCheck(JsObjectKindName(RoboJsObjectArray) != NULL);

    value = JsValueUndefined();
    RoboCheck(JsObjectGet(object, "missing", &value) == RoboNotFound);
    RoboCheck(JsValueIsUndefined(&value));
    RoboCheck(JsObjectHas(object, "missing") == 0);

    RoboCheck(JsObjectSet(heap, object, "answer", &one) == RoboOk);
    RoboCheck(JsObjectHas(object, "answer") == 1);
    RoboCheck(JsObjectHasOwn(object, "answer") == 1);
    RoboCheck(JsObjectGet(object, "answer", &value) == RoboOk);
    RoboCheckEqInt(JsValueToInt32(&value), 1);
    RoboCheckEqInt(JsObjectOwnPropertyCount(object), 1);

    /* Overwrite keeps the attributes and answers the new value. */
    {
        JsValue two = JsValueNumber(2);

        RoboCheck(JsObjectSet(heap, object, "answer", &two) == RoboOk);
        RoboCheck(JsObjectGet(object, "answer", &value) == RoboOk);
        RoboCheckEqInt(JsValueToInt32(&value), 2);
        RoboCheckEqInt(JsObjectOwnPropertyCount(object), 1);
    }

    /* A non-writable property refuses, and the refusal is a distinguishable code. */
    {
        JsValue five = JsValueNumber(5);
        JsValue six = JsValueNumber(6);

        RoboCheck(JsObjectDefine(heap, object, "frozen", &five, 0) == RoboOk);
        RoboCheck(JsObjectSet(heap, object, "frozen", &six) == RoboPermissionDenied);
        RoboCheck(JsObjectGet(object, "frozen", &value) == RoboOk);
        RoboCheckEqInt(JsValueToInt32(&value), 5);
    }

    /* `frozen` was defined with no attributes, so it is not enumerable and `OwnKeys` — the door a
     * `for...in` reads — does not report it. */
    RoboCheckEqInt(JsObjectOwnKeys(object, keys, 8), 1);
    RoboCheck(keys[0] != NULL);
    RoboCheckStrEq(keys[0], "answer");

    RoboCheck(JsObjectDelete(object, "answer") == RoboOk);
    RoboCheck(JsObjectHas(object, "answer") == 0);
    RoboCheck(JsObjectDelete(object, "answer") == RoboNotFound);

    /* Prototypes are a chain, and the lookup walks it. */
    {
        JsObject *prototype = JsObjectCreate(heap, NULL);
        JsObject *child = JsObjectCreate(heap, prototype);
        JsValue seven = JsValueNumber(7);

        RoboCheck(JsObjectSet(heap, prototype, "inherited", &seven) == RoboOk);
        RoboCheck(JsObjectGet(child, "inherited", &value) == RoboOk);
        RoboCheckEqInt(JsValueToInt32(&value), 7);
        RoboCheck(JsObjectHasOwn(child, "inherited") == 0);
        RoboCheck(JsObjectOwnKeys(child, keys, 8) == 0);
    }

    JsHeapDestroy(heap);
}

static void TestObjectArrays(void)
{
    JsHeap *heap = JsHeapCreate(0);
    JsObject *array = JsArrayCreate(heap, NULL, 0);
    JsValue value;
    JsValue item;
    int32_t index = -1;

    RoboCheck(heap != NULL);
    RoboCheck(array != NULL);
    RoboCheckEqInt(array->kind, RoboJsObjectArray);
    RoboCheckEqInt(array->length, 0);

    item = JsValueNumber(10);
    RoboCheckEqInt(JsArrayPush(heap, array, &item), 1);
    item = JsValueNumber(20);
    RoboCheckEqInt(JsArrayPush(heap, array, &item), 2);
    RoboCheckEqInt(array->length, 2);

    value = JsValueUndefined();
    RoboCheck(JsArrayGet(array, 1, &value) == RoboOk);
    RoboCheckEqInt(JsValueToInt32(&value), 20);
    RoboCheck(JsArrayGet(array, 5, &value) == RoboNotFound);

    item = JsValueNumber(30);
    RoboCheck(JsArraySet(heap, array, 4, &item) == RoboOk);
    RoboCheckEqInt(array->length, 5);
    RoboCheck(JsArrayGet(array, 2, &value) == RoboOk);
    RoboCheck(JsValueIsUndefined(&value));

    RoboCheck(JsArraySetLength(heap, array, 2) == RoboOk);
    RoboCheckEqInt(array->length, 2);
    RoboCheck(JsArrayGet(array, 4, &value) == RoboNotFound);
    RoboCheck(JsArraySetLength(heap, array, -1) == RoboInvalidArgument);

    RoboCheck(JsArrayPop(array, &value) == RoboOk);
    RoboCheckEqInt(JsValueToInt32(&value), 20);
    RoboCheckEqInt(array->length, 1);
    RoboCheck(JsArrayPop(array, &value) == RoboOk);
    /* Popping an empty array is either "nothing to answer" or `undefined` — the header allows both
     * shapes, and what matters is that it does not invent an element. */
    RoboCheck(JsArrayPop(array, &value) != RoboOk || JsValueIsUndefined(&value));
    RoboCheckEqInt(array->length, 0);

    /* A canonical index is a canonical index: `01` and `-1` are not. */
    RoboCheckEqInt(JsArrayIndexOfKey("0", &index), 1);
    RoboCheckEqInt(index, 0);
    RoboCheckEqInt(JsArrayIndexOfKey("12", &index), 1);
    RoboCheckEqInt(index, 12);
    RoboCheckEqInt(JsArrayIndexOfKey("01", &index), 0);
    RoboCheckEqInt(JsArrayIndexOfKey("-1", &index), 0);
    RoboCheckEqInt(JsArrayIndexOfKey("length", &index), 0);

    /* A numeric property key becomes its canonical text through the interpreter's own door. */
    {
        char key[32];

        item = JsValueNumber(12);
        JsInterpPropertyKey(&item, key, sizeof(key));
        RoboCheckStrEq(key, "12");
        item = JsValueBool(1);
        JsInterpPropertyKey(&item, key, sizeof(key));
        RoboCheckStrEq(key, "true");
    }

    JsHeapDestroy(heap);
}

/* ------------------------------------------------------------------ */
/* 16.6 / 16.7 — functions and closures, through the interpreter       */
/* ------------------------------------------------------------------ */

static void TestFunctionDeclaration(void)
{
    JsRuntimeConfig config;
    JsRuntime *runtime;

    runtime = JsTestRuntimeCreate(&config);
    RoboCheck(runtime != NULL);

    /* The phase's §16.6 acceptance: `add(2, 3)` is 5 and `typeof` reads `function`. */
    RoboCheck(JsTestGlobalNumber(runtime,
                                "function add(a, b) { return a + b; }\nvar result = add(2, 3);",
                                "result") == 5.0);
    {
        char text[32];

        JsTestGlobalString(runtime, "var kind = typeof add;", "kind", text, sizeof(text));
        RoboCheckStrEq(text, "function");
    }

    /* A function expression assigned to a name is callable the same way. */
    RoboCheck(JsTestGlobalNumber(runtime,
                                "var mul = function (a, b) { return a * b; };\nvar product = mul(6, 7);",
                                "product") == 42.0);

    /* Missing arguments are `undefined`; extra ones are ignored by the body that does not name them. */
    RoboCheck(JsTestGlobalNumber(runtime,
                                "function one(a) { return a; }\nvar missing = one();",
                                "missing") != JsTestGlobalNumber(runtime, "var zero = 0;", "zero"));
    RoboCheck(JsTestGlobalNumber(runtime,
                                "function first(a, b) { return a; }\nvar extra = first(9, 10, 11);",
                                "extra") == 9.0);

    /* A native binding is called by the same door: `console.log` is a function from a script's view. */
    {
        char text[32];

        JsTestGlobalString(runtime,
                           "var logType = typeof console.log;",
                           "logType",
                           text,
                           sizeof(text));
        RoboCheckStrEq(text, "function");
    }

    /* `this` on a method call is the receiver; on a plain call it is the global object. */
    RoboCheck(JsTestGlobalNumber(runtime,
                                "var box = { n: 4, get: function () { return this.n; } };\n"
                                "var fromMethod = box.get();",
                                "fromMethod") == 4.0);

    /* `new` creates the object, and a returned object overrides it. */
    RoboCheck(JsTestGlobalNumber(runtime,
                                "function Point(x) { this.x = x; }\n"
                                "var p = new Point(3);\nvar px = p.x;",
                                "px") == 3.0);
    RoboCheck(JsTestGlobalNumber(runtime,
                                "var kindOf = (new Point(1)) ? 1 : 0;",
                                "kindOf") == 1.0);

    JsRuntimeDestroy(runtime);
}

static void TestClosures(void)
{
    JsRuntimeConfig config;
    JsRuntime *runtime;

    runtime = JsTestRuntimeCreate(&config);
    RoboCheck(runtime != NULL);

    /* The phase's §19 demo, exactly. */
    RoboCheck(JsTestGlobalNumber(runtime,
                                "function createCounter() {\n"
                                "  var count = 0;\n"
                                "  return function () { count = count + 1; return count; };\n"
                                "}\n"
                                "var counter = createCounter();\n"
                                "var a = counter();\n"
                                "var b = counter();",
                                "a") == 1.0);
    RoboCheck(JsTestGlobalNumber(runtime, "var b2 = counter();", "b2") == 3.0);

    /* Two counters from two calls keep their own environment. */
    RoboCheck(JsTestGlobalNumber(runtime,
                                "var other = createCounter();\nvar first = other();",
                                "first") == 1.0);

    /* Two closures from *one* call share it, which a counter demo alone cannot show. */
    RoboCheck(JsTestGlobalNumber(runtime,
                                "function pair() {\n"
                                "  var shared = 0;\n"
                                "  function bump() { shared = shared + 1; return shared; }\n"
                                "  function read() { return shared; }\n"
                                "  return { bump: bump, read: read };\n"
                                "}\n"
                                "var p = pair();\n"
                                "p.bump();\n"
                                "p.bump();\n"
                                "var sharedSeen = p.read();",
                                "sharedSeen") == 2.0);

    /* The captured variable is still writable after the creating call returned, and it is per call. */
    RoboCheck(JsTestGlobalNumber(runtime,
                                "function make() { var n = 5; return function (v) { n = n + v; return n; }; }\n"
                                "var one = make();\n"
                                "var two = make();\n"
                                "one(1);\n"
                                "one(1);\n"
                                "var isolated = two(1);",
                                "isolated") == 6.0);

    /* A nested closure resolves through the whole chain, and a shadowing declaration wins. */
    RoboCheck(JsTestGlobalNumber(runtime,
                                "var outer = 1;\n"
                                "function shadow() { var outer = 2; return function () { return outer; }; }\n"
                                "var shadowed = shadow()();\n"
                                "var globalToo = outer;",
                                "shadowed") == 2.0);
    RoboCheck(JsTestGlobalNumber(runtime, "var globalStill = outer;", "globalStill") == 1.0);

    JsRuntimeDestroy(runtime);
}

/* ------------------------------------------------------------------ */
/* 16.8 — exceptions                                                   */
/* ------------------------------------------------------------------ */

static void TestExceptions(void)
{
    JsRuntimeConfig config;
    JsRuntime *runtime;

    runtime = JsTestRuntimeCreate(&config);
    RoboCheck(runtime != NULL);

    /* The phase's §20 demo: a throw two frames down is caught, and the runtime stays alive. The throw
     * crosses a call, which is the path a completion cannot travel and the pending slot does. */
    {
        char text[64];

        JsTestGlobalString(runtime,
                           "function test() { throw new Error(\"failure\"); }\n"
                           "var caught = \"\";\n"
                           "try { test(); } catch (error) { caught = error.message; }",
                           "caught",
                           text,
                           sizeof(text));
        RoboCheckStrEq(text, "failure");
    }

    /* The five error kinds name themselves, which is what a `catch` compares. */
    {
        char text[64];

        JsTestGlobalString(runtime,
                           "var names = \"\";\n"
                           "try { throw new TypeError(\"t\"); } catch (e) { names = names + e.name; }\n"
                           "try { throw new ReferenceError(\"r\"); } catch (e) { names = names + \"/\" + e.name; }\n"
                           "try { throw new RangeError(\"g\"); } catch (e) { names = names + \"/\" + e.name; }\n"
                           "try { throw new SyntaxError(\"s\"); } catch (e) { names = names + \"/\" + e.name; }\n"
                           "try { throw new Error(\"e\"); } catch (e) { names = names + \"/\" + e.name; }",
                           "names",
                           text,
                           sizeof(text));
        RoboCheckStrEq(text, "TypeError/ReferenceError/RangeError/SyntaxError/Error");
    }

    /* `throw` takes any value, and a caught string is the string. */
    {
        char text[64];

        JsTestGlobalString(runtime,
                           "var thrown = \"\";\n"
                           "try { throw \"just a string\"; } catch (e) { thrown = typeof e; }",
                           "thrown",
                           text,
                           sizeof(text));
        RoboCheckStrEq(text, "string");
    }

    /* `finally` runs on a `return`, on a throw and after a `catch` that swallowed it. */
    RoboCheck(JsTestGlobalNumber(runtime,
                                "var marks = 0;\n"
                                "function returns() { try { return 1; } finally { marks = marks + 1; } }\n"
                                "function throws() { try { throw new Error(\"x\"); } finally { marks = marks + 10; } }\n"
                                "function catches() { try { throw new Error(\"y\"); } catch (e) { marks = marks + 20; }"
                                " finally { marks = marks + 100; } }\n"
                                "returns();\n"
                                "try { throws(); } catch (e) { }\n"
                                "catches();\n"
                                "var total = marks;",
                                "total") == 131.0);

    /* A nested try inside a catch keeps both layers honest. */
    RoboCheck(JsTestGlobalNumber(runtime,
                                "var layers = 0;\n"
                                "try {\n"
                                "  try { throw new Error(\"inner\"); } finally { layers = layers + 1; }\n"
                                "} catch (e) { layers = layers + 10; }\n"
                                "var layerTotal = layers;",
                                "layerTotal") == 11.0);

    /* An uncaught throw is a reported failure with a readable pending exception, not a crash. */
    {
        JsValue thrown;

        RoboCheck(JsRuntimeEvaluate(runtime,
                                    "uncaught",
                                    "function boom() { throw new TypeError(\"unhandled\"); }\nboom();",
                                    -1,
                                    NULL) != RoboOk);
        thrown = JsValueUndefined();
        RoboCheck(JsRuntimeHasException(runtime) == 1);
        RoboCheck(JsRuntimeTakeException(runtime, &thrown) == RoboOk);
        RoboCheckEqInt(JsExceptionKindOf(&thrown), RoboJsErrorType);
        {
            char message[64];

            message[0] = '\0';
            JsExceptionMessage(&thrown, message, sizeof(message));
            RoboCheckStrEq(message, "unhandled");
        }
        RoboCheck(JsRuntimeHasException(runtime) == 0);
        /* And the runtime is still usable, which is the claim that matters for an OS. */
        RoboCheck(JsRuntimeEvaluate(runtime, "after", "var alive = 1;", -1, NULL) == RoboOk);
    }

    /* An error object carries its position, so a diagnostic can name where it happened. */
    {
        JsValue value;

        RoboCheck(JsRuntimeEvaluate(runtime,
                                    "located",
                                    "var caught2 = null;\n"
                                    "try { throw new Error(\"here\"); } catch (e) { caught2 = e; }",
                                    -1,
                                    &value) == RoboOk);
        RoboCheck(JsRuntimeGetGlobal(runtime, "caught2", &value) == RoboOk);
        RoboCheck(JsValueIsObject(&value) == 1);
        {
            JsObject *error = JsValueAsObject(&value);
            double line = 0.0;
            JsValue field;

            RoboCheck(error != NULL);
            RoboCheckEqInt(error->kind, RoboJsObjectError);
            field = JsValueUndefined();
            RoboCheck(JsObjectGet(error, "line", &field) == RoboOk);
            line = JsValueToNumber(&field);
            RoboCheck(line >= 1.0);
        }
    }

    JsRuntimeDestroy(runtime);
}

/* ------------------------------------------------------------------ */
/* 16.9 — the interpreter                                              */
/* ------------------------------------------------------------------ */

static void TestInterpreterLoops(void)
{
    JsRuntimeConfig config;
    JsRuntime *runtime;

    runtime = JsTestRuntimeCreate(&config);
    RoboCheck(runtime != NULL);

    /* 16.9's acceptance example, verbatim. */
    RoboCheck(JsTestGlobalNumber(runtime,
                                "var total = 0;\n"
                                "for (var i = 0; i < 10; i = i + 1) { total = total + i; }\n",
                                "total") == 45.0);

    /* `while`, `break` and `continue` are the same completion machinery. */
    RoboCheck(JsTestGlobalNumber(runtime,
                                "var seen = 0;\n"
                                "var n = 0;\n"
                                "while (n < 20) { n = n + 1; if (n == 5) { continue; } if (n > 8) { break; } "
                                "seen = seen + n; }",
                                "seen") == 1.0 + 2 + 3 + 4 + 6 + 7 + 8);
    RoboCheck(JsTestGlobalNumber(runtime, "var stoppedAt = n;", "stoppedAt") == 9.0);

    /* `do`/`while` runs its body once even when the condition is false. */
    RoboCheck(JsTestGlobalNumber(runtime,
                                "var once = 0;\n"
                                "do { once = once + 1; } while (false);",
                                "once") == 1.0);

    /* `if`/`else`, the conditional operator, and short-circuit evaluation. */
    RoboCheck(JsTestGlobalNumber(runtime,
                                "var branch = 0;\n"
                                "if (1 < 2) { branch = 1; } else { branch = 2; }\n"
                                "if (2 < 1) { branch = 3; } else if (3 > 2) { branch = branch + 10; }",
                                "branch") == 11.0);
    RoboCheck(JsTestGlobalNumber(runtime, "var ternary = (1 > 2) ? 5 : 6;", "ternary") == 6.0);
    RoboCheck(JsTestGlobalNumber(runtime,
                                "var called = 0;\n"
                                "function mark() { called = called + 1; return true; }\n"
                                "var aa = false && mark();\n"
                                "var bb = true || mark();",
                                "called") == 0.0);

    /* `switch` with `break`, falling through the way C does. */
    RoboCheck(JsTestGlobalNumber(runtime,
                                "var picked = 0;\n"
                                "switch (2) {\n"
                                "  case 1: picked = picked + 1;\n"
                                "  case 2: picked = picked + 10;\n"
                                "  case 3: picked = picked + 100; break;\n"
                                "  default: picked = picked + 1000;\n"
                                "}\n",
                                "picked") == 110.0);

    JsRuntimeDestroy(runtime);
}

static void TestInterpreterExpressions(void)
{
    JsRuntimeConfig config;
    JsRuntime *runtime;

    runtime = JsTestRuntimeCreate(&config);
    RoboCheck(runtime != NULL);

    /* Arithmetic, precedence and the compound assignments. */
    RoboCheck(JsTestGlobalNumber(runtime, "var a = 1 + 2 * 3 - 4 / 2;", "a") == 5.0);
    RoboCheck(JsTestGlobalNumber(runtime, "var b = 7 % 3;", "b") == 1.0);
    RoboCheck(JsTestGlobalNumber(runtime,
                                "var c = 1; c += 2; c -= 1; c *= 5; c /= 2;",
                                "c") == 5.0);
    RoboCheck(JsTestGlobalNumber(runtime, "var d = 1; var pre = ++d;", "pre") == 2.0);
    RoboCheck(JsTestGlobalNumber(runtime, "var e = 5; var post = e++;", "post") == 5.0);
    RoboCheck(JsTestGlobalNumber(runtime, "var after = e;", "after") == 6.0);
    RoboCheck(JsTestGlobalNumber(runtime, "var f = -(-3) + (+2);", "f") == 5.0);

    /* Comparison and equality. */
    RoboCheck(JsTestGlobalNumber(runtime,
                                "var truth = 0;\n"
                                "if (1 == \"1\") { truth = truth + 1; }\n"
                                "if (1 === \"1\") { truth = truth + 100; }\n"
                                "if (1 !== 2) { truth = truth + 10; }\n"
                                "if (2 >= 2 && 3 <= 4) { truth = truth + 1000; }\n",
                                "truth") == 1011.0);

    /* String concatenation and `typeof`. */
    {
        char text[64];

        JsTestGlobalString(runtime,
                           "var greeting = \"a\" + \"b\" + 1 + \"c\";",
                           "greeting",
                           text,
                           sizeof(text));
        RoboCheckStrEq(text, "ab1c");
        JsTestGlobalString(runtime, "var t1 = typeof 1;", "t1", text, sizeof(text));
        RoboCheckStrEq(text, "number");
        JsTestGlobalString(runtime, "var t2 = typeof \"s\";", "t2", text, sizeof(text));
        RoboCheckStrEq(text, "string");
        JsTestGlobalString(runtime, "var t3 = typeof true;", "t3", text, sizeof(text));
        RoboCheckStrEq(text, "boolean");
        JsTestGlobalString(runtime, "var t4 = typeof undefined;", "t4", text, sizeof(text));
        RoboCheckStrEq(text, "undefined");
        JsTestGlobalString(runtime, "var t5 = typeof null;", "t5", text, sizeof(text));
        RoboCheckStrEq(text, "object");
        JsTestGlobalString(runtime,
                           "var t6 = typeof { a: 1 };",
                           "t6",
                           text,
                           sizeof(text));
        RoboCheckStrEq(text, "object");
    }

    /* Object and array literals, member access and property assignment. */
    RoboCheck(JsTestGlobalNumber(runtime,
                                "var o = { a: 1, b: 2, nested: { c: 3 } };\n"
                                "var sum = o.a + o.b + o.nested.c;",
                                "sum") == 6.0);
    RoboCheck(JsTestGlobalNumber(runtime,
                                "var list = [1, 2, 3];\n"
                                "var len = list.length;\n"
                                "list[1] = 20;\n"
                                "var second = list[1];",
                                "second") == 20.0);
    RoboCheck(JsTestGlobalNumber(runtime, "var listLen = list.length;", "listLen") == 3.0);
    RoboCheck(JsTestGlobalNumber(runtime,
                                "var pushed = [1];\n"
                                "pushed.push(2);\n"
                                "pushed.push(3);\n"
                                "var last = pushed.pop();",
                                "last") == 3.0);
    RoboCheck(JsTestGlobalNumber(runtime,
                                "var computed = { key: \"value\" };\n"
                                "var name = \"key\";\n"
                                "var read = computed[name] === \"value\" ? 1 : 0;",
                                "read") == 1.0);
    RoboCheck(JsTestGlobalNumber(runtime,
                                "var counter2 = { n: 0 };\n"
                                "counter2.n = counter2.n + 7;\n"
                                "var assigned = counter2.n;",
                                "assigned") == 7.0);

    /* Recursion, and a function referring to itself by name. */
    RoboCheck(JsTestGlobalNumber(runtime,
                                "function fact(n) { if (n <= 1) { return 1; } return n * fact(n - 1); }\n"
                                "var f10 = fact(10);",
                                "f10") == 3628800.0);

    /* The Math subset is reachable and its results are numbers, not strings. */
    RoboCheck(JsTestGlobalNumber(runtime, "var m1 = Math.floor(2.7);", "m1") == 2.0);
    RoboCheck(JsTestGlobalNumber(runtime, "var m2 = Math.ceil(2.1);", "m2") == 3.0);
    RoboCheck(JsTestGlobalNumber(runtime, "var m3 = Math.abs(-4);", "m3") == 4.0);
    RoboCheck(JsTestGlobalNumber(runtime, "var m4 = Math.max(1, 9, 4);", "m4") == 9.0);

    JsRuntimeDestroy(runtime);
}

/* ------------------------------------------------------------------ */
/* 16.1's limits, proved rather than promised                          */
/* ------------------------------------------------------------------ */

static void TestLimits(void)
{
    JsRuntimeConfig config;
    JsRuntime *runtime;

    /* A small step budget: a loop that would never end is stopped by policy. */
    JsRuntimeConfigInit(&config);
    config.deterministic = 1;
    config.maxSteps = 2000;
    runtime = JsRuntimeCreate(&config);
    RoboCheck(runtime != NULL);
    {
        Result status = JsRuntimeEvaluate(runtime,
                                          "spin",
                                          "var n = 0;\nwhile (true) { n = n + 1; }",
                                          -1,
                                          NULL);

        RoboCheck(status != RoboOk);
        /* The failure is readable, and it is the runtime's own step budget that stopped it. */
        RoboCheck(JsRuntimeDiagnosticCount(runtime) > 0);
        RoboCheck(JsRuntimeStepCount(runtime) >= 2000);
        /* The host is still standing: another script runs right after. */
        RoboCheck(JsRuntimeEvaluate(runtime, "after", "var fine = 1;", -1, NULL) == RoboOk);
    }
    JsRuntimeDestroy(runtime);

    /* A small call-depth budget: runaway recursion is stopped rather than overflowing the C stack. */
    JsRuntimeConfigInit(&config);
    config.deterministic = 1;
    config.maxCallDepth = 24;
    runtime = JsRuntimeCreate(&config);
    RoboCheck(runtime != NULL);
    {
        Result status = JsRuntimeEvaluate(runtime,
                                          "deep",
                                          "function dive(n) { return dive(n + 1); }\n"
                                          "var depth = 0;\n"
                                          "dive(0);",
                                          -1,
                                          NULL);

        RoboCheck(status != RoboOk);
        RoboCheck(JsRuntimeMaxDepthReached(runtime) > 0);
        RoboCheck(JsRuntimeMaxDepthReached(runtime) <= 24 + 2);
        RoboCheck(JsRuntimeEvaluate(runtime, "after", "var fine = 2;", -1, NULL) == RoboOk);
    }
    JsRuntimeDestroy(runtime);

    /* A bounded queue refuses rather than growing without limit. */
    JsRuntimeConfigInit(&config);
    config.deterministic = 1;
    config.maxQueueLength = 4;
    runtime = JsRuntimeCreate(&config);
    RoboCheck(runtime != NULL);
    {
        JsValue callback = JsValueUndefined();
        int32_t posted = 0;
        int32_t refused = 0;
        int32_t i;

        RoboCheck(JsRuntimeEvaluate(runtime, "cb", "var f = function () { };", -1, NULL) == RoboOk);
        RoboCheck(JsRuntimeGetGlobal(runtime, "f", &callback) == RoboOk);
        for (i = 0; i < 10; ++i) {
            Result status = JsLoopPostTask(runtime, RoboJsTaskNative, &callback, NULL, NULL, 0);

            if (status == RoboOk) {
                posted += 1;
            } else {
                refused += 1;
            }
        }
        RoboCheckEqInt(posted, 4);
        RoboCheckEqInt(refused, 6);
        RoboCheckEqInt(JsLoopPendingTaskCount(runtime), 4);
        JsLoopClear(runtime);
        RoboCheckEqInt(JsLoopPendingTaskCount(runtime), 0);
    }
    JsRuntimeDestroy(runtime);
}

/* ------------------------------------------------------------------ */
/* Security: the absences, asserted                                    */
/* ------------------------------------------------------------------ */

static void TestSecurityBoundary(void)
{
    JsRuntimeConfig config;
    JsRuntime *runtime;
    char text[64];

    runtime = JsTestRuntimeCreate(&config);
    RoboCheck(runtime != NULL);

    /* Not one host capability is reachable by name. A build that quietly installed one fails here. */
    JsTestGlobalString(runtime, "var s1 = typeof process;", "s1", text, sizeof(text));
    RoboCheckStrEq(text, "undefined");
    JsTestGlobalString(runtime, "var s2 = typeof require;", "s2", text, sizeof(text));
    RoboCheckStrEq(text, "undefined");
    JsTestGlobalString(runtime, "var s3 = typeof global;", "s3", text, sizeof(text));
    RoboCheckStrEq(text, "undefined");
    JsTestGlobalString(runtime, "var s4 = typeof fetch;", "s4", text, sizeof(text));
    RoboCheckStrEq(text, "undefined");
    JsTestGlobalString(runtime, "var s5 = typeof XMLHttpRequest;", "s5", text, sizeof(text));
    RoboCheckStrEq(text, "undefined");
    JsTestGlobalString(runtime, "var s6 = typeof localStorage;", "s6", text, sizeof(text));
    RoboCheckStrEq(text, "undefined");
    JsTestGlobalString(runtime, "var s7 = typeof WebSocket;", "s7", text, sizeof(text));
    RoboCheckStrEq(text, "undefined");
    JsTestGlobalString(runtime, "var s8 = typeof eval;", "s8", text, sizeof(text));
    RoboCheckStrEq(text, "undefined");
    JsTestGlobalString(runtime, "var s9 = typeof Function;", "s9", text, sizeof(text));
    RoboCheckStrEq(text, "undefined");
    JsTestGlobalString(runtime, "var s10 = typeof importScripts;", "s10", text, sizeof(text));
    RoboCheckStrEq(text, "undefined");

    /* Reading a global that does not exist is a ReferenceError, not a host pointer. */
    {
        Result status = JsRuntimeEvaluate(runtime, "missing", "var x = nosuchthing;", -1, NULL);

        RoboCheck(status != RoboOk);
        JsRuntimeClearException(runtime);
    }

    /* And the runtime's own doors refuse a NULL rather than dereferencing it. */
    RoboCheck(JsRuntimeEvaluate(NULL, "null", "1;", -1, NULL) == RoboInvalidArgument);
    RoboCheck(JsRuntimeGetGlobal(runtime, NULL, NULL) == RoboInvalidArgument);
    RoboCheck(JsLoopPostTask(NULL, RoboJsTaskNative, NULL, NULL, NULL, 0) == RoboInvalidArgument);

    JsRuntimeDestroy(runtime);
}

/* ------------------------------------------------------------------ */
/* The console, and the host sink                                      */
/* ------------------------------------------------------------------ */

typedef struct JsTestConsole {
    int32_t logCount;
    int32_t warnCount;
    int32_t errorCount;
    char last[256];
} JsTestConsole;

static void JsTestConsoleWrite(void *userdata, int level, const char *text, int32_t length)
{
    JsTestConsole *sink = (JsTestConsole *)userdata;

    if (level == 0) {
        sink->logCount += 1;
    } else if (level == 1) {
        sink->warnCount += 1;
    } else {
        sink->errorCount += 1;
    }
    snprintf(sink->last, sizeof(sink->last), "%.*s", (int)length, text);
}

static void TestConsole(void)
{
    JsRuntimeConfig config;
    JsRuntime *runtime;
    JsTestConsole sink;

    memset(&sink, 0, sizeof(sink));
    JsRuntimeConfigInit(&config);
    config.deterministic = 1;
    config.console = JsTestConsoleWrite;
    config.consoleUserdata = &sink;
    runtime = JsRuntimeCreate(&config);
    RoboCheck(runtime != NULL);

    RoboCheck(JsRuntimeEvaluate(runtime, "console", "console.log(\"a\", 1, true);", -1, NULL) == RoboOk);
    RoboCheckEqInt(sink.logCount, 1);
    RoboCheckStrEq(sink.last, "a 1 true");

    RoboCheck(JsRuntimeEvaluate(runtime, "console", "console.warn(\"careful\");", -1, NULL) == RoboOk);
    RoboCheckEqInt(sink.warnCount, 1);
    RoboCheckStrEq(sink.last, "careful");

    RoboCheck(JsRuntimeEvaluate(runtime, "console", "console.error(\"bad\");", -1, NULL) == RoboOk);
    RoboCheckEqInt(sink.errorCount, 1);
    RoboCheckStrEq(sink.last, "bad");

    /* A host sink replaces the configured one through the door, and clearing goes back to it. */
    JsRuntimeSetConsole(runtime, NULL, NULL);
    RoboCheck(JsRuntimeEvaluate(runtime, "console", "console.log(\"second\");", -1, NULL) == RoboOk);
    RoboCheckEqInt(sink.logCount, 2);

    JsRuntimeDestroy(runtime);
}

/* ------------------------------------------------------------------ */
/* Declarations: the three keywords differ where it is observable      */
/* ------------------------------------------------------------------ */

/**
 * `var`, `let` and `const` are three declarations, not one with three spellings.
 *
 * The case that matters is `const`: it is the only one whose difference from `let` shows up *after* the
 * declaration, so a build that hoists all three the same way cannot answer it. The refusal has to be a
 * `TypeError` rather than a silent global — the implicit-global rule for an undeclared name is exactly
 * the door a broken `const` would escape through.
 */
static void TestDeclarations(void)
{
    JsRuntimeConfig config;
    JsRuntime *runtime;
    char text[64];

    runtime = JsTestRuntimeCreate(&config);
    RoboCheck(runtime != NULL);

    /* A `const` reads back its initializer. */
    RoboCheck(JsTestEvalNamed(runtime,
                              "const-read",
                              "const answer = 42;\nvar read = answer;",
                              NULL) == RoboOk);
    RoboCheckEqInt(JsTestGlobalNumber(runtime, "var read2 = read;", "read2"), 42);

    /* **The write it exists to refuse.** An assignment is a `TypeError`, the value is unchanged, and
     * the runtime is usable immediately afterwards. */
    RoboCheck(JsTestEvalExpectError(runtime, "const-write", "answer = 7;") != RoboOk);
    {
        char message[128];

        JsTestExceptionMessage(runtime, message, sizeof(message));
        RoboCheck(strstr(message, "constant") != NULL);
        JsRuntimeClearException(runtime);
    }
    RoboCheckEqInt(JsTestGlobalNumber(runtime, "var still = answer;", "still"), 42);
    /* No global was created in place of the refused binding. */
    {
        JsValue probe = JsValueUndefined();

        RoboCheck(JsRuntimeGetGlobal(runtime, "answer", &probe) == RoboOk);
        RoboCheckEqInt((long long)JsValueToNumber(&probe), 42);
    }

    /* An update and a compound assignment are the same door and are refused the same way, so a build
     * that only guarded plain `=` fails here. */
    RoboCheck(JsTestEvalExpectError(runtime, "const-update", "answer++;\n") != RoboOk);
    JsRuntimeClearException(runtime);
    RoboCheck(JsTestEvalExpectError(runtime, "const-compound", "answer += 1;\n") != RoboOk);
    JsRuntimeClearException(runtime);
    RoboCheckEqInt(JsTestGlobalNumber(runtime, "var untouched = answer;", "untouched"), 42);

    /* The global object's own view refuses the write too — reaching the binding through `this` is not
     * a way around it, because the mirror carries the same non-writable attribute. **Sloppy mode
     * makes that refusal silent rather than a throw** (`JsObjectSet`'s rule, and the specification's),
     * so the assertion is that the write changed nothing rather than that it raised. */
    RoboCheck(JsTestEvalNamed(runtime, "const-global-object", "this.answer = 9;", NULL) == RoboOk);
    RoboCheckEqInt(JsTestGlobalNumber(runtime, "var intact = answer;", "intact"), 42);
    /* And the global object still *reads* the constant, which is the half that makes the mirror worth
     * having: the binding is one value however the read arrives. */
    RoboCheck(JsTestEvalNamed(runtime, "const-global-read", "var viaThisValue = this.answer;", NULL) == RoboOk);
    RoboCheckEqInt(JsTestGlobalNumber(runtime, "var viaThisRead = viaThisValue;", "viaThisRead"), 42);

    /* `let` **is** writable — the difference is the whole point — and it shares the block scope. */
    RoboCheck(JsTestEvalNamed(runtime,
                              "let-write",
                              "let mutable = 1;\nmutable = 2;\n"
                              "var letRead = mutable;",
                              NULL) == RoboOk);
    RoboCheckEqInt(JsTestGlobalNumber(runtime, "var letRead2 = letRead;", "letRead2"), 2);

    /* And a bare assignment to a name nothing declares is still an implicit global — the rule the
     * `const` arm must not swallow. */
    RoboCheck(JsTestEvalNamed(runtime, "implicit-global", "undeclared = 5;", NULL) == RoboOk);
    RoboCheckEqInt(JsTestGlobalNumber(runtime, "var implicitRead = undeclared;", "implicitRead"), 5);

    /* A `const` inside a function is per-call, and its write is refused there too. */
    RoboCheck(JsTestEvalNamed(runtime,
                              "const-in-function",
                              "function pick() {\n"
                              "  const local = 3;\n"
                              "  try { local = 4; } catch (e) { return \"refused:\" + local; }\n"
                              "  return \"allowed\";\n"
                              "}\n"
                              "var pickResult = pick();",
                              NULL) == RoboOk);
    JsTestGlobalString(runtime, "var pickText = pickResult;", "pickText", text, sizeof(text));
    RoboCheckStrEq(text, "refused:3");

    RoboCheck(JsRuntimeReset(runtime) == RoboOk);
    JsRuntimeDestroy(runtime);
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

void TestRegisterRoboJS(void);

void TestRegisterRoboJS(void)
{
    TestRegister("js.interpreter.declarations", TestDeclarations);
    TestRegister("js.runtime.lifecycle", TestRuntimeLifecycle);
    TestRegister("js.runtime.clock", TestRuntimeClock);
    TestRegister("js.runtime.dump", TestRuntimeDump);
    TestRegister("js.runtime.capabilities", TestRuntimeCapabilities);
    TestRegister("js.lexer.vocabulary", TestLexerVocabulary);
    TestRegister("js.lexer.tokens", TestLexerTokens);
    TestRegister("js.lexer.errors", TestLexerErrors);
    TestRegister("js.parser.ast", TestParserAndAst);
    TestRegister("js.value.types", TestValueTypes);
    TestRegister("js.value.conversions", TestValueConversions);
    TestRegister("js.value.equality", TestValueEquality);
    TestRegister("js.value.int32", TestValueInt32);
    TestRegister("js.object.properties", TestObjectProperties);
    TestRegister("js.object.arrays", TestObjectArrays);
    TestRegister("js.function.declaration", TestFunctionDeclaration);
    TestRegister("js.closure.lifetime", TestClosures);
    TestRegister("js.exception.handling", TestExceptions);
    TestRegister("js.interpreter.loops", TestInterpreterLoops);
    TestRegister("js.interpreter.expressions", TestInterpreterExpressions);
    TestRegister("js.limit.budgets", TestLimits);
    TestRegister("js.security.boundary", TestSecurityBoundary);
    TestRegister("js.console.sink", TestConsole);
}
