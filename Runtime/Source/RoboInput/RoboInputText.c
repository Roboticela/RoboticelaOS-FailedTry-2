/*
 * RoboInput text pipeline (Phase 13.11/13.10/13.8) — see `RoboInputText.h`.
 *
 * One function does the work and the rest of the file is the vocabulary and the bookkeeping around it. The
 * function is worth reading in one piece, because the order of its questions *is* the pipeline:
 *
 *   1. is this a key event at all, and is it an up? (an up produces nothing, ever)
 *   2. does the layout map this key **at all**? -- asked at the base level, because "is this key cased?" is
 *      a property of the key and not of the level, and the level is what caps lock changes;
 *   3. is the chord a *control* chord? (then it is a command, and producing a character would double-fire)
 *   4. which level, and does the layout have a mapping for it? (AltGr on a US table does not)
 *   5. is an accent pending? (then compose, repeat, replace or reject)
 *   6. otherwise, does the mapping produce text or *arm* an accent?
 *
 * Every "no" in that list is an honest empty answer rather than an error, which is what lets a caller hand
 * **every** key event to this door and let the ones that mean nothing fall through.
 *
 * The pending accent's **spacing form** is encoded here, on demand, through RoboUnicode's own encoder --
 * there is no second UTF-8 encoder in this tree, and a dead key's spacing character is a scalar rather than
 * a string so that the layout table cannot spell it two ways.
 */

#include "RoboInput/RoboInputText.h"

#include "RoboInputInternal.h"

#include <stddef.h>
#include <stdint.h>

/* ------------------------------------------------------------------ */
/* Vocabulary                                                          */
/* ------------------------------------------------------------------ */

const char *InputTextOriginName(InputTextOrigin origin)
{
    switch (origin) {
    case RoboInputTextOriginNone: return "none";
    case RoboInputTextOriginKey: return "key";
    case RoboInputTextOriginDeadKeyRepeated: return "dead-key-repeated";
    case RoboInputTextOriginComposed: return "composed";
    case RoboInputTextOriginDeadKeyRejected: return "dead-key-rejected";
    case RoboInputTextOriginDeadKeyPending: return "dead-key-pending";
    case RoboInputTextOriginDeadKeyReplaced: return "dead-key-replaced";
    default: break;
    }
    return "unknown";
}

/* ------------------------------------------------------------------ */
/* Construction                                                        */
/* ------------------------------------------------------------------ */

void InputTextPipelineInit(InputTextPipeline *pipeline)
{
    if (!pipeline) {
        return;
    }
    pipeline->layout = NULL;
    pipeline->dead.id = RoboInputDeadKeyIdNone;
    pipeline->dead.startedMs = 0u;
    pipeline->producedTotal = 0u;
    pipeline->emptyTotal = 0u;
    pipeline->composedTotal = 0u;
    pipeline->rejectedTotal = 0u;
}

Result InputTextPipelineSetLayout(InputTextPipeline *pipeline,
                                 const InputLayout *layout,
                                 InputDiag *diag)
{
    if (!pipeline) {
        return RoboInputDiag(diag, RoboInputDomainEvent, RoboInvalidArgument,
                             "no pipeline to point at a layout");
    }
    /* NULL is a legitimate layout: it means every key produces nothing, which is what a session in a
     * pre-login state or in a field that takes no text wants without a second code path. */
    pipeline->layout = layout;
    return RoboOk;
}

InputDeadKeyId InputTextPipelinePendingDeadKey(const InputTextPipeline *pipeline)
{
    return pipeline ? pipeline->dead.id : RoboInputDeadKeyIdNone;
}

uint64_t InputTextPipelineDeadKeyStartedMs(const InputTextPipeline *pipeline)
{
    if (!pipeline || pipeline->dead.id == RoboInputDeadKeyIdNone) {
        return 0u;
    }
    return pipeline->dead.startedMs;
}

int InputTextPipelineCancelDeadKey(InputTextPipeline *pipeline)
{
    int had;

    if (!pipeline) {
        return 0;
    }
    had = pipeline->dead.id != RoboInputDeadKeyIdNone;
    pipeline->dead.id = RoboInputDeadKeyIdNone;
    pipeline->dead.startedMs = 0u;
    return had;
}

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

/** The scalar a mapping's text begins with, or 0 for empty or ill-formed text. */
static Uchar32 TextFirstScalar(const char *text)
{
    Uchar32 scalar = (Uchar32)0u;
    size_t next = 0u;

    if (!text || text[0] == '\0') {
        return (Uchar32)0u;
    }
    if (UnicodeUtf8Decode(text, RiBoundedStrlen(text, RoboInputTextMax), 0u, RoboUnicodeStrict,
                          &scalar, &next) != RoboUnicodeOk) {
        return (Uchar32)0u;
    }
    return scalar;
}

/** The pending accent's spacing form as UTF-8 in @p out; empty when it cannot be encoded (it never cannot). */
static void TextSpacingForm(uint32_t deadKey, char *out, size_t capacity)
{
    Uchar32 spacing = InputDeadKeySpacingScalar(deadKey);
    size_t written = 0u;

    out[0] = '\0';
    if (spacing == (Uchar32)0u || capacity == 0u) {
        return;
    }
    if (UnicodeUtf8Encode(spacing, out, capacity - 1u, &written) != RoboUnicodeOk) {
        out[0] = '\0';
        return;
    }
    out[written] = '\0';
}

/** Encode @p scalar into @p out (a whole buffer); empty when it does not fit. */
static void TextScalarForm(Uchar32 scalar, char *out, size_t capacity)
{
    size_t written = 0u;

    out[0] = '\0';
    if (scalar == (Uchar32)0u || capacity == 0u) {
        return;
    }
    if (UnicodeUtf8Encode(scalar, out, capacity - 1u, &written) != RoboUnicodeOk) {
        out[0] = '\0';
        return;
    }
    out[written] = '\0';
}

/**
 * Build one `TEXT_INPUT` event at @p index, inheriting the key event's common fields.
 *
 * The inheritance is the reason this helper exists rather than each branch filling four fields: a text event
 * that forgot the modifiers would still *validate*, and the loss would only show up as a clipboard paste
 * without the shift it was typed with.
 */
static Result TextEmit(InputEvent *outEvents,
                       uint32_t index,
                       const InputEvent *keyEvent,
                       const char *text,
                       InputDiag *diag)
{
    InputEventInit(&outEvents[index]);
    if (InputEventSetText(&outEvents[index], keyEvent->source, text, diag) != RoboOk) {
        return diag ? diag->code : RoboError;
    }
    InputEventSetDevice(&outEvents[index], &keyEvent->device, NULL);
    InputEventSetTimestamp(&outEvents[index], keyEvent->timestampMs);
    InputEventSetModifiers(&outEvents[index], keyEvent->modifiers, NULL);
    InputEventSetWindow(&outEvents[index], keyEvent->window);
    InputEventSetDisplay(&outEvents[index], keyEvent->display);
    return RoboOk;
}

/**
 * Report what happened, whether or not it produced an event. Safe on a NULL pointer.
 *
 * **One entry, and it is the chord's**: the origin is a property of what the user pressed rather than of the
 * events that came out, and both events of a rejected accent share it. Writing one per event would make
 * every caller pass an array to answer a question that has one answer -- and a caller who passed a scalar
 * would have the second write land on whatever the compiler put beside it.
 */
static void TextReport(InputTextOrigin *outOrigin, InputTextOrigin origin)
{
    if (outOrigin) {
        *outOrigin = origin;
    }
}

/* ------------------------------------------------------------------ */
/* The pipeline                                                        */
/* ------------------------------------------------------------------ */

Result InputTextPipelineTranslate(InputTextPipeline *pipeline,
                                  const InputEvent *keyEvent,
                                  InputEvent *outEvents,
                                  uint32_t outCapacity,
                                  uint32_t *outCount,
                                  InputTextOrigin *outOrigins,
                                  InputDiag *diag)
{
    InputLayoutMapping baseMapping;
    InputLayoutMapping levelMapping;
    uint32_t produced = 0u;

    if (!pipeline || !keyEvent || !outEvents || !outCount) {
        return RoboInputDiag(diag, RoboInputDomainEvent, RoboInvalidArgument,
                             "a pipeline, a key event, somewhere for the events and somewhere for the "
                             "count are all needed");
    }
    *outCount = 0u;
    if (outCapacity < RoboInputTextMaxEvents) {
        /* Refused rather than truncated: one chord may produce two events, and a caller with room for one
         * would silently lose half of what the user typed. */
        return RoboInputDiag(diag, RoboInputDomainEvent, RoboInvalidArgument,
                             "the output array must hold RoboInputTextMaxEvents events");
    }
    if (!InputEventTypeIsKey(keyEvent->type)) {
        return RoboInputDiag(diag, RoboInputDomainEvent, RoboInvalidArgument,
                             "only a key event produces text");
    }
    /* An up produces nothing, and is accepted rather than refused so a caller can hand **every** key event
     * to this one door. It does not touch the pending accent: the accent waits for a *down*, and releasing
     * the key that armed it is not a reason to abandon it. */
    if (keyEvent->type == RoboInputEventKeyUp) {
        TextReport(outOrigins, RoboInputTextOriginNone);
        return RoboOk;
    }

    /* --- 2/3. does this key mean anything at all, and is it a chord? -- */
    if (pipeline->layout == NULL ||
        InputLayoutLookup(pipeline->layout, keyEvent->payload.keyboard.physical,
                          RoboInputLayoutLevelBase, &baseMapping, NULL) != RoboOk) {
        pipeline->emptyTotal++;
        TextReport(outOrigins, RoboInputTextOriginNone);
        return RoboOk;
    }
    if (InputModifiersIsControlChord(keyEvent->modifiers)) {
        pipeline->emptyTotal++;
        TextReport(outOrigins, RoboInputTextOriginNone);
        return RoboOk;
    }

    /* --- 4. which level, and does the layout have one there? -------- */
    {
        InputLayoutLevel level = InputLayoutLevelFor(keyEvent->modifiers,
                                                     InputLayoutMappingIsCased(&baseMapping));

        if (InputLayoutLookup(pipeline->layout, keyEvent->payload.keyboard.physical, level,
                              &levelMapping, NULL) != RoboOk) {
            /* The key exists but not at this level: AltGr on a table with no third level is the everyday
             * case, and it means no character rather than a failure. */
            pipeline->emptyTotal++;
            TextReport(outOrigins, RoboInputTextOriginNone);
            return RoboOk;
        }
    }

    /* --- 5. is an accent pending? ---------------------------------- */
    if (pipeline->dead.id != RoboInputDeadKeyIdNone) {
        uint32_t pending = (uint32_t)pipeline->dead.id;
        char spacing[RoboInputTextMax];

        TextSpacingForm(pending, spacing, sizeof(spacing));
        pipeline->dead.id = RoboInputDeadKeyIdNone;
        pipeline->dead.startedMs = 0u;

        if (levelMapping.deadKey == pending) {
            /* The same accent again: the user wants the accent itself, which is how `´` is typed on a
             * keyboard whose only acute is a dead one. */
            if (TextEmit(outEvents, 0u, keyEvent, spacing, diag) != RoboOk) {
                return diag ? diag->code : RoboError;
            }
            produced = 1u;
            TextReport(outOrigins, RoboInputTextOriginDeadKeyRepeated);
        } else if (levelMapping.deadKey != (uint32_t)RoboInputDeadKeyNone) {
            /* A *different* accent: the old one could not compose with a dead key, so it becomes its own
             * character and the new accent takes over the waiting. */
            pipeline->dead.id = (InputDeadKeyId)levelMapping.deadKey;
            pipeline->dead.startedMs = keyEvent->timestampMs;
            pipeline->rejectedTotal++;
            if (TextEmit(outEvents, 0u, keyEvent, spacing, diag) != RoboOk) {
                return diag ? diag->code : RoboError;
            }
            produced = 1u;
            TextReport(outOrigins, RoboInputTextOriginDeadKeyReplaced);
        } else {
            Uchar32 composed = InputDeadKeyCompose(pending, TextFirstScalar(levelMapping.text));

            if (composed != (Uchar32)0u) {
                char composedText[RoboInputTextMax];

                TextScalarForm(composed, composedText, sizeof(composedText));
                if (composedText[0] == '\0') {
                    /* A scalar the encoder refused is a table defect and not a user's problem: the accent
                     * is dropped and the base still goes through by the fallback below. */
                    composed = (Uchar32)0u;
                }
                if (composed != (Uchar32)0u) {
                    pipeline->composedTotal++;
                    if (TextEmit(outEvents, 0u, keyEvent, composedText, diag) != RoboOk) {
                        return diag ? diag->code : RoboError;
                    }
                    produced = 1u;
                    TextReport(outOrigins, RoboInputTextOriginComposed);
                }
            }
            if (produced == 0u) {
                /* No precomposed form: the accent's own character, **then** what the user typed. Two events
                 * rather than one, because the alternative is concatenating into a fragment the event's
                 * bound might refuse or dropping one of the two characters -- both of which lose input. */
                pipeline->rejectedTotal++;
                if (spacing[0] != '\0') {
                    if (TextEmit(outEvents, 0u, keyEvent, spacing, diag) != RoboOk) {
                        return diag ? diag->code : RoboError;
                    }
                    produced = 1u;
                    TextReport(outOrigins, RoboInputTextOriginDeadKeyRejected);
                }
                if (levelMapping.text[0] != '\0') {
                    uint32_t index = produced;

                    if (TextEmit(outEvents, index, keyEvent, levelMapping.text, diag) != RoboOk) {
                        return diag ? diag->code : RoboError;
                    }
                    produced++;
                    TextReport(outOrigins, RoboInputTextOriginDeadKeyRejected);
                }
            }
        }

        if (produced == 0u) {
            pipeline->emptyTotal++;
            TextReport(outOrigins, RoboInputTextOriginNone);
            return RoboOk;
        }
        pipeline->producedTotal++;
        *outCount = produced;
        return RoboOk;
    }

    /* --- 6. nothing is pending ------------------------------------- */
    if (levelMapping.deadKey != (uint32_t)RoboInputDeadKeyNone) {
        /* Armed and **nothing emitted**: there is no character yet, and pretending there is would insert the
         * accent before the letter it modifies. */
        pipeline->dead.id = (InputDeadKeyId)levelMapping.deadKey;
        pipeline->dead.startedMs = keyEvent->timestampMs;
        pipeline->emptyTotal++;
        TextReport(outOrigins, RoboInputTextOriginDeadKeyPending);
        return RoboOk;
    }
    if (levelMapping.text[0] == '\0') {
        pipeline->emptyTotal++;
        TextReport(outOrigins, RoboInputTextOriginNone);
        return RoboOk;
    }

    if (TextEmit(outEvents, 0u, keyEvent, levelMapping.text, diag) != RoboOk) {
        return diag ? diag->code : RoboError;
    }
    pipeline->producedTotal++;
    *outCount = 1u;
    TextReport(outOrigins, RoboInputTextOriginKey);
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* The dump                                                            */
/* ------------------------------------------------------------------ */

Result InputTextPipelineWrite(const InputTextPipeline *pipeline,
                              char *buffer,
                              size_t capacity,
                              size_t *outLength,
                              InputDiag *diag)
{
    InputWriter text;

    if (!RiWriterArgumentsAreValid(buffer, capacity, outLength)) {
        return RoboInputDiag(diag, RoboInputDomainDump, RoboInvalidArgument,
                             "a dump needs a buffer, a capacity and somewhere to put the length");
    }
    RiWriterInit(&text, buffer, capacity);
    if (!pipeline) {
        RiWriterText(&text, "text <none>\n");
        return RiWriterFinish(&text, outLength, diag);
    }

    RiWriterText(&text, "text layout=");
    RiWriterText(&text, pipeline->layout ? pipeline->layout->name : "none");
    RiWriterFieldText(&text, " dead",
                      pipeline->dead.id == RoboInputDeadKeyIdNone
                          ? "none"
                          : InputDeadKeyName((uint32_t)pipeline->dead.id));
    RiWriterFieldU64(&text, " produced", pipeline->producedTotal);
    RiWriterFieldU64(&text, " empty", pipeline->emptyTotal);
    RiWriterFieldU64(&text, " composed", pipeline->composedTotal);
    RiWriterFieldU64(&text, " rejected", pipeline->rejectedTotal);
    RiWriterChar(&text, '\n');
    return RiWriterFinish(&text, outLength, diag);
}
