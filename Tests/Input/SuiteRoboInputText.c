/*
 * RoboInput text-pipeline suite (Phase 13.11/13.10/13.8) — `input.text.*`.
 *
 * Nine cases about the one door that turns a key into characters, and the *separation* is the subject rather
 * than the characters:
 *
 *   `vocabulary`  the seven origins, asserted positionally with their names, because they are what a
 *                 diagnostics page reads and what a failure names;
 *
 *   `init`        a fresh pipeline points at no layout, waits for no accent and counts nothing; a NULL
 *                 pipeline is a refusal rather than a crash, everywhere;
 *
 *   `kinds`       the four ways a call is refused (a non-key event, a short output array, a NULL argument) and
 *                 the one that is *not* a refusal: a key **up** produces nothing and is accepted, so a caller
 *                 can hand every key event to this one door;
 *
 *   `plain`       one key becomes one `TEXT_INPUT` event carrying the key event's device, timestamp,
 *                 modifiers, window and display -- and **no plane**, which is asserted through the model's own
 *                 validator rather than by reading a field, because "a text event has no position" is a rule
 *                 the event model already enforces;
 *
 *   `levels`      the layout's levels reached through the pipeline: shift, caps lock and caps lock *with*
 *                 shift, AltGr on a table that has no third level, and a control chord producing nothing
 *                 rather than a character (the case that would otherwise fire `Ctrl+C` *and* insert a `c`);
 *
 *   `deadkey`     arming, composing, and the two facts that make the pending accent honest: a keystroke that
 *                 means no character does not disturb it, and a **release** does not abandon it;
 *
 *   `repeat`      the same accent twice: the spacing form, which is how a user types `´` on a keyboard whose
 *                 only acute is a dead one;
 *
 *   `replace`     a *different* accent while one is pending: the old one's spacing form, then the new one
 *                 waits -- so no accent is ever silently dropped;
 *
 *   `reject`      an accent and a base with no precomposed form: **two** events, in the order they were
 *                 typed, which is the case that makes the door's capacity two rather than one;
 *
 *   `cancel`      focus loss, `Escape` and a device disconnect, each of which must not let an accent modify the
 *                 first character typed into the *next* field;
 *
 *   `dump`        the one line, asserted whole and byte-for-byte over two calls.
 *
 * Every expectation is either a character the user typed or a counter the module publishes. Nothing here
 * asserts a *glyph*: the pipeline produces text and stops, and the shaping of `é` is RoboText's (12.x), not
 * this suite's.
 */

#include "RoboInputTestSupport.h"

/* ------------------------------------------------------------------ */
/* Fixtures                                                            */
/* ------------------------------------------------------------------ */

/** A key **down** at @p physical with @p modifiers, carrying a real device and a timestamp. */
static InputEvent TextKeyEvent(PhysicalKey physical, InputModifiers modifiers, uint64_t timestampMs)
{
    InputEvent event;
    InputKeyboardData data = InputKeyData(physical, RoboLogicalKeyUnknown, RoboKeyActionDown, 0, NULL);
    InputDevice device = GetRoboInputDevice(1u, RoboInputSourceKeyboard, "Test keyboard");

    InputEventInit(&event);
    if (InputEventSetKeyboard(&event, RoboInputEventKeyDown, RoboInputSourceKeyboard, &data, NULL) !=
        RoboOk) {
        RoboCheckMsg(0, "the case's key event was refused");
    }
    InputEventSetDevice(&event, &device, NULL);
    InputEventSetTimestamp(&event, timestampMs);
    InputEventSetModifiers(&event, modifiers, NULL);
    InputEventSetWindow(&event, 3u);
    return event;
}

/** A key **up** at @p physical. */
static InputEvent TextKeyUp(PhysicalKey physical)
{
    InputEvent event;
    InputKeyboardData data = InputKeyData(physical, RoboLogicalKeyUnknown, RoboKeyActionUp, 0, NULL);

    InputEventInit(&event);
    if (InputEventSetKeyboard(&event, RoboInputEventKeyUp, RoboInputSourceKeyboard, &data, NULL) !=
        RoboOk) {
        RoboCheckMsg(0, "the case's key-up event was refused");
    }
    InputEventSetWindow(&event, 3u);
    return event;
}

/**
 * Translate one key and hand back how many events came out.
 *
 * The one-liner every case wants; the produced events land in the caller's array and the origin in
 * @p outOrigin. A refusal is reported rather than swallowed, because a case that got one would otherwise
 * read as "produced nothing".
 */
static uint32_t TextTranslate(InputTextPipeline *pipeline,
                              const InputEvent *keyEvent,
                              InputEvent *outEvents,
                              InputTextOrigin *outOrigin)
{
    uint32_t count = 0u;

    if (InputTextPipelineTranslate(pipeline, keyEvent, outEvents, RoboInputTextMaxEvents, &count,
                                   outOrigin, NULL) != RoboOk) {
        RoboCheckMsg(0, "the case's translation was refused");
    }
    return count;
}

/** Assert that @p count is 1 and that event 0's text is @p text. */
static void CheckText(const InputEvent *events, uint32_t count, const char *text)
{
    RoboCheckEqInt((int)count, 1);
    RoboCheckEqInt((int)events[0].type, (int)RoboInputEventTextInput);
    RoboCheckEqInt((int)InputEventPayloadKind(events[0].type), (int)RoboInputPayloadText);
    RoboCheckStrEq(events[0].payload.text.text, text);
    RoboCheck(InputEventIsValid(&events[0]));
}

/* ------------------------------------------------------------------ */
/* Vocabulary                                                          */
/* ------------------------------------------------------------------ */

static void TestTextVocabulary(void)
{
    RoboCheckEqInt((int)RoboInputTextOriginNone, 0);
    RoboCheckEqInt((int)RoboInputTextOriginKey, 1);
    RoboCheckEqInt((int)RoboInputTextOriginDeadKeyRepeated, 2);
    RoboCheckEqInt((int)RoboInputTextOriginComposed, 3);
    RoboCheckEqInt((int)RoboInputTextOriginDeadKeyRejected, 4);
    RoboCheckEqInt((int)RoboInputTextOriginDeadKeyPending, 5);
    RoboCheckEqInt((int)RoboInputTextOriginDeadKeyReplaced, 6);
    RoboCheckEqInt((int)RoboInputTextOriginCount, 7);
    RoboCheckStrEq(InputTextOriginName(RoboInputTextOriginNone), "none");
    RoboCheckStrEq(InputTextOriginName(RoboInputTextOriginKey), "key");
    RoboCheckStrEq(InputTextOriginName(RoboInputTextOriginDeadKeyRepeated), "dead-key-repeated");
    RoboCheckStrEq(InputTextOriginName(RoboInputTextOriginComposed), "composed");
    RoboCheckStrEq(InputTextOriginName(RoboInputTextOriginDeadKeyRejected), "dead-key-rejected");
    RoboCheckStrEq(InputTextOriginName(RoboInputTextOriginDeadKeyPending), "dead-key-pending");
    RoboCheckStrEq(InputTextOriginName(RoboInputTextOriginDeadKeyReplaced), "dead-key-replaced");
    RoboCheckStrEq(InputTextOriginName((InputTextOrigin)99), "unknown");
    /* Two events, and that is the maximum: `dead + an incompatible base` is why, and the door refuses a
     * caller with room for fewer rather than producing half a pair. */
    RoboCheckEqInt((int)RoboInputTextMaxEvents, 2);
}

/* ------------------------------------------------------------------ */
/* Bring-up                                                            */
/* ------------------------------------------------------------------ */

static void TestTextInit(void)
{
    InputTextPipeline pipeline;
    InputLayout layout;
    InputDiag diag;
    InputEvent events[RoboInputTextMaxEvents];
    char line[RoboInputTextPipelineDumpMax];
    size_t length = 0u;

    InputTextPipelineInit(&pipeline);
    RoboCheck(pipeline.layout == NULL);
    RoboCheckEqInt((int)InputTextPipelinePendingDeadKey(&pipeline), (int)RoboInputDeadKeyIdNone);
    RoboCheckEqInt((int)InputTextPipelineDeadKeyStartedMs(&pipeline), 0);
    RoboCheckEqInt((int)InputTextPipelineCancelDeadKey(&pipeline), 0);
    RoboCheckEqInt((int)pipeline.producedTotal, 0);
    RoboCheckEqInt((int)pipeline.emptyTotal, 0);
    RoboCheckEqInt((int)pipeline.composedTotal, 0);
    RoboCheckEqInt((int)pipeline.rejectedTotal, 0);

    RoboCheckEqInt(InputLayoutCreateBuiltin(&layout, RoboInputLayoutUsAscii, &diag), RoboOk);
    RoboCheckEqInt(InputTextPipelineSetLayout(&pipeline, &layout, &diag), RoboOk);
    RoboCheck(pipeline.layout == &layout);
    /* A NULL layout is *a* layout: every key then produces nothing, which is what a session with no text
     * focus wants without a second code path. */
    RoboCheckEqInt(InputTextPipelineSetLayout(&pipeline, NULL, &diag), RoboOk);
    RoboCheck(pipeline.layout == NULL);
    RoboCheckEqInt(InputTextPipelineSetLayout(NULL, &layout, &diag), RoboInvalidArgument);

    /* No layout means no characters, whichever key arrives -- and no crash. */
    {
        InputEvent key = TextKeyEvent(RoboPhysicalKeyA, RoboInputModifiersNone, 1u);
        InputTextOrigin origin = RoboInputTextOriginNone;

        RoboCheckEqInt((int)TextTranslate(&pipeline, &key, events, &origin), 0);
        RoboCheckEqInt((int)origin, (int)RoboInputTextOriginNone);
        RoboCheckEqInt((int)pipeline.emptyTotal, 1);
    }

    RoboCheckEqInt(InputTextPipelineSetLayout(&pipeline, &layout, &diag), RoboOk);
    RoboCheckEqInt(InputTextPipelineWrite(&pipeline, line, sizeof(line), &length, &diag), RoboOk);
    RoboCheckStrEq(line, "text layout=us-ascii dead=none produced=0 empty=1 composed=0 rejected=0\n");

    /* Nothing to dump is a line and not a refusal, and neither is a NULL pipeline. */
    RoboCheckEqInt(InputTextPipelineWrite(NULL, line, sizeof(line), &length, &diag), RoboOk);
    RoboCheckStrEq(line, "text <none>\n");
    RoboCheckEqInt(InputTextPipelineWrite(&pipeline, NULL, sizeof(line), &length, &diag),
                   RoboInvalidArgument);
    RoboCheckEqInt(InputTextPipelineWrite(&pipeline, line, 0u, &length, &diag), RoboInvalidArgument);
    RoboCheckEqInt(InputTextPipelineWrite(&pipeline, line, sizeof(line), NULL, &diag),
                   RoboInvalidArgument);

    InputTextPipelineInit(NULL);
    RoboCheckEqInt((int)InputTextPipelinePendingDeadKey(NULL), (int)RoboInputDeadKeyIdNone);
    RoboCheckEqInt((int)InputTextPipelineDeadKeyStartedMs(NULL), 0);
    RoboCheckEqInt((int)InputTextPipelineCancelDeadKey(NULL), 0);
}

/* ------------------------------------------------------------------ */
/* What is refused, and what merely means nothing                      */
/* ------------------------------------------------------------------ */

static void TestTextKinds(void)
{
    InputTextPipeline pipeline;
    InputLayout layout;
    InputDiag diag;
    InputEvent events[RoboInputTextMaxEvents];
    uint32_t count = 0u;

    InputTextPipelineInit(&pipeline);
    RoboCheckEqInt(InputLayoutCreateBuiltin(&layout, RoboInputLayoutUsAscii, &diag), RoboOk);
    InputTextPipelineSetLayout(&pipeline, &layout, &diag);

    /* A pointer event is refused: a pipeline that accepted one would have to decide what a click meant to a
     * keyboard. */
    {
        InputEvent pointer = InputPointerMove(10, 10, 0u, RoboInputSpaceScreen, 3u);

        count = 7u;
        RoboCheckEqInt(InputTextPipelineTranslate(&pipeline, &pointer, events, RoboInputTextMaxEvents,
                                                  &count, NULL, &diag),
                       RoboInvalidArgument);
        RoboCheckEqInt((int)count, 0);
        RoboCheckEqInt(diag.domain, RoboInputDomainText);
    }

    /* The arguments, each of which is a refusal rather than a crash. A short output array is one of them:
     * a chord may produce two events, and a caller with room for one would silently lose half of what was
     * typed. */
    {
        InputEvent key = TextKeyEvent(RoboPhysicalKeyA, RoboInputModifiersNone, 1u);

        count = 0u;
        RoboCheckEqInt(InputTextPipelineTranslate(NULL, &key, events, RoboInputTextMaxEvents, &count, NULL,
                                                  &diag),
                       RoboInvalidArgument);
        RoboCheckEqInt(InputTextPipelineTranslate(&pipeline, NULL, events, RoboInputTextMaxEvents, &count,
                                                  NULL, &diag),
                       RoboInvalidArgument);
        RoboCheckEqInt(InputTextPipelineTranslate(&pipeline, &key, NULL, RoboInputTextMaxEvents, &count,
                                                  NULL, &diag),
                       RoboInvalidArgument);
        RoboCheckEqInt(InputTextPipelineTranslate(&pipeline, &key, events, RoboInputTextMaxEvents, NULL,
                                                  NULL, &diag),
                       RoboInvalidArgument);
        RoboCheckEqInt(InputTextPipelineTranslate(&pipeline, &key, events, 1u, &count, NULL, &diag),
                       RoboInvalidArgument);
        RoboCheckEqInt((int)count, 0);
        /* None of those refusals touched the counters: a call that was refused did not happen. */
        RoboCheckEqInt((int)pipeline.producedTotal, 0);
        RoboCheckEqInt((int)pipeline.emptyTotal, 0);
    }

    /* A key **up** is not a refusal: it is a key event, and it means no character. That is what lets a
     * caller hand every key event to this one door. */
    {
        InputEvent up = TextKeyUp(RoboPhysicalKeyA);
        InputTextOrigin origin = RoboInputTextOriginKey;

        RoboCheckEqInt((int)TextTranslate(&pipeline, &up, events, &origin), 0);
        RoboCheckEqInt((int)origin, (int)RoboInputTextOriginNone);
        /* ... and it is not an *empty* chord either: nothing happened, so nothing is counted. */
        RoboCheckEqInt((int)pipeline.emptyTotal, 0);
        RoboCheckEqInt((int)pipeline.producedTotal, 0);
    }
}

/* ------------------------------------------------------------------ */
/* One key, one event                                                  */
/* ------------------------------------------------------------------ */

static void TestTextPlain(void)
{
    InputTextPipeline pipeline;
    InputLayout layout;
    InputDiag diag;
    InputEvent events[RoboInputTextMaxEvents];
    InputTextOrigin origin = RoboInputTextOriginNone;
    InputEvent key;

    InputTextPipelineInit(&pipeline);
    RoboCheckEqInt(InputLayoutCreateBuiltin(&layout, RoboInputLayoutUsAscii, &diag), RoboOk);
    InputTextPipelineSetLayout(&pipeline, &layout, &diag);

    key = TextKeyEvent(RoboPhysicalKeyA, RoboInputModifiersNone, 4000u);
    RoboCheckEqInt((int)TextTranslate(&pipeline, &key, events, &origin), 1);
    RoboCheckEqInt((int)origin, (int)RoboInputTextOriginKey);
    CheckText(events, 1u, "a");
    RoboCheckEqInt((int)pipeline.producedTotal, 1);
    RoboCheckEqInt((int)pipeline.emptyTotal, 0);

    /* The event carries what happened *around* the character: the device, the timestamp, the modifiers, the
     * window and the display. A text event that lost the timestamp would be unorderable beside the key event
     * that produced it, which is what a replay needs. */
    RoboCheckEqInt((int)events[0].source, (int)RoboInputSourceKeyboard);
    RoboCheckEqInt((int)events[0].timestampMs, 4000);
    RoboCheckEqInt((int)events[0].window, 3);
    RoboCheckEqInt((int)events[0].device.id, (int)key.device.id);
    RoboCheckStrEq(events[0].device.name, key.device.name);
    RoboCheckEqInt((int)events[0].modifiers, (int)key.modifiers);
    /* And it names **no plane**: "a text event has no position" is the model's own rule, so it is asserted
     * through the model's validator (which `CheckText` already did) and by asking the position door. */
    {
        GfxPoint point;

        RoboCheckEqInt(InputEventPosition(&events[0], &point, &diag), RoboNotSupported);
    }

    /* A key the layout has no character for: a function key, an arrow, a modifier. The key event still
     * routes; it simply produces nothing, which is counted as an empty chord rather than as a failure. */
    key = TextKeyEvent(RoboPhysicalKeyF1, RoboInputModifiersNone, 4100u);
    RoboCheckEqInt((int)TextTranslate(&pipeline, &key, events, &origin), 0);
    RoboCheckEqInt((int)origin, (int)RoboInputTextOriginNone);
    RoboCheckEqInt((int)pipeline.emptyTotal, 1);
    RoboCheckEqInt((int)pipeline.producedTotal, 1);

    /* Space is a key like any other, and a real character: a pipeline that forgot it would type every word
     * run together. */
    key = TextKeyEvent(RoboPhysicalKeySpace, RoboInputModifiersNone, 4200u);
    RoboCheckEqInt((int)TextTranslate(&pipeline, &key, events, &origin), 1);
    CheckText(events, 1u, " ");
}

/* ------------------------------------------------------------------ */
/* Levels through the pipeline                                         */
/* ------------------------------------------------------------------ */

static void TestTextLevels(void)
{
    InputTextPipeline pipeline;
    InputLayout us;
    InputDiag diag;
    InputEvent events[RoboInputTextMaxEvents];
    InputTextOrigin origin = RoboInputTextOriginNone;

    InputTextPipelineInit(&pipeline);
    RoboCheckEqInt(InputLayoutCreateBuiltin(&us, RoboInputLayoutUsAscii, &diag), RoboOk);
    InputTextPipelineSetLayout(&pipeline, &us, &diag);

    {
        InputEvent key = TextKeyEvent(RoboPhysicalKeyA, RoboKeyModShift, 1u);

        RoboCheckEqInt((int)TextTranslate(&pipeline, &key, events, &origin), 1);
        CheckText(events, 1u, "A");
    }
    {
        InputEvent key = TextKeyEvent(RoboPhysicalKeyDigit1, RoboKeyModShift, 2u);

        RoboCheckEqInt((int)TextTranslate(&pipeline, &key, events, &origin), 1);
        CheckText(events, 1u, "!");
    }
    /* Caps lock is a case lock: it inverts shift for a letter... */
    {
        InputEvent key = TextKeyEvent(RoboPhysicalKeyA, RoboKeyModCapsLock, 3u);

        RoboCheckEqInt((int)TextTranslate(&pipeline, &key, events, &origin), 1);
        CheckText(events, 1u, "A");
    }
    /* ... and leaves punctuation alone, which is the rule that keeps `Shift+1` as `!`. */
    {
        InputEvent key = TextKeyEvent(RoboPhysicalKeyDigit1, RoboKeyModCapsLock | RoboKeyModShift, 4u);

        RoboCheckEqInt((int)TextTranslate(&pipeline, &key, events, &origin), 1);
        CheckText(events, 1u, "!");
    }
    /* ... and caps lock *with* shift is the base level again. */
    {
        InputEvent key = TextKeyEvent(RoboPhysicalKeyA, RoboKeyModCapsLock | RoboKeyModShift, 5u);

        RoboCheckEqInt((int)TextTranslate(&pipeline, &key, events, &origin), 1);
        CheckText(events, 1u, "a");
    }

    /* AltGr on a table with no third level: the key exists, the level does not, and no character is
     * produced. Not a refusal and not the base character twice. */
    {
        InputEvent key = TextKeyEvent(RoboPhysicalKeyE, RoboKeyModAltRight, 6u);

        RoboCheckEqInt((int)TextTranslate(&pipeline, &key, events, &origin), 0);
        RoboCheckEqInt((int)origin, (int)RoboInputTextOriginNone);
    }

    /* A control chord produces **nothing**: this is the rule that keeps `Ctrl+C` from firing the copy command
     * *and* inserting a `c` into the document. Meta is the same. */
    {
        uint64_t before = pipeline.emptyTotal;
        InputEvent ctrlA = TextKeyEvent(RoboPhysicalKeyC, RoboKeyModCtrl, 7u);
        InputEvent metaA = TextKeyEvent(RoboPhysicalKeyA, RoboKeyModMeta, 8u);
        InputEvent ctrlShift = TextKeyEvent(RoboPhysicalKeyA, RoboKeyModCtrl | RoboKeyModShift, 9u);

        RoboCheckEqInt((int)TextTranslate(&pipeline, &ctrlA, events, &origin), 0);
        RoboCheckEqInt((int)TextTranslate(&pipeline, &metaA, events, &origin), 0);
        RoboCheckEqInt((int)TextTranslate(&pipeline, &ctrlShift, events, &origin), 0);
        RoboCheckEqInt((int)(pipeline.emptyTotal - before), 3);
    }
    /* `Ctrl+Alt` is **AltGr** and therefore never a control chord: it is how two of the three platforms in
     * the world spell the third level, and a rule that treated it as a chord would make every AltGr character
     * unreachable. On the US table there is still no character at that level, which is the level's answer
     * rather than the chord rule's. */
    {
        InputEvent key = TextKeyEvent(RoboPhysicalKeyA, RoboKeyModCtrl | RoboKeyModAlt, 10u);

        RoboCheckEqInt((int)TextTranslate(&pipeline, &key, events, &origin), 0);
        RoboCheckEqInt(InputModifiersIsControlChord(RoboKeyModCtrl | RoboKeyModAlt), 0);
    }
}

/* ------------------------------------------------------------------ */
/* Dead keys: arm, compose, and what does not disturb the accent        */
/* ------------------------------------------------------------------ */

static void TestTextDeadKey(void)
{
    InputTextPipeline pipeline;
    InputLayout international;
    InputDiag diag;
    InputEvent events[RoboInputTextMaxEvents];
    InputTextOrigin origin = RoboInputTextOriginNone;

    InputTextPipelineInit(&pipeline);
    RoboCheckEqInt(InputLayoutCreateBuiltin(&international, RoboInputLayoutUsInternational, &diag),
                   RoboOk);
    InputTextPipelineSetLayout(&pipeline, &international, &diag);

    /* Arming: no text yet, and the pending accent is *reported* rather than forced into a wordless event. */
    {
        InputEvent key = TextKeyEvent(RoboPhysicalKeyQuote, RoboInputModifiersNone, 100u);

        RoboCheckEqInt((int)TextTranslate(&pipeline, &key, events, &origin), 0);
        RoboCheckEqInt((int)origin, (int)RoboInputTextOriginDeadKeyPending);
        RoboCheckEqInt((int)InputTextPipelinePendingDeadKey(&pipeline), (int)RoboInputDeadKeyAcute);
        RoboCheckEqInt((int)InputTextPipelineDeadKeyStartedMs(&pipeline), 100);
        RoboCheckEqInt((int)pipeline.composedTotal, 0);
    }

    /* A keystroke that means **no character** does not disturb it: pressing `F1` while an accent waits is not
     * a reason to abandon what the user typed. */
    {
        InputEvent f1 = TextKeyEvent(RoboPhysicalKeyF1, RoboInputModifiersNone, 150u);

        RoboCheckEqInt((int)TextTranslate(&pipeline, &f1, events, &origin), 0);
        RoboCheckEqInt((int)InputTextPipelinePendingDeadKey(&pipeline), (int)RoboInputDeadKeyAcute);
    }

    /* A **release** does not abandon it either: the accent waits for a down. */
    {
        InputEvent up = TextKeyUp(RoboPhysicalKeyQuote);

        RoboCheckEqInt((int)TextTranslate(&pipeline, &up, events, &origin), 0);
        RoboCheckEqInt((int)InputTextPipelinePendingDeadKey(&pipeline), (int)RoboInputDeadKeyAcute);
    }

    /* Composing: `acute` + `e` is one precomposed character, which is what the user sees and what the
     * document receives -- not an accent followed by a letter. */
    {
        InputEvent key = TextKeyEvent(RoboPhysicalKeyE, RoboInputModifiersNone, 200u);

        RoboCheckEqInt((int)TextTranslate(&pipeline, &key, events, &origin), 1);
        RoboCheckEqInt((int)origin, (int)RoboInputTextOriginComposed);
        CheckText(events, 1u, "\xC3\xA9");
        RoboCheckEqInt((int)InputTextPipelinePendingDeadKey(&pipeline), (int)RoboInputDeadKeyIdNone);
        RoboCheckEqInt((int)InputTextPipelineDeadKeyStartedMs(&pipeline), 0);
        RoboCheckEqInt((int)pipeline.composedTotal, 1);
    }

    /* The grave accent, with a shift level in play: `shift+backquote` is the tilde accent, and
     * `shift+grave`-then-`a` composes just as the base level does. */
    {
        InputEvent arm = TextKeyEvent(RoboPhysicalKeyBackquote, RoboInputModifiersNone, 300u);
        InputEvent key = TextKeyEvent(RoboPhysicalKeyA, RoboKeyModShift, 310u);

        RoboCheckEqInt((int)TextTranslate(&pipeline, &arm, events, &origin), 0);
        RoboCheckEqInt((int)InputTextPipelinePendingDeadKey(&pipeline), (int)RoboInputDeadKeyGrave);
        RoboCheckEqInt((int)TextTranslate(&pipeline, &key, events, &origin), 1);
        CheckText(events, 1u, "\xC3\x80"); /* À */
        RoboCheckEqInt((int)pipeline.composedTotal, 2);
    }

    /* The shifted accent: `shift+backquote` arms the tilde, and `n` composes with it. */
    {
        InputEvent arm = TextKeyEvent(RoboPhysicalKeyBackquote, RoboKeyModShift, 400u);
        InputEvent key = TextKeyEvent(RoboPhysicalKeyN, RoboInputModifiersNone, 410u);

        RoboCheckEqInt((int)TextTranslate(&pipeline, &arm, events, &origin), 0);
        RoboCheckEqInt((int)InputTextPipelinePendingDeadKey(&pipeline), (int)RoboInputDeadKeyTilde);
        RoboCheckEqInt((int)TextTranslate(&pipeline, &key, events, &origin), 1);
        CheckText(events, 1u, "\xC3\xB1"); /* ñ */
    }

    /* `shift+6` arms the circumflex, and the unshifted `6` is still a digit at its own level -- which is what
     * proves the layout's dead-key flag is per level and not per key. The pending accent *claims* the digit,
     * though, and `^` has no precomposed form with `6`, so what comes out is the accent and then the digit:
     * exactly what a US-International user sees typed. */
    {
        InputEvent arm = TextKeyEvent(RoboPhysicalKeyDigit6, RoboKeyModShift, 500u);
        InputEvent digit = TextKeyEvent(RoboPhysicalKeyDigit6, RoboInputModifiersNone, 510u);
        InputEvent twice = TextKeyEvent(RoboPhysicalKeyDigit6, RoboKeyModShift, 520u);

        RoboCheckEqInt((int)TextTranslate(&pipeline, &arm, events, &origin), 0);
        RoboCheckEqInt((int)InputTextPipelinePendingDeadKey(&pipeline),
                       (int)RoboInputDeadKeyCircumflex);
        RoboCheckEqInt((int)TextTranslate(&pipeline, &digit, events, &origin), 2);
        RoboCheckEqInt((int)origin, (int)RoboInputTextOriginDeadKeyRejected);
        RoboCheckStrEq(events[0].payload.text.text, "^");
        RoboCheckStrEq(events[1].payload.text.text, "6");
        RoboCheck(InputEventIsValid(&events[0]));
        RoboCheck(InputEventIsValid(&events[1]));
        RoboCheckEqInt((int)InputTextPipelinePendingDeadKey(&pipeline), (int)RoboInputDeadKeyIdNone);
        /* ... and with the accent gone, the shifted key arms it again. */
        RoboCheckEqInt((int)TextTranslate(&pipeline, &twice, events, &origin), 0);
        RoboCheckEqInt((int)InputTextPipelinePendingDeadKey(&pipeline),
                       (int)RoboInputDeadKeyCircumflex);
        RoboCheckEqInt(InputTextPipelineCancelDeadKey(&pipeline), 1);
    }
}

/* ------------------------------------------------------------------ */
/* The same accent twice                                               */
/* ------------------------------------------------------------------ */

static void TestTextRepeat(void)
{
    InputTextPipeline pipeline;
    InputLayout international;
    InputDiag diag;
    InputEvent events[RoboInputTextMaxEvents];
    InputTextOrigin origin = RoboInputTextOriginNone;

    InputTextPipelineInit(&pipeline);
    RoboCheckEqInt(InputLayoutCreateBuiltin(&international, RoboInputLayoutUsInternational, &diag),
                   RoboOk);
    InputTextPipelineSetLayout(&pipeline, &international, &diag);

    {
        InputEvent arm = TextKeyEvent(RoboPhysicalKeyQuote, RoboInputModifiersNone, 100u);
        InputEvent again = TextKeyEvent(RoboPhysicalKeyQuote, RoboInputModifiersNone, 120u);

        RoboCheckEqInt((int)TextTranslate(&pipeline, &arm, events, &origin), 0);
        RoboCheckEqInt((int)TextTranslate(&pipeline, &again, events, &origin), 1);
        RoboCheckEqInt((int)origin, (int)RoboInputTextOriginDeadKeyRepeated);
        /* The **spacing** form, which is how a user types `´` on a keyboard whose only acute is a dead one.
         * A pipeline that emitted the combining mark here would put a floating accent on the page. */
        CheckText(events, 1u, "\xC2\xB4");
        RoboCheckEqInt((int)InputTextPipelinePendingDeadKey(&pipeline), (int)RoboInputDeadKeyIdNone);
        RoboCheckEqInt((int)pipeline.composedTotal, 0);
        RoboCheckEqInt((int)pipeline.rejectedTotal, 0);
    }

    /* The diaeresis, on the shifted level of the same key, behaves the same way. */
    {
        InputEvent arm = TextKeyEvent(RoboPhysicalKeyQuote, RoboKeyModShift, 200u);
        InputEvent again = TextKeyEvent(RoboPhysicalKeyQuote, RoboKeyModShift, 220u);

        RoboCheckEqInt((int)TextTranslate(&pipeline, &arm, events, &origin), 0);
        RoboCheckEqInt((int)InputTextPipelinePendingDeadKey(&pipeline),
                       (int)RoboInputDeadKeyDiaeresis);
        RoboCheckEqInt((int)TextTranslate(&pipeline, &again, events, &origin), 1);
        CheckText(events, 1u, "\xC2\xA8");
    }
}

/* ------------------------------------------------------------------ */
/* A different accent while one waits                                  */
/* ------------------------------------------------------------------ */

static void TestTextReplace(void)
{
    InputTextPipeline pipeline;
    InputLayout international;
    InputDiag diag;
    InputEvent events[RoboInputTextMaxEvents];
    InputTextOrigin origin = RoboInputTextOriginNone;

    InputTextPipelineInit(&pipeline);
    RoboCheckEqInt(InputLayoutCreateBuiltin(&international, RoboInputLayoutUsInternational, &diag),
                   RoboOk);
    InputTextPipelineSetLayout(&pipeline, &international, &diag);

    {
        InputEvent acute = TextKeyEvent(RoboPhysicalKeyQuote, RoboInputModifiersNone, 100u);
        InputEvent grave = TextKeyEvent(RoboPhysicalKeyBackquote, RoboInputModifiersNone, 130u);
        InputEvent base = TextKeyEvent(RoboPhysicalKeyA, RoboInputModifiersNone, 160u);

        RoboCheckEqInt((int)TextTranslate(&pipeline, &acute, events, &origin), 0);
        RoboCheckEqInt((int)TextTranslate(&pipeline, &grave, events, &origin), 1);
        RoboCheckEqInt((int)origin, (int)RoboInputTextOriginDeadKeyReplaced);
        /* The accent that could compose with nothing becomes its own character -- it is not dropped -- and
         * the *new* accent takes over the waiting. */
        CheckText(events, 1u, "\xC2\xB4");
        RoboCheckEqInt((int)InputTextPipelinePendingDeadKey(&pipeline), (int)RoboInputDeadKeyGrave);
        RoboCheckEqInt((int)InputTextPipelineDeadKeyStartedMs(&pipeline), 130);
        RoboCheckEqInt((int)pipeline.rejectedTotal, 1);

        /* ... and the one that replaced it composes normally. */
        RoboCheckEqInt((int)TextTranslate(&pipeline, &base, events, &origin), 1);
        CheckText(events, 1u, "\xC3\xA0"); /* à */
        RoboCheckEqInt((int)InputTextPipelinePendingDeadKey(&pipeline), (int)RoboInputDeadKeyIdNone);
        RoboCheckEqInt((int)pipeline.composedTotal, 1);
    }
}

/* ------------------------------------------------------------------ */
/* An accent that cannot compose                                       */
/* ------------------------------------------------------------------ */

static void TestTextReject(void)
{
    InputTextPipeline pipeline;
    InputLayout international;
    InputDiag diag;
    InputEvent events[RoboInputTextMaxEvents];
    InputTextOrigin origin = RoboInputTextOriginNone;

    InputTextPipelineInit(&pipeline);
    RoboCheckEqInt(InputLayoutCreateBuiltin(&international, RoboInputLayoutUsInternational, &diag),
                   RoboOk);
    InputTextPipelineSetLayout(&pipeline, &international, &diag);

    /* `acute` + `q`: there is no precomposed form, and what the user typed is the acute **and then** the
     * letter. Two events, in that order -- one event would have to concatenate them into a fragment the
     * event's bound might refuse, or drop one of the two characters. */
    {
        InputEvent arm = TextKeyEvent(RoboPhysicalKeyQuote, RoboInputModifiersNone, 100u);
        InputEvent q = TextKeyEvent(RoboPhysicalKeyQ, RoboInputModifiersNone, 140u);

        RoboCheckEqInt((int)TextTranslate(&pipeline, &arm, events, &origin), 0);
        RoboCheckEqInt((int)TextTranslate(&pipeline, &q, events, &origin), 2);
        RoboCheckEqInt((int)origin, (int)RoboInputTextOriginDeadKeyRejected);
        RoboCheckEqInt((int)events[0].type, (int)RoboInputEventTextInput);
        RoboCheckEqInt((int)events[1].type, (int)RoboInputEventTextInput);
        RoboCheckStrEq(events[0].payload.text.text, "\xC2\xB4");
        RoboCheckStrEq(events[1].payload.text.text, "q");
        /* Both are the user's input: two events and one counter, and the second one carries the *same*
         * timestamp as the first because one keystroke produced both. */
        RoboCheckEqInt((int)events[0].timestampMs, 140);
        RoboCheckEqInt((int)events[1].timestampMs, 140);
        RoboCheck(InputEventIsValid(&events[0]));
        RoboCheck(InputEventIsValid(&events[1]));
        RoboCheckEqInt((int)InputTextPipelinePendingDeadKey(&pipeline), (int)RoboInputDeadKeyIdNone);
        RoboCheckEqInt((int)pipeline.rejectedTotal, 1);
        RoboCheckEqInt((int)pipeline.composedTotal, 0);
    }

    /* `tilde` + `e` has no form either (the table says so rather than the code deriving it), and the digit
     * row is where a US-International user meets it every day: `~` then `1`. */
    {
        InputEvent arm = TextKeyEvent(RoboPhysicalKeyBackquote, RoboKeyModShift, 200u);
        InputEvent one = TextKeyEvent(RoboPhysicalKeyDigit1, RoboInputModifiersNone, 240u);

        RoboCheckEqInt((int)TextTranslate(&pipeline, &arm, events, &origin), 0);
        RoboCheckEqInt((int)TextTranslate(&pipeline, &one, events, &origin), 2);
        RoboCheckStrEq(events[0].payload.text.text, "~");
        RoboCheckStrEq(events[1].payload.text.text, "1");
    }
}

/* ------------------------------------------------------------------ */
/* Cancelling                                                          */
/* ------------------------------------------------------------------ */

static void TestTextCancel(void)
{
    InputTextPipeline pipeline;
    InputLayout international;
    InputDiag diag;
    InputEvent events[RoboInputTextMaxEvents];
    InputTextOrigin origin = RoboInputTextOriginNone;

    InputTextPipelineInit(&pipeline);
    RoboCheckEqInt(InputLayoutCreateBuiltin(&international, RoboInputLayoutUsInternational, &diag),
                   RoboOk);
    InputTextPipelineSetLayout(&pipeline, &international, &diag);

    /* Cancelling is what focus loss, `Escape` and a device disconnect call: the accent is dropped and the
     * *next* field's first character is not modified by a keystroke typed into this one. */
    {
        InputEvent arm = TextKeyEvent(RoboPhysicalKeyQuote, RoboInputModifiersNone, 100u);
        InputEvent e = TextKeyEvent(RoboPhysicalKeyE, RoboInputModifiersNone, 200u);

        RoboCheckEqInt((int)TextTranslate(&pipeline, &arm, events, &origin), 0);
        RoboCheckEqInt(InputTextPipelineCancelDeadKey(&pipeline), 1);
        RoboCheckEqInt((int)InputTextPipelinePendingDeadKey(&pipeline), (int)RoboInputDeadKeyIdNone);
        RoboCheckEqInt((int)InputTextPipelineDeadKeyStartedMs(&pipeline), 0);
        /* Nothing was emitted by the cancellation itself... */
        RoboCheckEqInt((int)pipeline.producedTotal, 0);
        /* ... and the following letter is a plain letter. */
        RoboCheckEqInt((int)TextTranslate(&pipeline, &e, events, &origin), 1);
        RoboCheckEqInt((int)origin, (int)RoboInputTextOriginKey);
        CheckText(events, 1u, "e");
        /* Cancelling when nothing waits is reported rather than assumed. */
        RoboCheckEqInt(InputTextPipelineCancelDeadKey(&pipeline), 0);
        RoboCheckEqInt(InputTextPipelineCancelDeadKey(&pipeline), 0);
    }
}

/* ------------------------------------------------------------------ */
/* The dump                                                            */
/* ------------------------------------------------------------------ */

static void TestTextDump(void)
{
    InputTextPipeline pipeline;
    InputLayout international;
    InputDiag diag;
    InputEvent events[RoboInputTextMaxEvents];
    InputTextOrigin origin = RoboInputTextOriginNone;
    static char first[RoboInputTextPipelineDumpMax];
    static char second[RoboInputTextPipelineDumpMax];
    size_t firstLength = 0u;
    size_t secondLength = 0u;

    InputTextPipelineInit(&pipeline);
    RoboCheckEqInt(InputLayoutCreateBuiltin(&international, RoboInputLayoutUsInternational, &diag),
                   RoboOk);
    InputTextPipelineSetLayout(&pipeline, &international, &diag);

    /* A specific sequence, counted by hand so the line is a fact rather than a snapshot: arm (empty), compose
     * (produced+composed), a plain letter (produced), an unmapped key (empty), arm again (empty), the same
     * accent twice (produced), a letter (produced), and a control chord (empty). */
    {
        InputEvent arm = TextKeyEvent(RoboPhysicalKeyQuote, RoboInputModifiersNone, 1u);
        InputEvent e = TextKeyEvent(RoboPhysicalKeyE, RoboInputModifiersNone, 2u);
        InputEvent a = TextKeyEvent(RoboPhysicalKeyA, RoboInputModifiersNone, 3u);
        InputEvent f1 = TextKeyEvent(RoboPhysicalKeyF1, RoboInputModifiersNone, 4u);
        InputEvent arm2 = TextKeyEvent(RoboPhysicalKeyQuote, RoboInputModifiersNone, 5u);
        InputEvent twice = TextKeyEvent(RoboPhysicalKeyQuote, RoboInputModifiersNone, 6u);
        InputEvent q = TextKeyEvent(RoboPhysicalKeyQ, RoboInputModifiersNone, 7u);
        InputEvent ctrl = TextKeyEvent(RoboPhysicalKeyA, RoboKeyModCtrl, 8u);

        RoboCheckEqInt((int)TextTranslate(&pipeline, &arm, events, &origin), 0);
        RoboCheckEqInt((int)TextTranslate(&pipeline, &e, events, &origin), 1);
        RoboCheckEqInt((int)TextTranslate(&pipeline, &a, events, &origin), 1);
        RoboCheckEqInt((int)TextTranslate(&pipeline, &f1, events, &origin), 0);
        RoboCheckEqInt((int)TextTranslate(&pipeline, &arm2, events, &origin), 0);
        RoboCheckEqInt((int)TextTranslate(&pipeline, &twice, events, &origin), 1);
        RoboCheckEqInt((int)TextTranslate(&pipeline, &q, events, &origin), 1);
        RoboCheckEqInt((int)TextTranslate(&pipeline, &ctrl, events, &origin), 0);
    }

    RoboCheckEqInt(InputTextPipelineWrite(&pipeline, first, sizeof(first), &firstLength, &diag), RoboOk);
    RoboCheckStrEq(first,
                   "text layout=us-international dead=none produced=4 empty=4 composed=1 rejected=0\n");

    /* Byte-for-byte determinism, which is what a replay of a recording needs, and the narrow buffer
     * answering `?` rather than a prefix that reads as a whole line. */
    RoboCheckEqInt(InputTextPipelineWrite(&pipeline, second, sizeof(second), &secondLength, &diag), RoboOk);
    RoboCheckEqInt((int)secondLength, (int)firstLength);
    RoboCheck(strcmp(first, second) == 0);
    {
        char small[16];
        size_t needed = 0u;

        RoboCheckEqInt(InputTextPipelineWrite(&pipeline, small, sizeof(small), &needed, &diag),
                       RoboNotSupported);
        RoboCheck(needed >= firstLength);
        RoboCheckStrEq(small, "?");
        RoboCheckEqInt(diag.domain, RoboInputDomainDump);
    }

    /* An accent left waiting shows up in the line, which is the whole reason the field is there: a
     * diagnostics page that could not say "an accent is pending" would report a keystroke that did
     * nothing. */
    {
        InputEvent arm = TextKeyEvent(RoboPhysicalKeyQuote, RoboInputModifiersNone, 9u);

        RoboCheckEqInt((int)TextTranslate(&pipeline, &arm, events, &origin), 0);
        RoboCheckEqInt(InputTextPipelineWrite(&pipeline, first, sizeof(first), &firstLength, &diag), RoboOk);
        RoboCheck(strncmp(first, "text layout=us-international dead=acute ", 39u) == 0);
    }
}

void TestRegisterInputText(void)
{
    TestRegister("input.text.vocabulary", TestTextVocabulary);
    TestRegister("input.text.init", TestTextInit);
    TestRegister("input.text.kinds", TestTextKinds);
    TestRegister("input.text.plain", TestTextPlain);
    TestRegister("input.text.levels", TestTextLevels);
    TestRegister("input.text.deadkey", TestTextDeadKey);
    TestRegister("input.text.repeat", TestTextRepeat);
    TestRegister("input.text.replace", TestTextReplace);
    TestRegister("input.text.reject", TestTextReject);
    TestRegister("input.text.cancel", TestTextCancel);
    TestRegister("input.text.dump", TestTextDump);
}
