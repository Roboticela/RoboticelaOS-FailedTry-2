/*
 * RoboInput shortcuts (Phase 13.35/13.36) — the **one** place a `PhysicalKey` becomes an intention.
 *
 * ## Why this file exists, and why it is the only one
 *
 * A command (`RoboInputCommand.h`) is what the user meant; a chord is what they pressed. Somebody has to hold
 * the mapping, and if more than one layer does, every layer has its own opinion about whether `Ctrl+A` selects
 * a document or a text. So the mapping lives here, in a **table**, and resolution is a total function of it:
 * same chord, same scope, same command, every time.
 *
 * ## Scopes, and the order that makes a text field work
 *
 * A shortcut belongs to a scope, and scopes are answered **from the inside out**:
 *
 *     Control  →  Document  →  Window  →  Desktop  →  Global
 *
 * which is the whole of the brief's "a focused text field should be able to consume a shortcut before a global
 * action where appropriate": `Ctrl+A` bound to *select all* in `Control` never reaches a `Global` *select
 * everything*, and nothing had to special-case a text field to make that true.
 *
 * ## The rule that keeps resolution deterministic
 *
 * **One chord means one command per scope.** Adding a second command on a chord a scope already uses is
 * `RoboAlreadyExists` rather than a silent override, because an override makes the answer depend on
 * registration order — and a shortcut table is exactly the kind of data that arrives from several places. The
 * same command on the same chord is accepted and is idempotent, so a caller that re-registers its own table
 * does not have to know what it did last.
 *
 * ## What a chord is, and what it is not
 *
 * A chord is a `PhysicalKey` plus the modifier bits that **mean something for a shortcut**: shift, control,
 * alt and meta (`RoboInputModifierChordMask`). The lock bits are *not* part of a chord — caps lock does not
 * change what `Ctrl+A` means — and neither is a key's side. `AltRight` is folded onto `Ctrl+Alt`, so the two
 * spellings of AltGr (13.9) resolve to one chord instead of needing two table entries that could drift apart.
 *
 * `RoboInputModifierChord` is that normalization, exposed because a caller filling a shortcut table from a
 * config file needs to say what a chord *is* without reading this paragraph.
 */
#ifndef ROBO_INPUT_SHORTCUT_H
#define ROBO_INPUT_SHORTCUT_H

#include "RoboBase/RoboError.h"

#include "RoboInput/RoboInputCommand.h"
#include "RoboInput/RoboInputDiagnostics.h"
#include "RoboInput/RoboInputEvent.h"
#include "RoboInput/RoboInputTypes.h"

#include "RoboWindow/RoboWindowKey.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* The chord                                                           */
/* ------------------------------------------------------------------ */

/** The modifier bits a shortcut resolves on: shift, control, alt and meta, and nothing else. */
#define RoboInputModifierChordMask \
    ((InputModifiers)(RoboKeyModShift | RoboKeyModCtrl | RoboKeyModAlt | RoboKeyModMeta))

/**
 * The chord a modifier state is, for shortcut purposes.
 *
 * The lock bits are dropped — caps lock does not change what `Ctrl+A` means — a key's **side** folds onto its
 * family (a backend that reports only the side bit would otherwise resolve a modified chord as an unmodified
 * one), and `AltRight` folds onto `Ctrl+Alt` so both spellings of AltGr resolve to **one** chord rather than two
 * entries a table could disagree about. A bare left Alt stays the plain `Alt` it is: it is a menu accelerator,
 * and folding it onto AltGr would make every Alt binding fire on the right-hand key.
 */
InputModifiers InputModifierChord(InputModifiers modifiers);

/* ------------------------------------------------------------------ */
/* Scopes                                                              */
/* ------------------------------------------------------------------ */

/** Who owns a shortcut. Answered from the inside out: `Control` first, `Global` last. */
typedef enum InputShortcutScope {
    /** Anywhere in the runtime. The broadest, and answered last. */
    RoboInputShortcutScopeGlobal = 0,
    /** The desktop shell: the menu bar, window management, the debug overlay. */
    RoboInputShortcutScopeDesktop = 1,
    /** The active window. */
    RoboInputShortcutScopeWindow = 2,
    /** The active document: select all, find, save. */
    RoboInputShortcutScopeDocument = 3,
    /** The focused control itself. The narrowest, and answered **first**. */
    RoboInputShortcutScopeControl = 4
} InputShortcutScope;

/** Number of defined scopes. Asserted positionally by `input.shortcut.vocabulary`. */
#define RoboInputShortcutScopeCount 5u

/** Non-zero when @p scope is defined. */
int InputShortcutScopeIsKnown(InputShortcutScope scope);

/** Human-readable scope name (`"control"`); never NULL; `"unknown"` when undefined. */
const char *InputShortcutScopeName(InputShortcutScope scope);

/* ------------------------------------------------------------------ */
/* One shortcut                                                        */
/* ------------------------------------------------------------------ */

/** One row of a shortcut table: this chord, in this scope, means this command. */
typedef struct InputShortcut {
    /** What it means. Never `NONE` in a valid record. */
    InputCommandId command;
    /** The position it fires on. */
    PhysicalKey physical;
    /** The chord's modifier bits, already normalized (`InputModifierChord`). */
    InputModifiers modifiers;
    /** Who owns it. */
    InputShortcutScope scope;
    /** Non-zero when it may fire. A disabled row is *remembered* rather than removed, so a mode's table can be
     *  switched off without losing what it was. */
    int enabled;
} InputShortcut;

/** Clear @p shortcut: no command, no key, no modifiers, `GLOBAL`, enabled. Safe on NULL. */
void InputShortcutInit(InputShortcut *shortcut);

/** Fill @p shortcut, normalizing @p modifiers to a chord. Refused for an unknown command or scope, a key with
 *  no identity, or a modifier bit this build does not define. */
Result InputShortcutSet(InputShortcut *shortcut,
                        InputCommandId command,
                        PhysicalKey physical,
                        InputModifiers modifiers,
                        InputShortcutScope scope,
                        InputDiag *diag);

/** Non-zero when @p shortcut is a row a table may hold. */
int InputShortcutIsValid(const InputShortcut *shortcut);

/** Field-by-field equality, **chords compared as chords** so two spellings of AltGr are equal. Safe on NULL. */
int InputShortcutEqual(const InputShortcut *a, const InputShortcut *b);

/** Non-zero when @p shortcut fires on @p physical with @p modifiers. Ignores @p modifiers' locks and sides. */
int InputShortcutMatches(const InputShortcut *shortcut,
                         PhysicalKey physical,
                         InputModifiers modifiers);

/* ------------------------------------------------------------------ */
/* The table                                                           */
/* ------------------------------------------------------------------ */

/** How many shortcuts one manager holds. A stated bound, refused rather than grown. */
#define RoboInputShortcutMax 64u

/**
 * A shortcut table, held by value.
 *
 * No allocator and nothing to destroy, like the device registry, the layout and the command chain. The table is
 * **not** kept sorted: resolution is a scope-ordered scan, and a sorted table would have to be re-sorted by
 * `enabled` toggles that do not change the ordering rule.
 */
typedef struct InputShortcutManager {
    /** The rows, in registration order. */
    InputShortcut shortcuts[RoboInputShortcutMax];
    /** How many are live. */
    uint32_t count;
    /** Chords that resolved to a command. Diagnostics. */
    uint64_t resolvedTotal;
    /** Chords that resolved to nothing. The number that says whether a claim about a shortcut is true. */
    uint64_t missesTotal;
} InputShortcutManager;

/** Clear @p manager. Safe on NULL. */
void InputShortcutManagerInit(InputShortcutManager *manager);

/**
 * Add or re-state one shortcut.
 *
 * @return `RoboOk` when it was added, or when the same command was already on that chord in that scope (which
 *         is idempotent, not a conflict); `RoboAlreadyExists` when a **different** command already owns the
 *         chord in that scope; `RoboNotSupported` when the table is full; `RoboInvalidArgument` for a NULL
 *         manager or an invalid shortcut.
 */
Result InputShortcutManagerAdd(InputShortcutManager *manager,
                              const InputShortcut *shortcut,
                              InputDiag *diag);

/** Remove the shortcut for @p command in @p scope, whichever chord it uses. `RoboNotFound` when there is
 *  none. Removing is how a mode takes a binding away; `enabled` is how it pauses one. */
Result InputShortcutManagerRemoveCommand(InputShortcutManager *manager,
                                        InputCommandId command,
                                        InputShortcutScope scope,
                                        InputDiag *diag);

/** Remove whatever owns @p physical with @p modifiers in @p scope. `RoboNotFound` when nothing does. */
Result InputShortcutManagerRemoveChord(InputShortcutManager *manager,
                                      PhysicalKey physical,
                                      InputModifiers modifiers,
                                      InputShortcutScope scope,
                                      InputDiag *diag);

/** Drop every row. */
void InputShortcutManagerClear(InputShortcutManager *manager);

/** How many rows the table holds. 0 for NULL. */
uint32_t InputShortcutManagerCount(const InputShortcutManager *manager);

/** The row at @p index in registration order. Refused past the end. */
Result InputShortcutManagerAt(const InputShortcutManager *manager,
                             uint32_t index,
                             InputShortcut *out,
                             InputDiag *diag);

/** The first row that names @p command, in scope order (most specific first) and then registration order.
 *  `RoboNotFound` when none does. */
Result InputShortcutManagerFindCommand(const InputShortcutManager *manager,
                                      InputCommandId command,
                                      InputShortcut *out,
                                      InputDiag *diag);

/** Turn the row for @p command in @p scope on or off. `RoboNotFound` when there is none. A disabled row never
 *  resolves, and it is still findable, which is what makes a mode switchable. */
Result InputShortcutManagerSetEnabled(InputShortcutManager *manager,
                                     InputCommandId command,
                                     InputShortcutScope scope,
                                     int enabled,
                                     InputDiag *diag);

/**
 * Resolve a chord.
 *
 * Walks the scopes from `CONTROL` outwards and answers the first enabled row that matches, so the most specific
 * binding wins and a focused control consumes a chord before a global action sees it.
 *
 * @param modifiers the modifier state **as reported**; it is normalized here, so a caller does not have to.
 * @return `RoboOk` with @p out filled; `RoboNotFound` when nothing matched (counted, because a chord that
 *         resolved to nothing is the fact a shortcut claim is judged by).
 *
 * The manager is **not const**: resolving counts its two outcomes, and a diagnostic that only existed on a
 * copy would be a diagnostic nobody could read. Asking is still a read of the table.
 */
Result InputShortcutManagerResolve(InputShortcutManager *manager,
                                  PhysicalKey physical,
                                  InputModifiers modifiers,
                                  InputShortcut *out,
                                  InputDiag *diag);

/**
 * Resolve a **key event**.
 *
 * The door a dispatcher calls: it refuses anything that is not a key event (`RoboNotSupported`, because a
 * pointer event has no chord) and otherwise forwards the event's key and modifier state to
 * `InputShortcutManagerResolve`. A key **up** — and an auto-repeat — is refused by name: a shortcut fires on a
 * press, and a table that also answered a release or a repeat would fire every command twice.
 *
 * A **refusal here does not count a miss**, because the chord never reached the table: counting it would make
 * "how often did a chord reach the table and find nothing?" unanswerable.
 */
Result InputShortcutManagerResolveEvent(InputShortcutManager *manager,
                                       const InputEvent *keyEvent,
                                       InputShortcut *out,
                                       InputDiag *diag);

/** Room `InputShortcutManagerWrite` needs, NUL included. */
#define RoboInputShortcutManagerDumpMax (128u + (RoboInputShortcutMax * 96u))

/**
 * Write @p manager as text: a summary line, then one line per row in **scope order** (most specific first) so
 * the text reads in the order resolution uses.
 *
 *     shortcuts count=3 resolved=0 misses=0
 *     shortcut scope=control physical=C modifiers=ctrl command=copy enabled=1
 *
 * `?` and the needed length when the buffer is too small.
 */
Result InputShortcutManagerWrite(const InputShortcutManager *manager,
                                char *buffer,
                                size_t capacity,
                                size_t *outLength,
                                InputDiag *diag);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_INPUT_SHORTCUT_H */
