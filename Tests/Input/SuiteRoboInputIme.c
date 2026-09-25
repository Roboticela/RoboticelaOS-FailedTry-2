/*
 * RoboInput input-method suite (Phase 13.37--13.39) — `input.ime.*`.
 *
 * Eight cases. Two of them are the phase's whole point, and they are the two where this layer has an opinion
 * rather than a translation:
 *
 *   `surrounding`   a **password** field never asks its editor, even when the editor declares it can answer
 *                   and would happily do so. The suite proves it by counting the calls the client received:
 *                   zero. A rule kept by refusing to ask cannot be undone by a cooperative client.
 *
 *   `candidate`     a candidate window with no caret to anchor to is **refused** rather than placed at the
 *                   origin, because a window in the top-left corner is worse than being told there is
 *                   nothing to anchor to.
 *
 * The rest are the translations: capabilities that drop when the door behind them is missing, a refusal that
 * is *reported* rather than turned into a zero, the three composition counters, and the dump.
 */

#include "RoboInputTestSupport.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* A recording client                                                  */
/* ------------------------------------------------------------------ */

typedef struct ImeFixture {
    /* What it declares, and what it actually implements. */
    uint32_t declared;
    int hasCaret;
    int hasSurrounding;
    int hasInsert;
    int hasComposition;
    /* What it answers with. */
    Result caretResult;
    Result surroundingResult;
    Result compositionResult;
    Result commitResult;
    Result insertResult;
    /* Calls seen, so the suite can prove a door was *not* used. */
    uint32_t caretCalls;
    uint32_t surroundingCalls;
    uint32_t compositionCalls;
    uint32_t commitCalls;
    uint32_t cancelCalls;
    uint32_t insertCalls;
    /* The last arguments the client was handed. */
    uint32_t lastBefore;
    uint32_t lastAfter;
    char lastText[64];
    size_t lastTextLength;
    float caretX;
    float caretY;
    float caretHeight;
} ImeFixture;

static ImeFixture Ime;

static void ImeReset(void)
{
    memset(&Ime, 0, sizeof(Ime));
    Ime.declared = RoboInputImeCapabilityAll;
    Ime.hasCaret = 1;
    Ime.hasSurrounding = 1;
    Ime.hasInsert = 1;
    Ime.hasComposition = 1;
    Ime.caretResult = RoboOk;
    Ime.surroundingResult = RoboOk;
    Ime.compositionResult = RoboOk;
    Ime.commitResult = RoboOk;
    Ime.insertResult = RoboOk;
    Ime.caretX = 100.0f;
    Ime.caretY = 200.0f;
    Ime.caretHeight = 20.0f;
}

static Result ImeCaret(void *userdata, InputMethodCaret *out)
{
    ImeFixture *fixture = (ImeFixture *)userdata;

    fixture->caretCalls++;
    if (fixture->caretResult == RoboOk && out) {
        memset(out, 0, sizeof(*out));
        out->valid = 1;
        out->x = fixture->caretX;
        out->y = fixture->caretY;
        out->width = 2.0f;
        out->height = fixture->caretHeight;
    }
    return fixture->caretResult;
}

static Result ImeSurrounding(void *userdata,
                             uint32_t before,
                             uint32_t after,
                             char *out,
                             size_t capacity,
                             size_t *outLength)
{
    ImeFixture *fixture = (ImeFixture *)userdata;

    fixture->surroundingCalls++;
    fixture->lastBefore = before;
    fixture->lastAfter = after;
    if (fixture->surroundingResult != RoboOk) {
        return fixture->surroundingResult;
    }
    if (capacity < 4u) {
        return RoboOutOfMemory;
    }
    memcpy(out, "abc", 4u);
    if (outLength) {
        *outLength = 3u;
    }
    return RoboOk;
}

static Result ImeInsert(void *userdata, const char *text, size_t length)
{
    ImeFixture *fixture = (ImeFixture *)userdata;

    fixture->insertCalls++;
    if (length >= sizeof(fixture->lastText)) {
        length = sizeof(fixture->lastText) - 1u;
    }
    if (text) {
        memcpy(fixture->lastText, text, length);
    }
    fixture->lastText[length] = '\0';
    fixture->lastTextLength = length;
    return fixture->insertResult;
}

static Result ImeSetComposition(void *userdata, const char *text, size_t length)
{
    ImeFixture *fixture = (ImeFixture *)userdata;

    fixture->compositionCalls++;
    (void)text;
    fixture->lastTextLength = length;
    return fixture->compositionResult;
}

static Result ImeCommit(void *userdata, const char *text, size_t length)
{
    ImeFixture *fixture = (ImeFixture *)userdata;

    fixture->commitCalls++;
    (void)text;
    fixture->lastTextLength = length;
    return fixture->commitResult;
}

static Result ImeCancel(void *userdata)
{
    ImeFixture *fixture = (ImeFixture *)userdata;

    fixture->cancelCalls++;
    return RoboOk;
}

static InputMethodClient ImeClient(void)
{
    InputMethodClient client;

    memset(&client, 0, sizeof(client));
    client.userdata = &Ime;
    client.capabilities = Ime.declared;
    client.caret = Ime.hasCaret ? ImeCaret : NULL;
    client.surroundingText = Ime.hasSurrounding ? ImeSurrounding : NULL;
    client.insertText = Ime.hasInsert ? ImeInsert : NULL;
    client.setComposition = Ime.hasComposition ? ImeSetComposition : NULL;
    client.commitComposition = Ime.hasComposition ? ImeCommit : NULL;
    client.cancelComposition = Ime.hasComposition ? ImeCancel : NULL;
    return client;
}

static InputTarget ImeTarget(LayoutNodeId node)
{
    InputTarget target;
    InputDiag diag;

    InputTargetInit(&target);
    if (InputTargetSetDocument(&target, (WindowId)7u, NULL, node, 0u, RoboInputSpaceWindow, &diag) != RoboOk) {
        RoboCheckMsg(0, "the fixture's document target was refused");
    }
    return target;
}

/* Attach a fresh, fully capable client to @p node. */
static void ImeAttach(InputMethodContext *context, LayoutNodeId node, InputImeTextPurpose purpose)
{
    InputTarget target;
    InputMethodClient client = ImeClient();
    InputDiag diag;

    InputTargetInit(&target);
    target = ImeTarget(node);
    if (InputMethodContextAttach(context, &target, &client, purpose, &diag) != RoboOk) {
        RoboCheckMsg(0, "the fixture's attach was refused");
    }
}

/* ------------------------------------------------------------------ */
/* Vocabulary                                                          */
/* ------------------------------------------------------------------ */

static void TestImeVocabulary(void)
{
    RoboCheckEqInt((int)RoboInputImePurposeNone, 0);
    RoboCheckEqInt((int)RoboInputImePurposeText, 1);
    RoboCheckEqInt((int)RoboInputImePurposeSearch, 2);
    RoboCheckEqInt((int)RoboInputImePurposePassword, 3);
    RoboCheckEqInt((int)RoboInputImePurposeEmail, 4);
    RoboCheckEqInt((int)RoboInputImePurposeUrl, 5);
    RoboCheckEqInt((int)RoboInputImePurposeNumber, 6);
    RoboCheckEqInt((int)RoboInputImeTextPurposeCount, 7);

    RoboCheckStrEq(InputImeTextPurposeName(RoboInputImePurposeNone), "none");
    RoboCheckStrEq(InputImeTextPurposeName(RoboInputImePurposeText), "text");
    RoboCheckStrEq(InputImeTextPurposeName(RoboInputImePurposeSearch), "search");
    RoboCheckStrEq(InputImeTextPurposeName(RoboInputImePurposePassword), "password");
    RoboCheckStrEq(InputImeTextPurposeName(RoboInputImePurposeEmail), "email");
    RoboCheckStrEq(InputImeTextPurposeName(RoboInputImePurposeUrl), "url");
    RoboCheckStrEq(InputImeTextPurposeName(RoboInputImePurposeNumber), "number");
    RoboCheckStrEq(InputImeTextPurposeName((InputImeTextPurpose)9), "unknown");
    RoboCheck(InputImeTextPurposeIsKnown(RoboInputImePurposePassword));
    RoboCheck(!InputImeTextPurposeIsKnown((InputImeTextPurpose)9));

    /*
     * Privacy is exactly *password*. A search field is not private in this sense -- the learning-history rule
     * belongs to the input method -- and a field typed as a number is not private either.
     */
    RoboCheck(InputImeTextPurposeIsPrivate(RoboInputImePurposePassword));
    RoboCheck(!InputImeTextPurposeIsPrivate(RoboInputImePurposeSearch));
    RoboCheck(!InputImeTextPurposeIsPrivate(RoboInputImePurposeText));
    RoboCheck(!InputImeTextPurposeIsPrivate(RoboInputImePurposeEmail));
    RoboCheck(!InputImeTextPurposeIsPrivate(RoboInputImePurposeNone));

    /* The capability bits, positionally: they travel in a recording. */
    RoboCheckEqInt((int)RoboInputImeCapabilitySurroundingText, 1);
    RoboCheckEqInt((int)RoboInputImeCapabilityCaretGeometry, 2);
    RoboCheckEqInt((int)RoboInputImeCapabilitySelection, 4);
    RoboCheckEqInt((int)RoboInputImeCapabilityInsert, 8);
    RoboCheckEqInt((int)RoboInputImeCapabilityAll, 15);
}

/* ------------------------------------------------------------------ */
/* Attach and detach                                                   */
/* ------------------------------------------------------------------ */

static void TestImeAttach(void)
{
    InputMethodContext context;
    InputTarget target = ImeTarget((LayoutNodeId)11u);
    InputTarget other = ImeTarget((LayoutNodeId)12u);
    InputTarget invalid;
    InputMethodClient client;
    InputDiag diag;

    InputDiagReset(&diag);
    /* The client is built *after* the fixture is reset: it copies the doors into a vtable, and one built
     * before `ImeReset` would carry the zeroed fixture's absent composition doors. */
    ImeReset();
    client = ImeClient();
    InputMethodContextInit(&context);
    RoboCheck(!InputMethodContextIsActive(&context));
    RoboCheckEqInt((int)InputMethodContextCapabilities(&context), 0);
    RoboCheck(context.lastRefusal != NULL);
    RoboCheckStrEq(context.lastRefusal, "");

    /* A target that is not a record this build may carry. */
    InputTargetInit(&invalid);
    RoboCheckEqInt(InputMethodContextAttach(&context, &invalid, &client, RoboInputImePurposeText, &diag),
                   RoboInvalidArgument);
    /* A purpose this build does not define. */
    RoboCheckEqInt(InputMethodContextAttach(&context, &target, &client, (InputImeTextPurpose)9, &diag),
                   RoboInvalidArgument);
    RoboCheck(!InputMethodContextIsActive(&context));

    RoboCheckEqInt(InputMethodContextAttach(&context, &target, &client, RoboInputImePurposeText, &diag),
                   RoboOk);
    RoboCheck(InputMethodContextIsActive(&context));
    RoboCheckEqInt((int)context.purpose, (int)RoboInputImePurposeText);
    RoboCheckEqInt(context.compositions, 0);

    /* Attaching the **same** target again is not a move: nothing to cancel. */
    RoboCheckEqInt(InputMethodContextAttach(&context, &target, &client, RoboInputImePurposeText, &diag),
                   RoboOk);
    RoboCheckEqInt(Ime.cancelCalls, 0);
    RoboCheckEqInt(context.cancels, 0);

    /*
     * Attaching a different target **cancels** what the previous editor held: a composition belongs to an
     * editor, and leaving one behind is how a preedit appears in the field the user has left.
     */
    RoboCheckEqInt(InputMethodContextAttach(&context, &other, &client, RoboInputImePurposeSearch, &diag),
                   RoboOk);
    RoboCheckEqInt(Ime.cancelCalls, 1);
    RoboCheckEqInt(context.cancels, 1);
    RoboCheckEqInt((int)context.purpose, (int)RoboInputImePurposeSearch);
    RoboCheck(InputTargetEqual(&context.target, &other));

    /* Detaching cancels too, and says so; detaching twice is not a second cancellation. */
    RoboCheckEqInt(InputMethodContextDetach(&context), 1);
    RoboCheckEqInt(Ime.cancelCalls, 2);
    RoboCheck(!InputMethodContextIsActive(&context));
    RoboCheckEqInt(InputMethodContextDetach(&context), 0);

    /* NULL is a no-op rather than a crash. */
    InputMethodContextInit(NULL);
    RoboCheckEqInt(InputMethodContextDetach(NULL), 0);
    RoboCheck(!InputMethodContextIsActive(NULL));
    RoboCheckEqInt((int)InputMethodContextCapabilities(NULL), 0);
}

/* ------------------------------------------------------------------ */
/* Capabilities                                                        */
/* ------------------------------------------------------------------ */

static void TestImeCapabilities(void)
{
    InputMethodContext context;
    InputMethodClient client;
    InputDiag diag;

    InputDiagReset(&diag);
    ImeReset();
    InputMethodContextInit(&context);

    /* A client that declares caret geometry with no door behind it: the layer reports only what it can
     * actually get, so a caller's check is not a guess. */
    ImeReset();
    Ime.declared = RoboInputImeCapabilityCaretGeometry;
    Ime.hasCaret = 0;
    client = ImeClient();
    {
        InputTarget target = ImeTarget((LayoutNodeId)11u);

        RoboCheckEqInt(InputMethodContextAttach(&context, &target, &client, RoboInputImePurposeText, &diag),
                       RoboOk);
    }
    RoboCheckEqInt((int)InputMethodContextCapabilities(&context), 0);

    /* The door without the bit: also not advertised. */
    ImeReset();
    Ime.declared = RoboInputImeCapabilitySurroundingText;
    Ime.hasInsert = 0;
    Ime.hasComposition = 0;
    client = ImeClient();
    {
        InputTarget target = ImeTarget((LayoutNodeId)11u);

        RoboCheckEqInt(InputMethodContextAttach(&context, &target, &client, RoboInputImePurposeText, &diag),
                       RoboOk);
    }
    RoboCheckEqInt((int)InputMethodContextCapabilities(&context), (int)RoboInputImeCapabilitySurroundingText);

    /* Both halves present: the declared set survives, and a capability this build does not name never
     * appears. */
    ImeReset();
    Ime.declared = RoboInputImeCapabilityAll | (uint32_t)1u << 20;
    client = ImeClient();
    {
        InputTarget target = ImeTarget((LayoutNodeId)11u);

        RoboCheckEqInt(InputMethodContextAttach(&context, &target, &client, RoboInputImePurposeText, &diag),
                       RoboOk);
    }
    RoboCheckEqInt((int)InputMethodContextCapabilities(&context), (int)RoboInputImeCapabilityAll);

    /* No client at all: active, and able to do nothing. That is a legitimate state -- a field whose editor
     * is not wired yet -- and the honest answer is zero capabilities rather than a set of promises. */
    ImeReset();
    {
        InputTarget target = ImeTarget((LayoutNodeId)11u);

        RoboCheckEqInt(InputMethodContextAttach(&context, &target, NULL, RoboInputImePurposeText, &diag),
                       RoboOk);
    }
    RoboCheck(InputMethodContextIsActive(&context));
    RoboCheckEqInt((int)InputMethodContextCapabilities(&context), 0);
}

/* ------------------------------------------------------------------ */
/* Caret                                                               */
/* ------------------------------------------------------------------ */

static void TestImeCaret(void)
{
    InputMethodContext context;
    InputMethodClient client;
    InputMethodCaret caret;
    InputDiag diag;

    InputDiagReset(&diag);
    ImeReset();
    InputMethodContextInit(&context);
    memset(&caret, 0, sizeof(caret));

    /* Nothing attached: `RoboNotFound`, which is a different thing from "attached and cannot answer". */
    RoboCheckEqInt(InputMethodContextCaret(&context, &caret, &diag), RoboNotFound);
    RoboCheckEqInt(caret.valid, 0);

    /* Attached, but the client has no geometry to give *yet* -- nobody laid it out. Refused, and counted. */
    ImeReset();
    Ime.declared = 0u;
    Ime.hasCaret = 0;
    client = ImeClient();
    {
        InputTarget target = ImeTarget((LayoutNodeId)11u);

        RoboCheckEqInt(InputMethodContextAttach(&context, &target, &client, RoboInputImePurposeText, &diag),
                       RoboOk);
    }
    RoboCheckEqInt(InputMethodContextCaret(&context, &caret, &diag), RoboNotSupported);
    RoboCheckEqInt(context.refusals, 1);
    RoboCheckStrEq(context.lastRefusal, "the editor reports no caret geometry");

    /* A client that *has* the door and fails: reported, and never turned into a zeroed caret. */
    ImeReset();
    Ime.caretResult = RoboInvalidArgument;
    client = ImeClient();
    {
        InputTarget target = ImeTarget((LayoutNodeId)11u);

        RoboCheckEqInt(InputMethodContextAttach(&context, &target, &client, RoboInputImePurposeText, &diag),
                       RoboOk);
    }
    RoboCheckEqInt(InputMethodContextCaret(&context, &caret, &diag), RoboNotSupported);
    RoboCheckEqInt(context.refusals, 2);
    RoboCheckStrEq(context.lastRefusal, "the editor refused to report its caret");

    /* Success: the client's rectangle, unchanged. */
    ImeReset();
    client = ImeClient();
    {
        InputTarget target = ImeTarget((LayoutNodeId)11u);

        RoboCheckEqInt(InputMethodContextAttach(&context, &target, &client, RoboInputImePurposeText, &diag),
                       RoboOk);
    }
    RoboCheckEqInt(InputMethodContextCaret(&context, &caret, &diag), RoboOk);
    RoboCheckEqInt(caret.valid, 1);
    RoboCheckEqInt((int)caret.x, 100);
    RoboCheckEqInt((int)caret.y, 200);
    RoboCheckEqInt((int)caret.height, 20);
    RoboCheckEqInt(Ime.caretCalls, 1);
    /* The refusal note is **sticky**: it names the last question that was refused, and the `refusals`
     * counter is the aggregate a caller watches. A later success does not rewrite history -- a diagnostic
     * that erased itself would be a diagnostic a reader could not quote. */
    RoboCheckStrEq(context.lastRefusal, "the editor refused to report its caret");

    RoboCheckEqInt(InputMethodContextCaret(&context, NULL, &diag), RoboInvalidArgument);
    RoboCheckEqInt(InputMethodContextCaret(NULL, &caret, &diag), RoboInvalidArgument);
}

/* ------------------------------------------------------------------ */
/* Surrounding text, and the password rule                             */
/* ------------------------------------------------------------------ */

static void TestImeSurrounding(void)
{
    InputMethodContext context;
    InputMethodClient client;
    char buffer[RoboInputImeSurroundingMax];
    size_t length = 0u;
    InputDiag diag;

    InputDiagReset(&diag);
    ImeReset();
    InputMethodContextInit(&context);

    RoboCheckEqInt(InputMethodContextSurroundingText(&context, 4u, 4u, buffer, sizeof(buffer), &length,
                                                     &diag),
                   RoboNotFound);

    /* An ordinary text field: the client is asked, and the counts pass through. */
    ImeAttach(&context, (LayoutNodeId)11u, RoboInputImePurposeText);
    RoboCheckEqInt(InputMethodContextSurroundingText(&context, 4u, 6u, buffer, sizeof(buffer), &length,
                                                     &diag),
                   RoboOk);
    RoboCheckEqInt(Ime.surroundingCalls, 1);
    RoboCheckEqInt((int)Ime.lastBefore, 4);
    RoboCheckEqInt((int)Ime.lastAfter, 6);
    RoboCheckEqInt((int)length, 3);
    RoboCheckStrEq(buffer, "abc");

    /* A search field is *not* private in this sense: the learning-history rule is the input method's. */
    ImeAttach(&context, (LayoutNodeId)11u, RoboInputImePurposeSearch);
    RoboCheckEqInt(InputMethodContextSurroundingText(&context, 1u, 1u, buffer, sizeof(buffer), &length,
                                                     &diag),
                   RoboOk);
    RoboCheckEqInt(Ime.surroundingCalls, 2);

    /*
     * The rule this layer keeps for itself: a **password** field never asks, however willing the editor is.
     * The proof is the call count, not the answer -- a client that could report the text must not be asked.
     */
    ImeReset();
    ImeAttach(&context, (LayoutNodeId)11u, RoboInputImePurposePassword);
    RoboCheckEqInt(InputMethodContextSurroundingText(&context, 8u, 8u, buffer, sizeof(buffer), &length, &diag),
                   RoboNotSupported);
    RoboCheckEqInt(Ime.surroundingCalls, 0);
    RoboCheckEqInt((int)length, 0);
    RoboCheckStrEq(buffer, "");
    RoboCheckEqInt(context.refusals, 1);
    RoboCheckStrEq(context.lastRefusal, "a private field reports no surrounding text");

    /* Capability and door are checked separately, and a failing client is reported rather than zeroed. */
    ImeReset();
    Ime.declared = RoboInputImeCapabilityCaretGeometry;
    Ime.hasSurrounding = 0;
    client = ImeClient();
    {
        InputTarget target = ImeTarget((LayoutNodeId)12u);

        RoboCheckEqInt(InputMethodContextAttach(&context, &target, &client, RoboInputImePurposeText, &diag),
                       RoboOk);
    }
    RoboCheckEqInt(InputMethodContextSurroundingText(&context, 1u, 1u, buffer, sizeof(buffer), &length, &diag),
                   RoboNotSupported);
    RoboCheckEqInt(Ime.surroundingCalls, 0);

    ImeReset();
    Ime.surroundingResult = RoboOutOfMemory;
    client = ImeClient();
    {
        InputTarget target = ImeTarget((LayoutNodeId)12u);

        RoboCheckEqInt(InputMethodContextAttach(&context, &target, &client, RoboInputImePurposeText, &diag),
                       RoboOk);
    }
    RoboCheckEqInt(InputMethodContextSurroundingText(&context, 1u, 1u, buffer, sizeof(buffer), &length, &diag),
                   RoboNotSupported);
    RoboCheckEqInt(Ime.surroundingCalls, 1);

    /* A buffer too small is the caller's mistake and is refused by name, with nothing written. */
    RoboCheckEqInt(InputMethodContextSurroundingText(&context, 1u, 1u, NULL, sizeof(buffer), &length, &diag),
                   RoboInvalidArgument);
}

/* ------------------------------------------------------------------ */
/* Composition                                                         */
/* ------------------------------------------------------------------ */

static void TestImeComposition(void)
{
    InputMethodContext context;
    InputMethodClient client;
    InputDiag diag;

    InputDiagReset(&diag);
    ImeReset();
    InputMethodContextInit(&context);

    /* Nothing attached: every door says so, and none of them counts as a composition. */
    RoboCheckEqInt(InputMethodContextCompose(&context, "ka", 2u, 1u, &diag), RoboNotFound);
    RoboCheckEqInt(InputMethodContextCommit(&context, NULL, 0u, &diag), RoboNotFound);
    RoboCheckEqInt(InputMethodContextCancel(&context, &diag), RoboNotFound);
    RoboCheckEqInt(InputMethodContextInsertText(&context, "x", 1u, &diag), RoboNotFound);

    ImeAttach(&context, (LayoutNodeId)11u, RoboInputImePurposeText);

    /* Start and update are the same door: a composition is text in the editor's buffer, and both are writes
     * to it. */
    RoboCheckEqInt(InputMethodContextCompose(&context, "k", 1u, 0u, &diag), RoboOk);
    RoboCheckEqInt(context.compositions, 1);
    RoboCheckEqInt((int)Ime.lastTextLength, 1);
    RoboCheckEqInt(InputMethodContextCompose(&context, "ka", 2u, 2u, &diag), RoboOk);
    RoboCheckEqInt(context.compositions, 2);
    RoboCheckEqInt((int)Ime.lastTextLength, 2);

    /* Too many clauses: refused before the client is touched, because it is this layer's vocabulary. */
    RoboCheckEqInt(InputMethodContextCompose(&context, "ka", 2u, RoboInputImeMaxClauses + 1u, &diag),
                   RoboInvalidArgument);
    RoboCheckEqInt(context.compositions, 2);

    /* Text of a non-zero length is required: a NULL pointer with a length is a defect, not an empty
     * composition. */
    RoboCheckEqInt(InputMethodContextCompose(&context, NULL, 4u, 0u, &diag), RoboInvalidArgument);

    RoboCheckEqInt(InputMethodContextCommit(&context, NULL, 0u, &diag), RoboOk);
    RoboCheckEqInt(context.commits, 1);
    RoboCheckEqInt(Ime.commitCalls, 1);

    RoboCheckEqInt(InputMethodContextCancel(&context, &diag), RoboOk);
    RoboCheckEqInt(context.cancels, 1);
    RoboCheckEqInt(Ime.cancelCalls, 1);

    /* A commit carries the replacement text through unchanged, and an insertion is the committed path. */
    RoboCheckEqInt(InputMethodContextCommit(&context, "kana", 4u, &diag), RoboOk);
    RoboCheckEqInt((int)Ime.lastTextLength, 4);
    RoboCheckEqInt(InputMethodContextInsertText(&context, "hi", 2u, &diag), RoboOk);
    RoboCheckEqInt(Ime.insertCalls, 1);
    RoboCheckStrEq(Ime.lastText, "hi");

    /* An editor that cannot hold a composition: refused rather than composed somewhere else. */
    ImeReset();
    Ime.hasComposition = 0;
    client = ImeClient();
    {
        InputTarget target = ImeTarget((LayoutNodeId)13u);

        RoboCheckEqInt(InputMethodContextAttach(&context, &target, &client, RoboInputImePurposeText, &diag),
                       RoboOk);
    }
    RoboCheckEqInt(InputMethodContextCompose(&context, "k", 1u, 0u, &diag), RoboNotSupported);
    RoboCheckEqInt(InputMethodContextCommit(&context, NULL, 0u, &diag), RoboNotSupported);
    RoboCheckEqInt(InputMethodContextCancel(&context, &diag), RoboNotSupported);
    RoboCheckEqInt(context.refusals, 3);
    RoboCheckStrEq(context.lastRefusal, "the editor cannot cancel a composition");

    /* A client that fails a commit leaves the counters alone: the composition is still there. */
    {
        uint32_t commitsBefore = context.commits;
        uint32_t compositionsBefore = context.compositions;

        ImeReset();
        Ime.commitResult = RoboInvalidArgument;
    client = ImeClient();
    {
        InputTarget target = ImeTarget((LayoutNodeId)13u);

        RoboCheckEqInt(InputMethodContextAttach(&context, &target, &client, RoboInputImePurposeText, &diag),
                       RoboOk);
    }
        RoboCheckEqInt(InputMethodContextCommit(&context, "x", 1u, &diag), RoboNotSupported);
        RoboCheckEqInt(context.commits, (int)commitsBefore);
        RoboCheckEqInt(context.compositions, (int)compositionsBefore);
    }
}

/* ------------------------------------------------------------------ */
/* Candidate placement                                                 */
/* ------------------------------------------------------------------ */

static void TestImeCandidate(void)
{
    InputMethodContext context;
    InputMethodCandidateRect rect;
    GfxRect viewport;
    InputDiag diag;

    InputDiagReset(&diag);
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = 800.0f;
    viewport.height = 600.0f;

    InputMethodContextInit(&context);
    /* No caret, no anchor: refused, and **not** answered with the origin. */
    RoboCheckEqInt(InputMethodContextCandidateRect(&context, 200.0f, 100.0f, &viewport, &rect, &diag),
                   RoboNotFound);
    RoboCheckEqInt(rect.valid, 0);

    ImeReset();
    ImeAttach(&context, (LayoutNodeId)11u, RoboInputImePurposeText);

    /* Below the caret by default: the candidate must not cover the text being composed. */
    RoboCheckEqInt(InputMethodContextCandidateRect(&context, 200.0f, 100.0f, &viewport, &rect, &diag),
                   RoboOk);
    RoboCheckEqInt(rect.valid, 1);
    RoboCheckEqInt((int)rect.x, 100);
    RoboCheckEqInt((int)rect.y, 220);
    RoboCheckEqInt((int)rect.width, 200);
    RoboCheckEqInt((int)rect.height, 100);
    RoboCheckEqInt(rect.flipped, 0);
    RoboCheckEqInt(rect.clamped, 0);

    /* No viewport: the caret's own answer, unclamped. */
    RoboCheckEqInt(InputMethodContextCandidateRect(&context, 10.0f, 10.0f, NULL, &rect, &diag), RoboOk);
    RoboCheckEqInt((int)rect.y, 220);

    /* A caret near the bottom: the candidate goes **above** it rather than off the screen. */
    Ime.caretY = 550.0f;
    RoboCheckEqInt(InputMethodContextCandidateRect(&context, 200.0f, 100.0f, &viewport, &rect, &diag),
                   RoboOk);
    RoboCheckEqInt((int)rect.y, 450);
    RoboCheckEqInt(rect.flipped, 1);
    RoboCheckEqInt(rect.clamped, 0);

    /* A caret near the right edge: clamped so the window never hangs off the side. */
    Ime.caretY = 200.0f;
    Ime.caretX = 700.0f;
    RoboCheckEqInt(InputMethodContextCandidateRect(&context, 300.0f, 100.0f, &viewport, &rect, &diag),
                   RoboOk);
    RoboCheckEqInt((int)rect.x, 500);
    RoboCheckEqInt(rect.flipped, 0);
    RoboCheckEqInt(rect.clamped, 1);

    /* A caret near the bottom whose candidate *would* fit above: the flip is used, and no clamping is
     * needed. (The clamp is only reachable when neither side fits, which takes a candidate taller than the
     * viewport -- the next case.) */
    Ime.caretY = 590.0f;
    Ime.caretX = 100.0f;
    RoboCheckEqInt(InputMethodContextCandidateRect(&context, 200.0f, 100.0f, &viewport, &rect, &diag),
                   RoboOk);
    RoboCheckEqInt((int)rect.y, 490);
    RoboCheckEqInt(rect.flipped, 1);
    RoboCheckEqInt(rect.clamped, 0);

    /* Neither below nor above fits -- the candidate is taller than the viewport. The window is kept on
     * screen and *says* it was clamped, rather than being drawn half off the bottom to satisfy the flip
     * rule. */
    Ime.caretY = 200.0f;
    RoboCheckEqInt(InputMethodContextCandidateRect(&context, 200.0f, 700.0f, &viewport, &rect, &diag),
                   RoboOk);
    RoboCheckEqInt((int)rect.y, 0);
    RoboCheckEqInt(rect.flipped, 0);
    RoboCheckEqInt(rect.clamped, 1);

    /* A size that is not a size. */
    RoboCheckEqInt(InputMethodContextCandidateRect(&context, 0.0f, 100.0f, &viewport, &rect, &diag),
                   RoboInvalidArgument);
    RoboCheckEqInt(InputMethodContextCandidateRect(&context, 10.0f, -1.0f, &viewport, &rect, &diag),
                   RoboInvalidArgument);
    RoboCheckEqInt(InputMethodContextCandidateRect(&context, 10.0f, 10.0f, &viewport, NULL, &diag),
                   RoboInvalidArgument);
}

/* ------------------------------------------------------------------ */
/* Dump                                                                */
/* ------------------------------------------------------------------ */

static void TestImeDump(void)
{
    InputMethodContext context;
    InputMethodCaret caret;
    InputMethodCandidateRect rect;
    InputDiag diag;
    char line[512];
    char again[512];
    size_t length = 0u;
    size_t againLength = 0u;

    InputDiagReset(&diag);
    ImeReset();
    InputMethodContextInit(&context);
    RoboCheckEqInt(InputMethodContextWrite(&context, line, sizeof(line), &length, &diag), RoboOk);
    RoboCheckStrEq(line, "ime active=0 target=none purpose=none capabilities=0 compositions=0 commits=0 "
                         "cancels=0 refusals=0\n");

    ImeAttach(&context, (LayoutNodeId)11u, RoboInputImePurposeSearch);
    RoboCheckEqInt(InputMethodContextCompose(&context, "ka", 2u, 1u, &diag), RoboOk);
    RoboCheckEqInt(InputMethodContextCommit(&context, NULL, 0u, &diag), RoboOk);
    /* One refusal, so the line carries it. */
    RoboCheckEqInt(InputMethodContextCompose(&context, "ka", 2u, RoboInputImeMaxClauses + 1u, &diag),
                   RoboInvalidArgument);

    RoboCheckEqInt(InputMethodContextWrite(&context, line, sizeof(line), &length, &diag), RoboOk);
    RoboCheckEqInt((int)strlen(line), (int)length);
    RoboCheckStrEq(line, "ime active=1 target=document purpose=search capabilities=15 compositions=1 "
                         "commits=1 cancels=0 refusals=0\n");
    /* Two generations are the same bytes: a dump a reviewer diffs cannot drift on its own. */
    RoboCheckEqInt(InputMethodContextWrite(&context, again, sizeof(again), &againLength, &diag), RoboOk);
    RoboCheckEqInt((int)againLength, (int)length);
    RoboCheck(strcmp(line, again) == 0);

    /* The caret and the candidate rectangle have lines of their own, and both survive a narrow buffer
     * by refusing rather than truncating. */
    RoboCheckEqInt(InputMethodContextCaret(&context, &caret, &diag), RoboOk);
    RoboCheckEqInt(InputMethodCaretWrite(&caret, line, sizeof(line), &length, &diag), RoboOk);
    RoboCheckEqInt((int)strlen(line), (int)length);
    RoboCheckStrEq(line, "ime-caret valid=1 x=100 y=200 width=2 height=20 horizontal=0\n");

    RoboCheckEqInt(InputMethodCaretWrite(NULL, line, sizeof(line), &length, &diag), RoboOk);
    RoboCheckStrEq(line, "ime-caret <none>\n");
    RoboCheckEqInt(InputMethodCandidateRectWrite(NULL, line, sizeof(line), &length, &diag), RoboOk);
    RoboCheckStrEq(line, "ime-candidate <none>\n");

    {
        GfxRect viewport;
        char small[16];
        size_t needed = 0u;

        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = 800.0f;
        viewport.height = 600.0f;
        RoboCheckEqInt(InputMethodContextCandidateRect(&context, 200.0f, 100.0f, &viewport, &rect, &diag),
                       RoboOk);
        RoboCheckEqInt(InputMethodCandidateRectWrite(&rect, line, sizeof(line), &length, &diag), RoboOk);
        RoboCheckStrEq(line, "ime-candidate valid=1 x=100 y=220 width=200 height=100 flipped=0 clamped=0\n");
        RoboCheckEqInt(InputMethodCandidateRectWrite(&rect, small, sizeof(small), &needed, &diag),
                       RoboNotSupported);
        RoboCheckStrEq(small, "?");
    }

    RoboCheckEqInt(InputMethodContextWrite(NULL, line, sizeof(line), &length, &diag), RoboOk);
    RoboCheckStrEq(line, "ime <none>\n");
}

/* ------------------------------------------------------------------ */
/* Frame                                                               */
/* ------------------------------------------------------------------ */

void TestRegisterInputIme(void)
{
    TestRegister("input.ime.vocabulary", TestImeVocabulary);
    TestRegister("input.ime.attach", TestImeAttach);
    TestRegister("input.ime.capabilities", TestImeCapabilities);
    TestRegister("input.ime.caret", TestImeCaret);
    TestRegister("input.ime.surrounding", TestImeSurrounding);
    TestRegister("input.ime.composition", TestImeComposition);
    TestRegister("input.ime.candidate", TestImeCandidate);
    TestRegister("input.ime.dump", TestImeDump);
}
