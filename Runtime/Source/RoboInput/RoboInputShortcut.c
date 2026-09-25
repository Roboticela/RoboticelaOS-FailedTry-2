/*
 * RoboInput shortcuts (Phase 13.35/13.36) — see `RoboInputShortcut.h`.
 *
 * Four pieces:
 *
 *   **the chord**      one normalization function, which is what makes AltGr's two spellings one chord and keeps
 *                      the locks out of a shortcut's identity;
 *   **the row**        one shortcut as a value, with `Matches` written once;
 *   **the table**      add with the conflict rule, remove two ways, and find-in-scope-order;
 *   **resolution**     walk the scopes from the inside out, answer the first enabled match, and count both
 *                      outcomes -- because "the shortcut did not fire" and "the shortcut fired the wrong thing"
 *                      are the two failures a table has, and they need two numbers.
 */

#include "RoboInput/RoboInputShortcut.h"

#include "RoboInputInternal.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* The chord                                                           */
/* ------------------------------------------------------------------ */

InputModifiers InputModifierChord(InputModifiers modifiers)
{
    InputModifiers chord = modifiers & (InputModifiers)RoboInputModifierChordMask;

    /* A key's **side** folds onto its family, because a chord is "shift is down", not "the left shift is down":
     * a backend that reports only the side bit (which is a real backend) would otherwise resolve a modified
     * chord as an unmodified one -- and an `Alt+F` binding would fire on a plain `F`. */
    if ((modifiers & (InputModifiers)(RoboKeyModShiftLeft | RoboKeyModShiftRight)) != 0u) {
        chord |= (InputModifiers)RoboKeyModShift;
    }
    if ((modifiers & (InputModifiers)(RoboKeyModCtrlLeft | RoboKeyModCtrlRight)) != 0u) {
        chord |= (InputModifiers)RoboKeyModCtrl;
    }
    if ((modifiers & (InputModifiers)RoboKeyModAltLeft) != 0u) {
        chord |= (InputModifiers)RoboKeyModAlt;
    }
    if ((modifiers & (InputModifiers)(RoboKeyModMetaLeft | RoboKeyModMetaRight)) != 0u) {
        chord |= (InputModifiers)RoboKeyModMeta;
    }

    /* AltGr has two spellings (13.9) and a shortcut table must not need two rows for it: `AltRight` folds onto
     * `Ctrl+Alt`, so a binding written either way is found either way -- while a bare left Alt stays the plain
     * `Alt` it is, because folding the two would make every Alt binding fire on AltGr. */
    if ((modifiers & (InputModifiers)RoboKeyModAltRight) != 0u) {
        chord |= (InputModifiers)(RoboKeyModCtrl | RoboKeyModAlt);
    }
    return chord;
}

/* ------------------------------------------------------------------ */
/* Scopes                                                              */
/* ------------------------------------------------------------------ */

int InputShortcutScopeIsKnown(InputShortcutScope scope)
{
    return (int)scope >= 0 && (int)scope < (int)RoboInputShortcutScopeCount;
}

const char *InputShortcutScopeName(InputShortcutScope scope)
{
    switch (scope) {
    case RoboInputShortcutScopeGlobal: return "global";
    case RoboInputShortcutScopeDesktop: return "desktop";
    case RoboInputShortcutScopeWindow: return "window";
    case RoboInputShortcutScopeDocument: return "document";
    case RoboInputShortcutScopeControl: return "control";
    default: break;
    }
    return "unknown";
}

/**
 * The scope a level of resolution asks about.
 *
 * The walk goes from the **narrowest** to the broadest, which is the order that makes a focused text field win
 * an argument with a global binding without anybody special-casing a text field.
 */
static InputShortcutScope ShortcutScopeAtLevel(uint32_t level)
{
    return (InputShortcutScope)((int)RoboInputShortcutScopeCount - 1 - (int)level);
}

/* ------------------------------------------------------------------ */
/* One row                                                             */
/* ------------------------------------------------------------------ */

void InputShortcutInit(InputShortcut *shortcut)
{
    if (!shortcut) {
        return;
    }
    shortcut->command = RoboInputCommandNone;
    shortcut->physical = RoboPhysicalKeyUnknown;
    shortcut->modifiers = RoboInputModifiersNone;
    shortcut->scope = RoboInputShortcutScopeGlobal;
    shortcut->enabled = 1;
}

Result InputShortcutSet(InputShortcut *shortcut,
                        InputCommandId command,
                        PhysicalKey physical,
                        InputModifiers modifiers,
                        InputShortcutScope scope,
                        InputDiag *diag)
{
    if (!shortcut) {
        return RoboInputDiag(diag, RoboInputDomainShortcut, RoboInvalidArgument,
                             "nowhere to put the shortcut");
    }
    /* Cleared **first**, so a refusal leaves an empty record rather than a mixture of the refused call and the
     * previous accepted one -- the rule every door in this module keeps. */
    InputShortcutInit(shortcut);
    if (!InputCommandIdIsKnown(command)) {
        return RoboInputDiag(diag, RoboInputDomainShortcut, RoboInvalidArgument,
                             "a shortcut must name a command this build defines");
    }
    if (physical == RoboPhysicalKeyUnknown || !PhysicalKeyIsKnown(physical)) {
        return RoboInputDiag(diag, RoboInputDomainShortcut, RoboInvalidArgument,
                             "a shortcut must name a key position this build defines");
    }
    if ((modifiers & ~(InputModifiers)RoboInputModifierAll) != 0u) {
        return RoboInputDiag(diag, RoboInputDomainShortcut, RoboInvalidArgument,
                             "the modifier state carries bits this build does not define");
    }
    if (!InputShortcutScopeIsKnown(scope)) {
        return RoboInputDiag(diag, RoboInputDomainShortcut, RoboInvalidArgument,
                             "a shortcut must belong to a scope this build defines");
    }

    shortcut->command = command;
    shortcut->physical = physical;
    shortcut->modifiers = InputModifierChord(modifiers);
    shortcut->scope = scope;
    shortcut->enabled = 1;
    return RoboOk;
}

int InputShortcutIsValid(const InputShortcut *shortcut)
{
    if (!shortcut) {
        return 0;
    }
    if (!InputCommandIdIsKnown(shortcut->command)) {
        return 0;
    }
    if (shortcut->physical == RoboPhysicalKeyUnknown || !PhysicalKeyIsKnown(shortcut->physical)) {
        return 0;
    }
    if (!InputShortcutScopeIsKnown(shortcut->scope)) {
        return 0;
    }
    /* Only a chord: a row that carried a lock bit could never match, because `Matches` compares chords. A
     * record holding one is a hand-filled row, and saying so is better than a binding that silently never
     * fires. */
    if ((shortcut->modifiers & ~(InputModifiers)RoboInputModifierChordMask) != 0u) {
        return 0;
    }
    return 1;
}

int InputShortcutEqual(const InputShortcut *a, const InputShortcut *b)
{
    if (a == b) {
        return 1;
    }
    if (!a || !b) {
        return 0;
    }
    /* Compared as **chords**, so a row written `AltRight` equals one written `Ctrl+Alt` -- which is the same
     * fact stated two ways, and a conflict check that missed it would let two rows claim one chord. */
    return a->command == b->command && a->physical == b->physical && a->scope == b->scope &&
           a->enabled == b->enabled &&
           InputModifierChord(a->modifiers) == InputModifierChord(b->modifiers);
}

int InputShortcutMatches(const InputShortcut *shortcut,
                         PhysicalKey physical,
                         InputModifiers modifiers)
{
    if (!shortcut || !shortcut->enabled) {
        return 0;
    }
    return shortcut->physical == physical &&
           InputModifierChord(shortcut->modifiers) == InputModifierChord(modifiers);
}

/* ------------------------------------------------------------------ */
/* The table                                                           */
/* ------------------------------------------------------------------ */

void InputShortcutManagerInit(InputShortcutManager *manager)
{
    uint32_t index;

    if (!manager) {
        return;
    }
    for (index = 0u; index < RoboInputShortcutMax; index++) {
        InputShortcutInit(&manager->shortcuts[index]);
    }
    manager->count = 0u;
    manager->resolvedTotal = 0u;
    manager->missesTotal = 0u;
}

uint32_t InputShortcutManagerCount(const InputShortcutManager *manager)
{
    return manager ? manager->count : 0u;
}

Result InputShortcutManagerAdd(InputShortcutManager *manager,
                              const InputShortcut *shortcut,
                              InputDiag *diag)
{
    InputShortcut row;
    uint32_t index;

    if (!manager || !shortcut) {
        return RoboInputDiag(diag, RoboInputDomainShortcut, RoboInvalidArgument,
                             "a manager and a shortcut are both needed");
    }
    if (!InputShortcutIsValid(shortcut)) {
        return RoboInputDiag(diag, RoboInputDomainShortcut, RoboInvalidArgument,
                             "the shortcut is not one a table may hold");
    }
    row = *shortcut;
    row.enabled = row.enabled ? 1 : 0;

    for (index = 0u; index < manager->count; index++) {
        const InputShortcut *existing = &manager->shortcuts[index];

        if (existing->scope != row.scope || existing->physical != row.physical ||
            InputModifierChord(existing->modifiers) != row.modifiers) {
            continue;
        }
        /* One chord means one command per scope. Re-stating the same command is idempotent -- a caller that
         * re-registers its table does not have to know what it did last -- while a *different* command is a
         * conflict, because an override would make the answer depend on registration order. */
        if (existing->command == row.command) {
            manager->shortcuts[index] = row;
            return RoboOk;
        }
        return RoboInputDiag(diag, RoboInputDomainShortcut, RoboAlreadyExists,
                             "that chord already means another command in that scope");
    }

    if (manager->count >= RoboInputShortcutMax) {
        return RoboInputDiag(diag, RoboInputDomainShortcut, RoboNotSupported,
                             "the shortcut table is full");
    }
    manager->shortcuts[manager->count] = row;
    manager->count++;
    return RoboOk;
}

Result InputShortcutManagerRemoveCommand(InputShortcutManager *manager,
                                        InputCommandId command,
                                        InputShortcutScope scope,
                                        InputDiag *diag)
{
    uint32_t index;

    if (!manager) {
        return RoboInputDiag(diag, RoboInputDomainShortcut, RoboInvalidArgument,
                             "no table to remove from");
    }
    for (index = 0u; index < manager->count; index++) {
        if (manager->shortcuts[index].command == command && manager->shortcuts[index].scope == scope) {
            /* Collapse the hole so the table has no gaps: `count` is the number of live rows, and a table with
             * a dead row in the middle would make `At` report one. */
            uint32_t move;
            for (move = index + 1u; move < manager->count; move++) {
                manager->shortcuts[move - 1u] = manager->shortcuts[move];
            }
            manager->count--;
            InputShortcutInit(&manager->shortcuts[manager->count]);
            return RoboOk;
        }
    }
    return RoboInputDiag(diag, RoboInputDomainShortcut, RoboNotFound,
                         "no shortcut for that command in that scope");
}

Result InputShortcutManagerRemoveChord(InputShortcutManager *manager,
                                      PhysicalKey physical,
                                      InputModifiers modifiers,
                                      InputShortcutScope scope,
                                      InputDiag *diag)
{
    uint32_t index;

    if (!manager) {
        return RoboInputDiag(diag, RoboInputDomainShortcut, RoboInvalidArgument,
                             "no table to remove from");
    }
    for (index = 0u; index < manager->count; index++) {
        const InputShortcut *row = &manager->shortcuts[index];

        if (row->scope == scope && row->physical == physical &&
            InputModifierChord(row->modifiers) == InputModifierChord(modifiers)) {
            uint32_t move;
            for (move = index + 1u; move < manager->count; move++) {
                manager->shortcuts[move - 1u] = manager->shortcuts[move];
            }
            manager->count--;
            InputShortcutInit(&manager->shortcuts[manager->count]);
            return RoboOk;
        }
    }
    return RoboInputDiag(diag, RoboInputDomainShortcut, RoboNotFound,
                         "no shortcut on that chord in that scope");
}

void InputShortcutManagerClear(InputShortcutManager *manager)
{
    uint32_t index;

    if (!manager) {
        return;
    }
    for (index = 0u; index < manager->count; index++) {
        InputShortcutInit(&manager->shortcuts[index]);
    }
    manager->count = 0u;
}

Result InputShortcutManagerAt(const InputShortcutManager *manager,
                             uint32_t index,
                             InputShortcut *out,
                             InputDiag *diag)
{
    if (!manager || !out) {
        return RoboInputDiag(diag, RoboInputDomainShortcut, RoboInvalidArgument,
                             "a table and somewhere to put the shortcut are both needed");
    }
    if (index >= manager->count) {
        return RoboInputDiag(diag, RoboInputDomainShortcut, RoboNotFound,
                             "no shortcut at that index");
    }
    *out = manager->shortcuts[index];
    return RoboOk;
}

Result InputShortcutManagerFindCommand(const InputShortcutManager *manager,
                                      InputCommandId command,
                                      InputShortcut *out,
                                      InputDiag *diag)
{
    uint32_t level;
    uint32_t index;

    if (!manager || !out) {
        return RoboInputDiag(diag, RoboInputDomainShortcut, RoboInvalidArgument,
                             "a table and somewhere to put the shortcut are both needed");
    }
    /* In **scope order**, so asking "where is copy bound?" answers with the binding that would actually fire if
     * several scopes named it. */
    for (level = 0u; level < RoboInputShortcutScopeCount; level++) {
        InputShortcutScope scope = ShortcutScopeAtLevel(level);

        for (index = 0u; index < manager->count; index++) {
            if (manager->shortcuts[index].command == command &&
                manager->shortcuts[index].scope == scope) {
                *out = manager->shortcuts[index];
                return RoboOk;
            }
        }
    }
    return RoboInputDiag(diag, RoboInputDomainShortcut, RoboNotFound,
                         "no shortcut for that command");
}

Result InputShortcutManagerSetEnabled(InputShortcutManager *manager,
                                     InputCommandId command,
                                     InputShortcutScope scope,
                                     int enabled,
                                     InputDiag *diag)
{
    uint32_t index;

    if (!manager) {
        return RoboInputDiag(diag, RoboInputDomainShortcut, RoboInvalidArgument,
                             "no table to change");
    }
    for (index = 0u; index < manager->count; index++) {
        if (manager->shortcuts[index].command == command && manager->shortcuts[index].scope == scope) {
            manager->shortcuts[index].enabled = enabled ? 1 : 0;
            return RoboOk;
        }
    }
    return RoboInputDiag(diag, RoboInputDomainShortcut, RoboNotFound,
                         "no shortcut for that command in that scope");
}

/* ------------------------------------------------------------------ */
/* Resolution                                                          */
/* ------------------------------------------------------------------ */

Result InputShortcutManagerResolve(InputShortcutManager *manager,
                                  PhysicalKey physical,
                                  InputModifiers modifiers,
                                  InputShortcut *out,
                                  InputDiag *diag)
{
    uint32_t level;
    uint32_t index;
    InputModifiers chord;

    if (!manager) {
        return RoboInputDiag(diag, RoboInputDomainShortcut, RoboInvalidArgument,
                             "no table to resolve against");
    }
    chord = InputModifierChord(modifiers);

    /* The inside-out walk: `CONTROL` is level 0 and `GLOBAL` is the last, so the narrowest binding wins. */
    for (level = 0u; level < RoboInputShortcutScopeCount; level++) {
        InputShortcutScope scope = ShortcutScopeAtLevel(level);

        for (index = 0u; index < manager->count; index++) {
            const InputShortcut *row = &manager->shortcuts[index];

            if (row->scope != scope || !row->enabled) {
                continue;
            }
            if (row->physical == physical && InputModifierChord(row->modifiers) == chord) {
                if (out) {
                    *out = *row;
                }
                manager->resolvedTotal++;
                return RoboOk;
            }
        }
    }

    manager->missesTotal++;
    return RoboInputDiag(diag, RoboInputDomainShortcut, RoboNotFound,
                         "no shortcut fires on that chord");
}

Result InputShortcutManagerResolveEvent(InputShortcutManager *manager,
                                       const InputEvent *keyEvent,
                                       InputShortcut *out,
                                       InputDiag *diag)
{
    if (!manager || !keyEvent) {
        return RoboInputDiag(diag, RoboInputDomainShortcut, RoboInvalidArgument,
                             "a table and a key event are both needed");
    }
    if (!InputEventTypeIsKey(keyEvent->type)) {
        return RoboInputDiag(diag, RoboInputDomainShortcut, RoboNotSupported,
                             "a shortcut resolves on a key event; a pointer event has no chord");
    }
    /* A press only. An **up** would fire every command a second time, and an auto-repeat would fire it as fast
     * as the keyboard repeats -- neither of which any caller wants, and both of which a table that answered
     * every key event would do. */
    if (keyEvent->type != RoboInputEventKeyDown) {
        return RoboInputDiag(diag, RoboInputDomainShortcut, RoboNotSupported,
                             "a shortcut fires on a key press, not a release or a repeat");
    }
    return InputShortcutManagerResolve(manager, keyEvent->payload.keyboard.physical,
                                       keyEvent->modifiers, out, diag);
    /* `InputEventTypeIsKey` accepts `KEY_REPEAT` (it is a *down* of a held key), and the check above is
     * deliberately `== KEY_DOWN`: an auto-repeat that fired a shortcut would repeat the command as fast as the
     * keyboard repeats. */
}

/* ------------------------------------------------------------------ */
/* The dump                                                            */
/* ------------------------------------------------------------------ */

/**
 * Write a chord as `ctrl+alt`, or `none`.
 *
 * Written here rather than read from a shared table because a chord is **four bits in a fixed order**, and the
 * order is the whole of what makes two dumps comparable: shift, control, alt, meta.
 */
static void ShortcutWriteChord(InputWriter *text, InputModifiers modifiers)
{
    InputModifiers chord = InputModifierChord(modifiers);
    int written = 0;

    if ((chord & (InputModifiers)RoboKeyModShift) != 0u) {
        RiWriterText(text, "shift");
        written = 1;
    }
    if ((chord & (InputModifiers)RoboKeyModCtrl) != 0u) {
        if (written) {
            RiWriterChar(text, '+');
        }
        RiWriterText(text, "ctrl");
        written = 1;
    }
    if ((chord & (InputModifiers)RoboKeyModAlt) != 0u) {
        if (written) {
            RiWriterChar(text, '+');
        }
        RiWriterText(text, "alt");
        written = 1;
    }
    if ((chord & (InputModifiers)RoboKeyModMeta) != 0u) {
        if (written) {
            RiWriterChar(text, '+');
        }
        RiWriterText(text, "meta");
        written = 1;
    }
    if (!written) {
        RiWriterText(text, "none");
    }
}

Result InputShortcutManagerWrite(const InputShortcutManager *manager,
                                char *buffer,
                                size_t capacity,
                                size_t *outLength,
                                InputDiag *diag)
{
    InputWriter text;
    uint32_t level;

    if (!RiWriterArgumentsAreValid(buffer, capacity, outLength)) {
        return RoboInputDiag(diag, RoboInputDomainDump, RoboInvalidArgument,
                             "a dump needs a buffer, a capacity and somewhere to put the length");
    }
    RiWriterInit(&text, buffer, capacity);
    if (!manager) {
        RiWriterText(&text, "shortcuts <none>\n");
        return RiWriterFinish(&text, outLength, diag);
    }

    RiWriterText(&text, "shortcuts");
    RiWriterFieldU64(&text, " count", (uint64_t)manager->count);
    RiWriterFieldU64(&text, " resolved", manager->resolvedTotal);
    RiWriterFieldU64(&text, " misses", manager->missesTotal);
    RiWriterChar(&text, '\n');

    /* In **scope order**, which is the order resolution uses: a dump in array order would make a reader
     * reverse it in their head to see which of two bindings wins. */
    for (level = 0u; level < RoboInputShortcutScopeCount; level++) {
        InputShortcutScope scope = ShortcutScopeAtLevel(level);
        uint32_t index;

        for (index = 0u; index < manager->count; index++) {
            const InputShortcut *row = &manager->shortcuts[index];

            if (row->scope != scope) {
                continue;
            }
            RiWriterText(&text, "shortcut");
            RiWriterFieldText(&text, " scope", InputShortcutScopeName(row->scope));
            RiWriterFieldText(&text, " physical", PhysicalKeyName(row->physical));
            RiWriterText(&text, " modifiers=");
            ShortcutWriteChord(&text, row->modifiers);
            RiWriterFieldText(&text, " command", InputCommandIdName(row->command));
            RiWriterFieldU64(&text, " enabled", row->enabled ? 1u : 0u);
            RiWriterChar(&text, '\n');
        }
    }
    return RiWriterFinish(&text, outLength, diag);
}
