/*
 * The cursor (Phase 13.43) — see `RoboInputCursor.h`.
 *
 * One table (`InputCursorForTarget`) and one owner (the manager). The manager's whole job is to make the shape
 * a function of the hit test rather than of the path the pointer took to get there.
 */

#include "RoboInput/RoboInputCursor.h"

#include "RoboInputInternal.h"

#include <string.h>

static const char *const RiCursorNames[RoboInputCursorTypeCount] = {
    "default",     "pointer",    "text",      "crosshair",
    "move",        "resize-h",   "resize-v",  "resize-diagonal",
    "not-allowed", "wait",       "progress",  "none",
    "grabbing"
};

int InputCursorTypeIsKnown(InputCursorType type)
{
    return (int)type >= 0 && (size_t)type < (size_t)RoboInputCursorTypeCount;
}

const char *InputCursorTypeName(InputCursorType type)
{
    if (!InputCursorTypeIsKnown(type)) {
        return "unknown";
    }
    return RiCursorNames[(size_t)type];
}

int InputCursorTypeIsHidden(InputCursorType type)
{
    return type == RoboInputCursorNone;
}

InputCursorType InputCursorForTarget(const InputTarget *target)
{
    if (!target || !InputTargetIsValid(target)) {
        /* A cursor is always somewhere on a screen, so "about nothing" is still the arrow. */
        return RoboInputCursorDefault;
    }
    if (target->kind != RoboInputTargetDocument) {
        /* A window's chrome and an application's own frame are the shell's to decide, and it declares an
         * override where it cares. */
        return RoboInputCursorDefault;
    }
    if (InputTargetHasFlag(target, (uint32_t)RoboInputTargetFlagText)) {
        return RoboInputCursorText;
    }
    if (InputTargetHasFlag(target, (uint32_t)RoboInputTargetFlagContent)) {
        /* A content node that is not text: a picture, a canvas, a layout box. The arrow, because "something
         * graphical" has no one shape. */
        return RoboInputCursorDefault;
    }
    return RoboInputCursorDefault;
}

void InputCursorManagerInit(InputCursorManager *manager)
{
    if (!manager) {
        return;
    }
    memset(manager, 0, sizeof(*manager));
    manager->current = RoboInputCursorDefault;
    InputTargetInit(&manager->source);
}

static InputCursorOverride *RiCursorFind(InputCursorManager *manager, const InputTarget *target)
{
    uint32_t index;

    if (!manager || !target) {
        return NULL;
    }
    for (index = 0u; index < RoboInputMaxCursorOverrides; index++) {
        if (manager->overrides[index].active &&
            InputTargetEqual(&manager->overrides[index].target, target)) {
            return &manager->overrides[index];
        }
    }
    return NULL;
}

Result InputCursorManagerSetOverride(InputCursorManager *manager,
                                     const InputTarget *target,
                                     InputCursorType type,
                                     InputDiag *diag)
{
    InputCursorOverride *slot;
    uint32_t index;

    if (!manager) {
        return RoboInputDiag(diag, RoboInputDomainCursor, RoboInvalidArgument,
                             "a cursor manager is required");
    }
    if (!InputTargetIsValid(target)) {
        manager->refused++;
        return RoboInputDiag(diag, RoboInputDomainCursor, RoboInvalidArgument,
                             "only a valid target declares a cursor");
    }
    if (!InputCursorTypeIsKnown(type)) {
        manager->refused++;
        return RoboInputDiag(diag, RoboInputDomainCursor, RoboInvalidArgument,
                             "a declared cursor names a defined shape");
    }

    slot = RiCursorFind(manager, target);
    if (!slot) {
        for (index = 0u; index < RoboInputMaxCursorOverrides; index++) {
            if (!manager->overrides[index].active) {
                slot = &manager->overrides[index];
                break;
            }
        }
    }
    if (!slot) {
        manager->refused++;
        return RoboInputDiag(diag, RoboInputDomainCursor, RoboOutOfMemory,
                             "the override table is full");
    }
    if (!slot->active) {
        slot->active = 1;
        manager->count++;
    }
    slot->target = *target;
    slot->type = type;
    if (diag) {
        InputDiagReset(diag);
    }
    return RoboOk;
}

int InputCursorManagerRemoveOverride(InputCursorManager *manager, const InputTarget *target)
{
    InputCursorOverride *slot = RiCursorFind(manager, target);

    if (!slot) {
        return 0;
    }
    memset(slot, 0, sizeof(*slot));
    if (manager->count > 0u) {
        manager->count--;
    }
    return 1;
}

InputCursorType InputCursorManagerUpdate(InputCursorManager *manager,
                                         const InputTarget *target,
                                         int *acceptedThisTime)
{
    InputCursorType decided;
    const InputCursorOverride *override;

    if (acceptedThisTime) {
        *acceptedThisTime = 0;
    }
    if (!manager) {
        return RoboInputCursorDefault;
    }

    decided = InputCursorForTarget(target);
    override = RiCursorFind(manager, target);
    if (override) {
        /* The caller's declaration wins over the default table, which is what makes the table a default
         * rather than a law. */
        decided = override->type;
    }

    if (decided == manager->current) {
        manager->unchanged++;
        return manager->current;
    }
    manager->current = decided;
    if (target) {
        manager->source = *target;
        manager->hasTarget = 1;
    } else {
        InputTargetInit(&manager->source);
        manager->hasTarget = 0;
    }
    manager->accepted++;
    if (acceptedThisTime) {
        *acceptedThisTime = 1;
    }
    return manager->current;
}

InputCursorType InputCursorManagerCurrent(const InputCursorManager *manager)
{
    return manager ? manager->current : RoboInputCursorDefault;
}

const InputTarget *InputCursorManagerSource(const InputCursorManager *manager)
{
    if (!manager || !manager->hasTarget) {
        return NULL;
    }
    return &manager->source;
}

uint32_t InputCursorManagerClearOverrides(InputCursorManager *manager)
{
    uint32_t removed;

    if (!manager) {
        return 0u;
    }
    removed = manager->count;
    memset(manager->overrides, 0, sizeof(manager->overrides));
    manager->count = 0u;
    return removed;
}

Result InputCursorManagerWrite(const InputCursorManager *manager,
                               char *buffer,
                               size_t capacity,
                               size_t *outLength,
                               InputDiag *diag)
{
    InputWriter text;
    uint32_t index;

    if (!RiWriterArgumentsAreValid(buffer, capacity, outLength)) {
        return RoboInputDiag(diag, RoboInputDomainDump, RoboInvalidArgument,
                             "a dump needs a buffer, a capacity and somewhere to put the length");
    }
    RiWriterInit(&text, buffer, capacity);
    if (!manager) {
        RiWriterText(&text, "cursor <none>\n");
        return RiWriterFinish(&text, outLength, diag);
    }

    RiWriterText(&text, "cursor shape=");
    RiWriterText(&text, InputCursorTypeName(manager->current));
    RiWriterText(&text, " from=");
    RiWriterText(&text, manager->hasTarget ? InputTargetKindName(manager->source.kind) : "none");
    RiWriterFieldU64(&text, " overrides", (uint64_t)manager->count);
    RiWriterFieldU64(&text, " accepted", (uint64_t)manager->accepted);
    RiWriterFieldU64(&text, " unchanged", (uint64_t)manager->unchanged);
    RiWriterFieldU64(&text, " refused", (uint64_t)manager->refused);
    RiWriterChar(&text, '\n');
    for (index = 0u; index < RoboInputMaxCursorOverrides; index++) {
        if (!manager->overrides[index].active) {
            continue;
        }
        RiWriterText(&text, "  override kind=");
        RiWriterText(&text, InputTargetKindName(manager->overrides[index].target.kind));
        RiWriterFieldU64(&text, " window", (uint64_t)manager->overrides[index].target.window);
        RiWriterFieldText(&text, " shape", InputCursorTypeName(manager->overrides[index].type));
        RiWriterChar(&text, '\n');
    }
    return RiWriterFinish(&text, outLength, diag);
}
