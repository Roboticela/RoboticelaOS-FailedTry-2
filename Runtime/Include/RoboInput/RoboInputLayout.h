/*
 * RoboInput keyboard layouts (Phase 13.9/13.10) — physical key + modifier state → a character.
 *
 * ## It produces **characters**, and that is where its job stops
 *
 * A layout maps a *position* to a *meaning*: this key, with shift down, is `A`. It does not shape, does not
 * reorder, does not know what a grapheme is and does not touch a text buffer. What it produces is the text
 * a `TEXT_INPUT` event carries (13.11), and RoboText shapes it afterwards -- the layering the brief states
 * and the reason a layout is a *table* here rather than a piece of a text engine.
 *
 * The one exception is deliberately small and is not shaping: a **dead key** (13.10) needs to combine with
 * the *next* key's scalar to produce one precomposed scalar, and that combination is a property of the
 * layout's own table rather than of Unicode. RoboUnicode is still the module that owns scalars; this one
 * carries a two-column table of pairs a layout author wrote down.
 *
 * ## Levels, and why caps lock is not a level
 *
 * A layout has four levels: base, shift, AltGr, and shift+AltGr. `InputLayoutLevelFor` derives one from the
 * modifier state, and the AltGr rule is the interesting half: a layout table cannot know whether the
 * platform spells it `AltRight` or `Ctrl+Alt`, so both spellings mean the third level -- which is also what
 * keeps `Ctrl+A` from being read as a character (a *control* chord is Ctrl without Alt, and produces no
 * text).
 *
 * Caps lock is **not** a level, because it does not work like one: it inverts the shift level for *cased*
 * keys and leaves punctuation alone, so `Shift+1` is still `!` with caps lock on. That is why a mapping
 * carries a `CASED` flag: a table that made caps lock a fifth level would have to list every punctuation
 * key twice to say "not this one".
 *
 * ## The built-in layouts, and the honesty of the second one
 *
 * Two: `US_ASCII` (letters, digits, the punctuation row, space -- no dead keys) and `US_INTERNATIONAL`
 * (the same, plus dead accents on the four keys a US-International layout puts them on). They are the
 * "minimal deterministic layout sufficient for testing" the brief asks for, and they are *built* by
 * `InputLayoutCreateBuiltin` rather than parsed from a file, because this runtime has no layout file format
 * and inventing one now would be a promise about a feature nobody has asked for. **A layout for any other
 * language is data this module can hold and does not ship** -- named rather than implied, and the reason
 * `InputLayoutSetMapping` is public.
 */
#ifndef ROBO_INPUT_LAYOUT_H
#define ROBO_INPUT_LAYOUT_H

#include "RoboBase/RoboError.h"

#include "RoboInput/RoboInputDiagnostics.h"
#include "RoboInput/RoboInputTypes.h"

#include "RoboUnicode/RoboUnicode.h"

#include "RoboWindow/RoboWindowKey.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Copied layout-name capacity, including the NUL. */
#define RoboInputLayoutNameMax 32u

/** How many mappings one layout holds. A bound on one table: two levels of a full US row plus room. */
#define RoboInputLayoutMaxMappings 128u

/** The mapping is **cased**: caps lock inverts its shift level. Set for letters, clear for punctuation. */
#define RoboInputLayoutMappingFlagCased ((uint32_t)(1u << 0))

/** Every defined mapping flag. */
#define RoboInputLayoutMappingFlagAll (RoboInputLayoutMappingFlagCased)

/** A layout the runtime builds. */
typedef enum InputLayoutId {
    /** Nothing: a cleared record's value. */
    RoboInputLayoutNone = 0,
    /** US ASCII: letters, digits, punctuation, space. No dead keys. */
    RoboInputLayoutUsAscii = 1,
    /** US International: the same, and `'`, `` ` ``, `^`, `"` are dead accents. */
    RoboInputLayoutUsInternational = 2
} InputLayoutId;

/** Number of defined layout ids. Asserted positionally by `input.layout.vocabulary`. */
#define RoboInputLayoutIdCount 3u

/** Human-readable layout name; never NULL; `"unknown"` when undefined. */
const char *InputLayoutIdName(InputLayoutId id);

/**
 * Which of a layout's four levels a modifier state selects.
 *
 * `SHIFT` means shift is down *after* caps lock has had its say for a cased key, so the level a caller passes
 * to `InputLayoutLookup` is already the answer rather than a hint.
 */
typedef enum InputLayoutLevel {
    /** Nothing down, or nothing that a layout cares about. */
    RoboInputLayoutLevelBase = 0,
    /** Shift (or caps lock on a cased key). */
    RoboInputLayoutLevelShift = 1,
    /** AltGr: the third level. */
    RoboInputLayoutLevelAltGr = 2,
    /** Shift and AltGr together: the fourth level. */
    RoboInputLayoutLevelShiftAltGr = 3
} InputLayoutLevel;

/** Number of defined levels. Asserted positionally by `input.layout.vocabulary`. */
#define RoboInputLayoutLevelCount 4u

/** Human-readable level name; never NULL. */
const char *InputLayoutLevelName(InputLayoutLevel level);

/** Non-zero if @p level is a defined level. */
int InputLayoutLevelIsKnown(InputLayoutLevel level);

/** The dead-key ids a mapping may carry. 0 means "this mapping is not a dead key". */
typedef enum InputDeadKey {
    /** Not a dead key. */
    RoboInputDeadKeyNone = 0,
    /** An acute accent: `´`, combining `U+0301`. */
    RoboInputDeadKeyAcute = 1,
    /** A grave accent: `` ` ``, combining `U+0300`. */
    RoboInputDeadKeyGrave = 2,
    /** A circumflex: `^`, combining `U+0302`. */
    RoboInputDeadKeyCircumflex = 3,
    /** A tilde: `~`, combining `U+0303`. */
    RoboInputDeadKeyTilde = 4,
    /** A diaeresis: `¨`, combining `U+0308`. */
    RoboInputDeadKeyDiaeresis = 5
} InputDeadKey;

/** Number of defined dead keys, including `NONE`. Asserted positionally by `input.layout.vocabulary`. */
#define RoboInputDeadKeyCount 6u

/** Non-zero if @p deadKey is a dead key this build defines and not `NONE`. */
int InputDeadKeyIsKnown(uint32_t deadKey);

/** Human-readable dead-key name; never NULL; `"unknown"` when undefined. */
const char *InputDeadKeyName(uint32_t deadKey);

/** The **spacing** form of @p deadKey (`´` for an acute): what a dead key produces when it stands alone. */
Uchar32 InputDeadKeySpacingScalar(uint32_t deadKey);

/** The **combining** form of @p deadKey (`U+0301` for an acute), or 0. */
Uchar32 InputDeadKeyCombiningScalar(uint32_t deadKey);

/**
 * Combine @p deadKey with @p baseScalar, or answer that it cannot.
 *
 * @return the composed scalar, or **0** when the pair has no precomposed form -- which is the case a caller
 *         must handle by emitting the dead key's spacing form and then the base, not by dropping either.
 *
 * The table is the layout's own, written down per pair (this header's comment), and it covers both cases of
 * the eight Latin letters these five accents compose with. Uppercase is listed rather than derived: the
 * Latin-1 offset that would derive it holds for every pair except `ÿ`, and a rule with one exception is a
 * rule that is wrong once.
 */
Uchar32 InputDeadKeyCompose(uint32_t deadKey, Uchar32 baseScalar);

/** One entry in a layout: this key, at this level, means this. */
typedef struct InputLayoutMapping {
    /** The key position. */
    PhysicalKey physical;
    /** The level this entry answers. */
    InputLayoutLevel level;
    /** The interpreted identity this key has at this level. */
    LogicalKey logical;
    /** The text this key produces, UTF-8, NUL-terminated, at most `RoboInputTextMax - 1` bytes. Empty for a
     *  key that produces none. */
    char text[RoboInputTextMax];
    /** The dead key this entry *is*, or @ref RoboInputDeadKeyNone. When set, @ref text is the dead key's
     *  spacing form and the pipeline holds it until the next key. */
    uint32_t deadKey;
    /** `RoboInputLayoutMappingFlag*` bits. */
    uint32_t flags;
} InputLayoutMapping;

/** Clear @p mapping. Safe on NULL. */
void InputLayoutMappingInit(InputLayoutMapping *mapping);

/**
 * Fill @p mapping.
 *
 * @param text     may be NULL (no text); longer than `RoboInputTextMax - 1` bytes is refused.
 * @param deadKey  @ref RoboInputDeadKeyNone, or a dead key this build defines -- and a dead key's mapping is
 *                 required to carry the key's **spacing** form as its text, because that is what the pipeline
 *                 emits when the next key cannot compose with it.
 */
Result InputLayoutMappingSet(InputLayoutMapping *mapping,
                             PhysicalKey physical,
                             InputLayoutLevel level,
                             LogicalKey logical,
                             const char *text,
                             uint32_t deadKey,
                             uint32_t flags,
                             InputDiag *diag);

/** Non-zero when @p mapping is a record a layout may hold. */
int InputLayoutMappingIsValid(const InputLayoutMapping *mapping);

/** Non-zero when @p mapping is cased (caps lock inverts its level). Safe on NULL (0). */
int InputLayoutMappingIsCased(const InputLayoutMapping *mapping);

/** Field-by-field equality, text compared as a string. Safe on NULL; two NULLs are equal. */
int InputLayoutMappingEqual(const InputLayoutMapping *a, const InputLayoutMapping *b);

/**
 * A layout, by value: a name, an id, and a bounded table of mappings.
 *
 * Held by value like `InputDeviceRegistry` and `InputKeyboardState` -- a stated bound, no allocator, nothing
 * to destroy. **The table is a binary search's worth of order and not a hash**: `InputLayoutSort` puts the
 * entries in `(physical, level)` order and `InputLayoutSetMapping` keeps them there, because determinism is
 * a requirement of the dumps and a test that added mappings in one order and read them in another would be a
 * test of insertion order.
 */
typedef struct InputLayout {
    /** Copied, NUL-terminated name; `""` when unnamed. */
    char name[RoboInputLayoutNameMax];
    /** Which built-in this is, or @ref RoboInputLayoutNone for a caller's own table. */
    InputLayoutId id;
    /** The mappings, in `(physical, level)` order. */
    InputLayoutMapping mappings[RoboInputLayoutMaxMappings];
    /** How many are live. */
    uint32_t count;
} InputLayout;

/** Clear @p layout: no name, no id, no mappings. Safe on NULL. */
void InputLayoutInit(InputLayout *layout);

/**
 * Build a layout the runtime ships.
 *
 * @param id @ref RoboInputLayoutUsAscii or @ref RoboInputLayoutUsInternational; `NONE` is refused.
 */
Result InputLayoutCreateBuiltin(InputLayout *layout, InputLayoutId id, InputDiag *diag);

/** Set the layout's copied name. NULL leaves it empty; too long is refused, never truncated. */
Result InputLayoutSetName(InputLayout *layout, const char *name, InputDiag *diag);

/**
 * Add or replace one mapping, keeping the table in `(physical, level)` order.
 *
 * Replacing is deliberate rather than refusing a duplicate: a caller building a layout from a file that
 * lists a key twice means the second entry, and a door that refused would make the file's order significant
 * in a way the format does not say. Refused with `RoboNotSupported` when the table is full and
 * `RoboInvalidArgument` for a mapping that is not valid.
 */
Result InputLayoutSetMapping(InputLayout *layout,
                             const InputLayoutMapping *mapping,
                             InputDiag *diag);

/** The mapping for @p physical at @p level, or `RoboNotFound` when the layout has none. */
Result InputLayoutLookup(const InputLayout *layout,
                         PhysicalKey physical,
                         InputLayoutLevel level,
                         InputLayoutMapping *out,
                         InputDiag *diag);

/** How many mappings a layout holds. 0 for NULL. */
uint32_t InputLayoutMappingCount(const InputLayout *layout);

/** The mapping at @p index in `(physical, level)` order, or `RoboNotFound` past the end. */
Result InputLayoutMappingAt(const InputLayout *layout,
                            uint32_t index,
                            InputLayoutMapping *out,
                            InputDiag *diag);

/** Sort the table into `(physical, level)` order. Idempotent; called by the setters. */
void InputLayoutSort(InputLayout *layout);

/**
 * The level a modifier state selects for a **cased** key (`Cased` non-zero) or an uncased one.
 *
 * The whole rule of this header, written once: shift is down, AltGr is down (`AltRight`, or `Ctrl` with
 * `Alt`), and caps lock inverts shift **only for a cased key**.
 *
 * Control and Meta are *not* levels: a caller asks this door for a chord that will produce text, and
 * `Ctrl`-without-`Alt` or `Meta` means the chord is a command rather than a character -- which the text
 * pipeline enforces, not this function, because "does this chord produce text at all?" is the pipeline's
 * question and this one is "at which level?".
 */
InputLayoutLevel InputLayoutLevelFor(InputModifiers modifiers, int cased);

/** Non-zero when @p modifiers is a **control chord**: it produces no text (`Ctrl` without `Alt`, or
 *  `Meta`). The pipeline's rule, exposed so a shortcut layer and a text layer agree about it. */
int InputModifiersIsControlChord(InputModifiers modifiers);

/**
 * Room `InputLayoutWrite` needs for the longest layout, NUL included.
 *
 * A summary line plus one line per mapping, which for the largest shipped layout is 96 lines of about forty
 * bytes. A bound on one buffer, stated so a caller sizes it once.
 */
#define RoboInputLayoutDumpMax (128u + (RoboInputLayoutMaxMappings * 64u))

/**
 * Write @p layout as text: a summary line, then one line per mapping in table order.
 *
 *     layout id=1 name=us-ascii mappings=96
 *     map A base logical=A text="a" flags=cased
 *     map A shift logical=A text="A" flags=cased
 *
 * `"?"` and the needed length when the buffer is too small.
 */
Result InputLayoutWrite(const InputLayout *layout,
                        char *buffer,
                        size_t capacity,
                        size_t *outLength,
                        InputDiag *diag);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_INPUT_LAYOUT_H */
