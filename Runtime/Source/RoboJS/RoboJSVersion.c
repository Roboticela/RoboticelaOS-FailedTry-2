/*
 * RoboJS's version and capability table — see `RoboJS/RoboJSVersion.h` for the contract.
 *
 * ## Why the counts are derived rather than written down
 *
 * `tokenKindCount`, `nodeKindCount` and `errorKindCount` are counted by asking the vocabulary doors
 * themselves (`JsTokenKindIsKnown` and friends) over a bounded range. A number typed into this file
 * would be a second statement of the enum that a later phase's new token silently falsifies; a number
 * *asked* of the enum cannot drift. The bound is 256, which is above every kind space in this build
 * and is itself asserted by the conformance suite: a table that outgrew it would fail there rather
 * than here, with a message that says what happened.
 *
 * ## The absences are as load-bearing as the flags
 *
 * `garbageCollector`, `hostProcessAccess`, `hostFileSystem`, `hostNetwork` and the language features
 * this build does not lex are all `0` on purpose, and the suite reads each one. A flag whose whole job
 * is to say "not here" is worth more than a flag nobody reads — a consumer can refuse to build a path
 * on it, and a phase that quietly grows the capability fails a test rather than a review.
 */

#include "RoboJS/RoboJSVersion.h"

#include "RoboJS/RoboJS.h"

#include <string.h>

/** The range the vocabulary counts are taken over. Above every kind space in this build. */
#define JsVersionKindProbeLimit 256

const char *JsVersionString(void)
{
    return "1.0.0";
}

Result JsGetVersion(I32 *outMajor, I32 *outMinor, I32 *outPatch)
{
    if (outMajor == NULL || outMinor == NULL || outPatch == NULL) {
        return RoboInvalidArgument;
    }
    *outMajor = RoboJsVersionMajor;
    *outMinor = RoboJsVersionMinor;
    *outPatch = RoboJsVersionPatch;
    return RoboOk;
}

const char *JsVersion(void)
{
    return JsVersionString();
}

Result JsVersionComponents(I32 *outMajor, I32 *outMinor, I32 *outPatch)
{
    return JsGetVersion(outMajor, outMinor, outPatch);
}

static unsigned JsVersionCountTokenKinds(void)
{
    unsigned count = 0;
    int32_t kind;

    for (kind = 0; kind < JsVersionKindProbeLimit; ++kind) {
        if (JsTokenKindIsKnown((JsTokenKind)kind)) {
            count += 1u;
        }
    }
    return count;
}

static unsigned JsVersionCountNodeKinds(void)
{
    unsigned count = 0;
    int32_t kind;

    for (kind = 0; kind < JsVersionKindProbeLimit; ++kind) {
        if (JsNodeKindIsKnown((JsNodeKind)kind)) {
            count += 1u;
        }
    }
    return count;
}

static unsigned JsVersionCountErrorKinds(void)
{
    unsigned count = 0;
    int32_t kind;

    for (kind = 0; kind < JsVersionKindProbeLimit; ++kind) {
        if (JsErrorKindIsKnown((JsErrorKind)kind)) {
            count += 1u;
        }
    }
    return count;
}

Result JsGetCapabilities(JsCapabilities *out)
{
    if (out == NULL) {
        return RoboInvalidArgument;
    }
    memset(out, 0, sizeof(*out));

    /* ---- Landed: 16.1–16.16, each read field by field in `SuiteRoboJS.c`. ---- */
    out->lexer = 1;
    out->parser = 1;
    out->ast = 1;
    out->astPrinter = 1;
    out->values = 1;
    out->objects = 1;
    out->prototypes = 1;
    out->propertyAttributes = 1;
    out->arrays = 1;
    out->functions = 1;
    out->closures = 1;
    out->exceptions = 1;
    out->errorObjects = 1;
    out->interpreter = 1;
    out->stepLimits = 1;
    out->callDepthLimit = 1;
    out->domBindings = 1;
    out->querySelector = 1;
    out->eventBindings = 1;
    out->eventPropagation = 1;
    out->timers = 1;
    out->eventLoop = 1;
    out->microtasks = 1;
    out->microtaskStarvationCap = 1;
    out->console = 1;
    out->diagnostics = 1;
    out->deterministicClock = 1;
    out->hostDomAdapter = 1;
    out->lifecycle = 1;
    /* The public surface is pinned by this phase: the headers in `RoboJS/` are the contract, and the
     * umbrella names every one of them (`text.api.header_hygiene`'s RoboJS counterpart is the suite's
     * own header count). */
    out->apiFrozen = 1;

    /*
     * ---- Deliberately absent ----
     *
     * Every one of these stays 0 and is asserted as 0. They are named here rather than left to
     * `memset` so that a reader of this file sees the list, and so a later phase that adds one has to
     * delete a line rather than notice a default.
     */
    out->classes = 0;
    out->asyncAwait = 0;
    out->generators = 0;
    out->modules = 0;
    out->destructuring = 0;
    out->spreadAndRest = 0;
    out->templateLiterals = 0;
    out->optionalChaining = 0;
    out->nullishCoalescing = 0;
    out->symbols = 0;
    out->bigInt = 0;
    out->typedArrays = 0;
    out->regexLiterals = 0;
    out->proxyAndReflect = 0;
    out->promises = 0;
    out->weakCollections = 0;
    out->garbageCollector = 0;
    out->jit = 0;
    out->bytecodeVm = 0;
    out->multiThreaded = 0;
    out->hostFileSystem = 0;
    out->hostProcessAccess = 0;
    out->hostNetwork = 0;
    out->hostEnvironmentVariables = 0;

    /* ---- Bounds: the same constants the allocators read, so a test reads a limit rather than
     *      hard-coding one a later phase might move. ---- */
    out->maxSteps = (unsigned)RoboJsDefaultMaxSteps;
    out->defaultMaxCallDepth = (unsigned)RoboJsDefaultMaxCallDepth;
    out->maxMicrotasksPerCycle = (unsigned)RoboJsDefaultMaxMicrotasks;
    out->maxTimersPerCycle = (unsigned)RoboJsDefaultMaxTimersPerCycle;
    out->maxTasksPerCycle = (unsigned)RoboJsDefaultMaxTasksPerCycle;
    out->maxQueueLength = (unsigned)RoboJsDefaultMaxQueueLength;
    out->maxTimerCount = (unsigned)RoboJsDefaultMaxTimerCount;
    out->maxPropertiesPerObject = (unsigned)RoboJsDefaultMaxProperties;
    out->maxStringLength = (unsigned)RoboJsDefaultMaxStringLength;
    out->maxParseDepth = (unsigned)RoboJsDefaultMaxParseDepth;
    out->heapChunkBytes = (unsigned)RoboJsDefaultHeapChunkBytes;
    out->tokenKindCount = JsVersionCountTokenKinds();
    out->nodeKindCount = JsVersionCountNodeKinds();
    out->errorKindCount = JsVersionCountErrorKinds();
    out->languageSubset =
        "var/let/const, function declarations and expressions, arrow functions, objects, arrays, "
        "arithmetic/comparison/logical/unary/assignment operators, if/else, while, do/while, for, "
        "switch, break/continue, return, throw, try/catch/finally, new, this, typeof, member access, "
        "calls, comments, and automatic semicolon insertion";
    return RoboOk;
}
