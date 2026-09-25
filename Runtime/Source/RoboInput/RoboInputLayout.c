/*
 * RoboInput keyboard layouts (Phase 13.9/13.10) — see `RoboInputLayout.h`.
 *
 * Four pieces:
 *
 *   **the level rule**    `InputLayoutLevelFor`, which is the whole semantics of a layout in one function:
 *                         shift, AltGr (two spellings of it) and caps lock applied only to cased keys.
 *   **the mapping table** a value record, an `(physical, level)` ordering the setters keep, and the three
 *                         doors that read it.
 *   **the built-ins**     two layouts built from two parallel strings and one key order, so the table is a
 *                         table rather than ninety-four hand-written lines, and adding a row is adding a
 *                         character to a string.
 *   **the dead keys**     five accents with a spacing form, a combining form and a compose table that holds
 *                         **both cases** of the eight letters they attach to.
 *
 * Nothing here shapes text, and nothing here reads a font: a layout ends at "this key, at this level, means
 * this character", which is exactly where 13.11's pipeline takes over.
 */

#include "RoboInput/RoboInputLayout.h"

#include "RoboInputInternal.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* The level rule                                                      */
/* ------------------------------------------------------------------ */

const char *InputLayoutIdName(InputLayoutId id)
{
    switch (id) {
    case RoboInputLayoutNone: return "none";
    case RoboInputLayoutUsAscii: return "us-ascii";
    case RoboInputLayoutUsInternational: return "us-international";
    default: break;
    }
    return "unknown";
}

int InputLayoutLevelIsKnown(InputLayoutLevel level)
{
    return (int)level >= 0 && (int)level < (int)RoboInputLayoutLevelCount;
}

const char *InputLayoutLevelName(InputLayoutLevel level)
{
    switch (level) {
    case RoboInputLayoutLevelBase: return "base";
    case RoboInputLayoutLevelShift: return "shift";
    case RoboInputLayoutLevelAltGr: return "altgr";
    case RoboInputLayoutLevelShiftAltGr: return "shift-altgr";
    default: break;
    }
    return "unknown";
}

int InputModifiersIsControlChord(InputModifiers modifiers)
{
    InputModifiers modifiersKnown = modifiers & (InputModifiers)RoboInputModifierAll;

    /* `Ctrl+Alt` is AltGr and therefore *not* a control chord: that pair is how two of the three platforms
     * in the world spell the third level, and a rule that treated it as a chord would make every AltGr
     * character unreachable. Meta is always a chord. */
    if (InputModifiersHas(modifiersKnown, RoboKeyModMeta)) {
        return 1;
    }
    if (InputModifiersHas(modifiersKnown, RoboKeyModCtrl) &&
        !InputModifiersHas(modifiersKnown, RoboKeyModAlt)) {
        return 1;
    }
    return 0;
}

InputLayoutLevel InputLayoutLevelFor(InputModifiers modifiers, int cased)
{
    InputModifiers modifiersKnown = modifiers & (InputModifiers)RoboInputModifierAll;
    int shift;
    int altGr;

    shift = InputModifiersHas(modifiersKnown, RoboKeyModShift) != 0;
    altGr = (InputModifiersHas(modifiersKnown, RoboKeyModAltRight) ||
             (InputModifiersHas(modifiersKnown, RoboKeyModCtrl) &&
              InputModifiersHas(modifiersKnown, RoboKeyModAlt))) != 0;

    /* Caps lock is a *case* lock, not a level: it inverts shift for a letter and does nothing to `1`, which
     * is why the mapping says whether it is cased (this header's comment). */
    if (cased && InputModifiersHas(modifiersKnown, RoboKeyModCapsLock)) {
        shift = !shift;
    }

    if (shift) {
        return altGr ? RoboInputLayoutLevelShiftAltGr : RoboInputLayoutLevelShift;
    }
    return altGr ? RoboInputLayoutLevelAltGr : RoboInputLayoutLevelBase;
}

/* ------------------------------------------------------------------ */
/* Dead keys                                                           */
/* ------------------------------------------------------------------ */

int InputDeadKeyIsKnown(uint32_t deadKey)
{
    return deadKey > 0u && deadKey < (uint32_t)RoboInputDeadKeyCount;
}

const char *InputDeadKeyName(uint32_t deadKey)
{
    switch (deadKey) {
    case RoboInputDeadKeyNone: return "none";
    case RoboInputDeadKeyAcute: return "acute";
    case RoboInputDeadKeyGrave: return "grave";
    case RoboInputDeadKeyCircumflex: return "circumflex";
    case RoboInputDeadKeyTilde: return "tilde";
    case RoboInputDeadKeyDiaeresis: return "diaeresis";
    default: break;
    }
    return "unknown";
}

Uchar32 InputDeadKeySpacingScalar(uint32_t deadKey)
{
    switch (deadKey) {
    case RoboInputDeadKeyAcute: return (Uchar32)0x00B4u;      /* ´ */
    case RoboInputDeadKeyGrave: return (Uchar32)0x0060u;      /* ` */
    case RoboInputDeadKeyCircumflex: return (Uchar32)0x005Eu; /* ^ */
    case RoboInputDeadKeyTilde: return (Uchar32)0x007Eu;      /* ~ */
    case RoboInputDeadKeyDiaeresis: return (Uchar32)0x00A8u;  /* ¨ */
    default: break;
    }
    return (Uchar32)0u;
}

Uchar32 InputDeadKeyCombiningScalar(uint32_t deadKey)
{
    switch (deadKey) {
    case RoboInputDeadKeyAcute: return (Uchar32)0x0301u;
    case RoboInputDeadKeyGrave: return (Uchar32)0x0300u;
    case RoboInputDeadKeyCircumflex: return (Uchar32)0x0302u;
    case RoboInputDeadKeyTilde: return (Uchar32)0x0303u;
    case RoboInputDeadKeyDiaeresis: return (Uchar32)0x0308u;
    default: break;
    }
    return (Uchar32)0u;
}

/**
 * One letter's five accented forms, or 0 where the pair has none.
 *
 * The table is written out in **both cases** rather than derived: the Latin-1 trick of "add 0x20" holds for
 * every pair these accents make except `ÿ`/`Ÿ`, and a rule with an exception is a rule that is wrong once
 * (this header's comment).
 */
typedef struct LayoutAccentRow {
    Uchar32 base;
    Uchar32 acute;
    Uchar32 grave;
    Uchar32 circumflex;
    Uchar32 tilde;
    Uchar32 diaeresis;
} LayoutAccentRow;

static const LayoutAccentRow layoutAccentRows[] = {
    /* base   acute   grave   circumflex tilde   diaeresis */
    { 'a', 0x00E1u, 0x00E0u, 0x00E2u, 0x00E3u, 0x00E4u },
    { 'e', 0x00E9u, 0x00E8u, 0x00EAu, 0x0000u, 0x00EBu },
    { 'i', 0x00EDu, 0x00ECu, 0x00EEu, 0x0000u, 0x00EFu },
    { 'o', 0x00F3u, 0x00F2u, 0x00F4u, 0x00F5u, 0x00F6u },
    { 'u', 0x00FAu, 0x00F9u, 0x00FBu, 0x0000u, 0x00FCu },
    { 'y', 0x00FDu, 0x0000u, 0x0000u, 0x0000u, 0x00FFu },
    { 'n', 0x0000u, 0x0000u, 0x0000u, 0x00F1u, 0x0000u },
    { 'A', 0x00C1u, 0x00C0u, 0x00C2u, 0x00C3u, 0x00C4u },
    { 'E', 0x00C9u, 0x00C8u, 0x00CAu, 0x0000u, 0x00CBu },
    { 'I', 0x00CDu, 0x00CCu, 0x00CEu, 0x0000u, 0x00CFu },
    { 'O', 0x00D3u, 0x00D2u, 0x00D4u, 0x00D5u, 0x00D6u },
    { 'U', 0x00DAu, 0x00D9u, 0x00DBu, 0x0000u, 0x00DCu },
    { 'Y', 0x00DDu, 0x0000u, 0x0000u, 0x0000u, 0x0178u },
    { 'N', 0x0000u, 0x0000u, 0x0000u, 0x00D1u, 0x0000u }
};

Uchar32 InputDeadKeyCompose(uint32_t deadKey, Uchar32 baseScalar)
{
    size_t index;
    size_t count = sizeof(layoutAccentRows) / sizeof(layoutAccentRows[0]);

    if (!InputDeadKeyIsKnown(deadKey) || baseScalar == (Uchar32)0u) {
        return (Uchar32)0u;
    }
    for (index = 0u; index < count; index++) {
        Uchar32 composed;

        if (layoutAccentRows[index].base != baseScalar) {
            continue;
        }
        switch (deadKey) {
        case RoboInputDeadKeyAcute: composed = layoutAccentRows[index].acute; break;
        case RoboInputDeadKeyGrave: composed = layoutAccentRows[index].grave; break;
        case RoboInputDeadKeyCircumflex: composed = layoutAccentRows[index].circumflex; break;
        case RoboInputDeadKeyTilde: composed = layoutAccentRows[index].tilde; break;
        case RoboInputDeadKeyDiaeresis: composed = layoutAccentRows[index].diaeresis; break;
        default: return (Uchar32)0u;
        }
        /* Zero means "this pair has no precomposed form", which is why the table stores 0 rather than
         * leaving a gap: the row's *shape* says which pairs exist. */
        return composed;
    }
    return (Uchar32)0u;
}

/* ------------------------------------------------------------------ */
/* The mapping table                                                   */
/* ------------------------------------------------------------------ */

void InputLayoutMappingInit(InputLayoutMapping *mapping)
{
    if (!mapping) {
        return;
    }
    mapping->physical = RoboPhysicalKeyUnknown;
    mapping->level = RoboInputLayoutLevelBase;
    mapping->logical = RoboLogicalKeyUnknown;
    mapping->text[0] = '\0';
    mapping->deadKey = (uint32_t)RoboInputDeadKeyNone;
    mapping->flags = 0u;
}

Result InputLayoutMappingSet(InputLayoutMapping *mapping,
                             PhysicalKey physical,
                             InputLayoutLevel level,
                             LogicalKey logical,
                             const char *text,
                             uint32_t deadKey,
                             uint32_t flags,
                             InputDiag *diag)
{
    InputLayoutMapping filled;

    if (!mapping) {
        return RoboInputDiag(diag, RoboInputDomainEvent, RoboInvalidArgument,
                             "nowhere to put the mapping");
    }
    if (physical == RoboPhysicalKeyUnknown) {
        return RoboInputDiag(diag, RoboInputDomainEvent, RoboInvalidArgument,
                             "a mapping needs a key position");
    }
    if (!InputLayoutLevelIsKnown(level)) {
        return RoboInputDiag(diag, RoboInputDomainEvent, RoboInvalidArgument,
                             "a mapping needs a level this build defines");
    }
    if ((flags & ~(uint32_t)RoboInputLayoutMappingFlagAll) != 0u) {
        return RoboInputDiag(diag, RoboInputDomainEvent, RoboInvalidArgument,
                             "mapping flags carry a bit this build does not define");
    }
    if (deadKey != (uint32_t)RoboInputDeadKeyNone && !InputDeadKeyIsKnown(deadKey)) {
        return RoboInputDiag(diag, RoboInputDomainEvent, RoboInvalidArgument,
                             "a mapping names a dead key this build does not define");
    }
    if (!RiTextFits(text, RoboInputTextMax)) {
        return RoboInputDiag(diag, RoboInputDomainEvent, RoboInvalidArgument,
                             "the mapping's text is longer than one keystroke may carry");
    }
    if (!LogicalKeyIsKnown(logical)) {
        return RoboInputDiag(diag, RoboInputDomainEvent, RoboInvalidArgument,
                             "a mapping needs an interpreted identity this build defines");
    }

    InputLayoutMappingInit(&filled);
    filled.physical = physical;
    filled.level = level;
    filled.logical = logical;
    filled.deadKey = deadKey;
    filled.flags = flags;
    RiTextCopy(filled.text, RoboInputTextMax, text);
    /* A dead key's mapping **must** carry its spacing form, because that is what the pipeline emits the day
     * the next key cannot compose with it -- and a dead key with no spacing form would be a key that
     * swallows itself. */
    if (deadKey != (uint32_t)RoboInputDeadKeyNone && filled.text[0] == '\0') {
        return RoboInputDiag(diag, RoboInputDomainEvent, RoboInvalidArgument,
                             "a dead key's mapping must carry its spacing form as its text");
    }

    *mapping = filled;
    return RoboOk;
}

int InputLayoutMappingIsValid(const InputLayoutMapping *mapping)
{
    if (!mapping) {
        return 0;
    }
    if (mapping->physical == RoboPhysicalKeyUnknown ||
        !PhysicalKeyIsKnown(mapping->physical)) {
        return 0;
    }
    if (!InputLayoutLevelIsKnown(mapping->level)) {
        return 0;
    }
    if (!LogicalKeyIsKnown(mapping->logical)) {
        return 0;
    }
    if ((mapping->flags & ~(uint32_t)RoboInputLayoutMappingFlagAll) != 0u) {
        return 0;
    }
    if (mapping->deadKey != (uint32_t)RoboInputDeadKeyNone &&
        !InputDeadKeyIsKnown(mapping->deadKey)) {
        return 0;
    }
    if (!RiTextFits(mapping->text, RoboInputTextMax)) {
        return 0;
    }
    if (mapping->deadKey != (uint32_t)RoboInputDeadKeyNone && mapping->text[0] == '\0') {
        return 0;
    }
    return 1;
}

int InputLayoutMappingIsCased(const InputLayoutMapping *mapping)
{
    return mapping != NULL &&
           (mapping->flags & (uint32_t)RoboInputLayoutMappingFlagCased) != 0u;
}

int InputLayoutMappingEqual(const InputLayoutMapping *a, const InputLayoutMapping *b)
{
    if (a == b) {
        return 1;
    }
    if (!a || !b) {
        return 0;
    }
    return a->physical == b->physical && a->level == b->level && a->logical == b->logical &&
           a->deadKey == b->deadKey && a->flags == b->flags && strcmp(a->text, b->text) == 0;
}

/* ------------------------------------------------------------------ */
/* The layout                                                          */
/* ------------------------------------------------------------------ */

void InputLayoutInit(InputLayout *layout)
{
    uint32_t index;

    if (!layout) {
        return;
    }
    layout->name[0] = '\0';
    layout->id = RoboInputLayoutNone;
    for (index = 0u; index < RoboInputLayoutMaxMappings; index++) {
        InputLayoutMappingInit(&layout->mappings[index]);
    }
    layout->count = 0u;
}

/** Order the table by `(physical, level)`: the order every reader and the dump rely on. */
static int LayoutMappingPrecedes(const InputLayoutMapping *a, const InputLayoutMapping *b)
{
    if (a->physical != b->physical) {
        return (int)a->physical < (int)b->physical;
    }
    return (int)a->level < (int)b->level;
}

void InputLayoutSort(InputLayout *layout)
{
    uint32_t outer;

    if (!layout || layout->count < 2u) {
        return;
    }
    /* Insertion sort: the table is at most 128 entries and is nearly ordered already, because the setters
     * keep it that way. A sort whose cost is invisible is worth more than a library. */
    for (outer = 1u; outer < layout->count; outer++) {
        InputLayoutMapping key = layout->mappings[outer];
        uint32_t inner = outer;

        while (inner > 0u && LayoutMappingPrecedes(&key, &layout->mappings[inner - 1u])) {
            layout->mappings[inner] = layout->mappings[inner - 1u];
            inner--;
        }
        layout->mappings[inner] = key;
    }
}

Result InputLayoutSetName(InputLayout *layout, const char *name, InputDiag *diag)
{
    if (!layout) {
        return RoboInputDiag(diag, RoboInputDomainEvent, RoboInvalidArgument,
                             "no layout to name");
    }
    if (!RiTextFits(name, RoboInputLayoutNameMax)) {
        return RoboInputDiag(diag, RoboInputDomainEvent, RoboInvalidArgument,
                             "the layout name is longer than the record can hold");
    }
    RiTextCopy(layout->name, RoboInputLayoutNameMax, name);
    return RoboOk;
}

Result InputLayoutSetMapping(InputLayout *layout,
                             const InputLayoutMapping *mapping,
                             InputDiag *diag)
{
    uint32_t index;

    if (!layout || !mapping) {
        return RoboInputDiag(diag, RoboInputDomainEvent, RoboInvalidArgument,
                             "a layout and a mapping are both needed");
    }
    if (!InputLayoutMappingIsValid(mapping)) {
        return RoboInputDiag(diag, RoboInputDomainEvent, RoboInvalidArgument,
                             "the mapping is not one a layout may hold");
    }
    for (index = 0u; index < layout->count; index++) {
        if (layout->mappings[index].physical == mapping->physical &&
            layout->mappings[index].level == mapping->level) {
            /* Replacing rather than refusing: a file that lists a key twice means the second entry, and a
             * door that refused would make the file's order significant. */
            layout->mappings[index] = *mapping;
            InputLayoutSort(layout);
            return RoboOk;
        }
    }
    if (layout->count >= RoboInputLayoutMaxMappings) {
        return RoboInputDiag(diag, RoboInputDomainEvent, RoboNotSupported,
                             "the layout's mapping table is full");
    }
    layout->mappings[layout->count] = *mapping;
    layout->count++;
    InputLayoutSort(layout);
    return RoboOk;
}

Result InputLayoutLookup(const InputLayout *layout,
                         PhysicalKey physical,
                         InputLayoutLevel level,
                         InputLayoutMapping *out,
                         InputDiag *diag)
{
    uint32_t index;

    if (!layout || !out) {
        return RoboInputDiag(diag, RoboInputDomainEvent, RoboInvalidArgument,
                             "a layout and somewhere to put the mapping are both needed");
    }
    if (!InputLayoutLevelIsKnown(level)) {
        return RoboInputDiag(diag, RoboInputDomainEvent, RoboInvalidArgument,
                             "no such level");
    }
    /* A linear scan over a sorted table, because the table is bounded at 128 and a binary search would be
     * four more lines for a cost nobody can measure. The *order* still matters: it is what the dump and the
     * table's own determinism rest on. */
    for (index = 0u; index < layout->count; index++) {
        if (layout->mappings[index].physical == physical &&
            layout->mappings[index].level == level) {
            *out = layout->mappings[index];
            return RoboOk;
        }
    }
    return RoboInputDiag(diag, RoboInputDomainEvent, RoboNotFound,
                         "the layout has no mapping for that key at that level");
}

uint32_t InputLayoutMappingCount(const InputLayout *layout)
{
    return layout ? layout->count : 0u;
}

Result InputLayoutMappingAt(const InputLayout *layout,
                            uint32_t index,
                            InputLayoutMapping *out,
                            InputDiag *diag)
{
    if (!layout || !out) {
        return RoboInputDiag(diag, RoboInputDomainEvent, RoboInvalidArgument,
                             "a layout and somewhere to put the mapping are both needed");
    }
    if (index >= layout->count) {
        return RoboInputDiag(diag, RoboInputDomainEvent, RoboNotFound,
                             "no mapping at that index");
    }
    *out = layout->mappings[index];
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* The built-ins                                                       */
/* ------------------------------------------------------------------ */

/**
 * The key order the two strings below are read against: the writing-system row, the digit row, the
 * punctuation row, then space. **The positions are QWERTY's**, which is what `PhysicalKey` names -- a layout
 * maps positions to characters, so a Dvorak layout is the same table with a different string rather than a
 * different key order.
 */
static const PhysicalKey layoutUsKeyOrder[] = {
    RoboPhysicalKeyA, RoboPhysicalKeyB, RoboPhysicalKeyC, RoboPhysicalKeyD, RoboPhysicalKeyE,
    RoboPhysicalKeyF, RoboPhysicalKeyG, RoboPhysicalKeyH, RoboPhysicalKeyI, RoboPhysicalKeyJ,
    RoboPhysicalKeyK, RoboPhysicalKeyL, RoboPhysicalKeyM, RoboPhysicalKeyN, RoboPhysicalKeyO,
    RoboPhysicalKeyP, RoboPhysicalKeyQ, RoboPhysicalKeyR, RoboPhysicalKeyS, RoboPhysicalKeyT,
    RoboPhysicalKeyU, RoboPhysicalKeyV, RoboPhysicalKeyW, RoboPhysicalKeyX, RoboPhysicalKeyY,
    RoboPhysicalKeyZ,
    RoboPhysicalKeyDigit1, RoboPhysicalKeyDigit2, RoboPhysicalKeyDigit3, RoboPhysicalKeyDigit4,
    RoboPhysicalKeyDigit5, RoboPhysicalKeyDigit6, RoboPhysicalKeyDigit7, RoboPhysicalKeyDigit8,
    RoboPhysicalKeyDigit9, RoboPhysicalKeyDigit0,
    RoboPhysicalKeyMinus, RoboPhysicalKeyEqual, RoboPhysicalKeyBracketLeft,
    RoboPhysicalKeyBracketRight, RoboPhysicalKeyBackslash, RoboPhysicalKeySemicolon,
    RoboPhysicalKeyQuote, RoboPhysicalKeyBackquote, RoboPhysicalKeyComma, RoboPhysicalKeyPeriod,
    RoboPhysicalKeySlash,
    RoboPhysicalKeySpace
};

/**
 * What each of those keys produces, one character per entry, **in the key order above**.
 *
 * Written as arrays rather than string literals so the compiler counts them. Two parallel strings are one
 * deleted character away from shifting every mapping after it, which is a defect nobody can see; a pair of
 * arrays whose lengths are checked against the key order cannot have it.
 *
 * The base row's punctuation is `- = [ ] \ ; ' ` , . /` and the shift row's is `_ + { } | : " ~ < > ?`, in
 * the same order -- US QWERTY's own, which is what makes this table readable against a keyboard.
 */
static const char layoutUsBaseChars[] = {
    'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm',
    'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z',
    '1', '2', '3', '4', '5', '6', '7', '8', '9', '0',
    '-', '=', '[', ']', '\\', ';', '\'', '`', ',', '.', '/',
    ' '
};

/** The same row with shift down: the writing-system row cased, and the punctuation row's upper forms. */
static const char layoutUsShiftChars[] = {
    'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M',
    'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z',
    '!', '@', '#', '$', '%', '^', '&', '*', '(', ')',
    '_', '+', '{', '}', '|', ':', '"', '~', '<', '>', '?',
    ' '
};

/** The logical identity a letter key has, from its base character. */
static LogicalKey LayoutLetterLogical(char base)
{
    switch (base) {
    case 'a': return RoboLogicalKeyA;
    case 'b': return RoboLogicalKeyB;
    case 'c': return RoboLogicalKeyC;
    case 'd': return RoboLogicalKeyD;
    case 'e': return RoboLogicalKeyE;
    case 'f': return RoboLogicalKeyF;
    case 'g': return RoboLogicalKeyG;
    case 'h': return RoboLogicalKeyH;
    case 'i': return RoboLogicalKeyI;
    case 'j': return RoboLogicalKeyJ;
    case 'k': return RoboLogicalKeyK;
    case 'l': return RoboLogicalKeyL;
    case 'm': return RoboLogicalKeyM;
    case 'n': return RoboLogicalKeyN;
    case 'o': return RoboLogicalKeyO;
    case 'p': return RoboLogicalKeyP;
    case 'q': return RoboLogicalKeyQ;
    case 'r': return RoboLogicalKeyR;
    case 's': return RoboLogicalKeyS;
    case 't': return RoboLogicalKeyT;
    case 'u': return RoboLogicalKeyU;
    case 'v': return RoboLogicalKeyV;
    case 'w': return RoboLogicalKeyW;
    case 'x': return RoboLogicalKeyX;
    case 'y': return RoboLogicalKeyY;
    case 'z': return RoboLogicalKeyZ;
    default: return RoboLogicalKeyUnknown;
    }
}

/** The logical identity a digit or punctuation key has, from its base character. */
static LogicalKey LayoutSymbolLogical(char base)
{
    switch (base) {
    case '1': return RoboLogicalKeyDigit1;
    case '2': return RoboLogicalKeyDigit2;
    case '3': return RoboLogicalKeyDigit3;
    case '4': return RoboLogicalKeyDigit4;
    case '5': return RoboLogicalKeyDigit5;
    case '6': return RoboLogicalKeyDigit6;
    case '7': return RoboLogicalKeyDigit7;
    case '8': return RoboLogicalKeyDigit8;
    case '9': return RoboLogicalKeyDigit9;
    case '0': return RoboLogicalKeyDigit0;
    case '-': return RoboLogicalKeyMinus;
    case '=': return RoboLogicalKeyEqual;
    case '[': return RoboLogicalKeyBracketLeft;
    case ']': return RoboLogicalKeyBracketRight;
    case '\\': return RoboLogicalKeyBackslash;
    case ';': return RoboLogicalKeySemicolon;
    case '\'': return RoboLogicalKeyQuote;
    case '`': return RoboLogicalKeyBackquote;
    case ',': return RoboLogicalKeyComma;
    case '.': return RoboLogicalKeyPeriod;
    case '/': return RoboLogicalKeySlash;
    case ' ': return RoboLogicalKeySpace;
    default: return RoboLogicalKeyUnknown;
    }
}

/** Encode @p scalar into @p out as UTF-8, through RoboUnicode's encoder; empty on a refusal. */
static void TextScalarToUtf8(Uchar32 scalar, char *out, size_t capacity, InputDiag *diag)
{
    size_t written = 0u;

    out[0] = '\0';
    if (scalar == (Uchar32)0u ||
        UnicodeUtf8Encode(scalar, out, capacity - 1u, &written) != RoboUnicodeOk) {
        RoboInputDiag(diag, RoboInputDomainEvent, RoboError,
                      "a dead key's spacing form could not be encoded");
        return;
    }
    out[written] = '\0';
}

/** The dead key a *base-level* character is, in the international layout, or `NONE`. */
static uint32_t LayoutInternationalDeadKey(char base)
{
    switch (base) {
    case '\'': return (uint32_t)RoboInputDeadKeyAcute;
    case '`': return (uint32_t)RoboInputDeadKeyGrave;
    case '^': return (uint32_t)RoboInputDeadKeyCircumflex;
    case '~': return (uint32_t)RoboInputDeadKeyTilde;
    case '"': return (uint32_t)RoboInputDeadKeyDiaeresis;
    default: return (uint32_t)RoboInputDeadKeyNone;
    }
}

/** One key's two mappings: base and shift, each a character or a dead accent. */
static Result LayoutAddKey(InputLayout *layout,
                           PhysicalKey physical,
                           char baseChar,
                           char shiftChar,
                           uint32_t baseDeadKey,
                           uint32_t shiftDeadKey,
                           InputDiag *diag)
{
    InputLayoutMapping mapping;
    LogicalKey logical;
    int cased = 0;
    /* Two buffers, not one: the quote key is a dead acute at base and a dead diaeresis at shift, so a
     * shared buffer would give the base mapping the shift level's accent. */
    char baseText[8];
    char shiftText[8];

    if (baseChar >= 'a' && baseChar <= 'z') {
        logical = LayoutLetterLogical(baseChar);
        cased = 1;
    } else {
        logical = LayoutSymbolLogical(baseChar);
    }
    if (logical == RoboLogicalKeyUnknown) {
        /* A key the order lists but the rows have no identity for: refused rather than mapped to nothing, so
         * a typo is a build-time refusal rather than a key that silently does nothing. */
        return RoboInputDiag(diag, RoboInputDomainEvent, RoboParseError,
                             "the built-in layout lists a key whose base character is unmapped");
    }

    /* A dead key's *text* is its spacing form -- the character a user sees when the accent stands alone --
     * encoded through RoboUnicode's own encoder, so there is no second UTF-8 encoder in this tree. A live
     * key's text is its one character. */
    baseText[0] = baseChar;
    baseText[1] = '\0';
    shiftText[0] = shiftChar;
    shiftText[1] = '\0';
    if (baseDeadKey != (uint32_t)RoboInputDeadKeyNone) {
        TextScalarToUtf8(InputDeadKeySpacingScalar(baseDeadKey), baseText, sizeof(baseText), diag);
        if (baseText[0] == '\0') {
            return diag ? diag->code : RoboError;
        }
    }
    if (shiftDeadKey != (uint32_t)RoboInputDeadKeyNone) {
        TextScalarToUtf8(InputDeadKeySpacingScalar(shiftDeadKey), shiftText, sizeof(shiftText), diag);
        if (shiftText[0] == '\0') {
            return diag ? diag->code : RoboError;
        }
    }

    InputLayoutMappingInit(&mapping);
    if (InputLayoutMappingSet(&mapping, physical, RoboInputLayoutLevelBase, logical, baseText,
                              baseDeadKey, cased ? RoboInputLayoutMappingFlagCased : 0u,
                              diag) != RoboOk) {
        return diag ? diag->code : RoboInvalidArgument;
    }
    if (InputLayoutSetMapping(layout, &mapping, diag) != RoboOk) {
        return diag ? diag->code : RoboError;
    }

    InputLayoutMappingInit(&mapping);
    if (InputLayoutMappingSet(&mapping, physical, RoboInputLayoutLevelShift, logical, shiftText,
                              shiftDeadKey, cased ? RoboInputLayoutMappingFlagCased : 0u,
                              diag) != RoboOk) {
        return diag ? diag->code : RoboInvalidArgument;
    }
    return InputLayoutSetMapping(layout, &mapping, diag);
}

Result InputLayoutCreateBuiltin(InputLayout *layout, InputLayoutId id, InputDiag *diag)
{
    uint32_t index;
    size_t keyCount;
    int international;

    if (!layout) {
        return RoboInputDiag(diag, RoboInputDomainEvent, RoboInvalidArgument,
                             "nowhere to put the layout");
    }
    if (id != RoboInputLayoutUsAscii && id != RoboInputLayoutUsInternational) {
        return RoboInputDiag(diag, RoboInputDomainEvent, RoboInvalidArgument,
                             "no such built-in layout");
    }
    international = id == RoboInputLayoutUsInternational;
    if (sizeof(layoutUsBaseChars) != sizeof(layoutUsShiftChars)) {
        return RoboInputDiag(diag, RoboInputDomainEvent, RoboError,
                             "the built-in layout's two rows disagree about their length");
    }

    InputLayoutInit(layout);
    layout->id = id;
    if (InputLayoutSetName(layout, InputLayoutIdName(id), diag) != RoboOk) {
        return diag ? diag->code : RoboError;
    }

    keyCount = sizeof(layoutUsKeyOrder) / sizeof(layoutUsKeyOrder[0]);
    if (keyCount != sizeof(layoutUsBaseChars) || keyCount != sizeof(layoutUsShiftChars)) {
        /* A row shorter than the key order would leave the tail unmapped and a row longer would read past
         * it, so a typo in either is a refusal rather than a key that quietly does nothing. */
        return RoboInputDiag(diag, RoboInputDomainEvent, RoboError,
                             "the built-in layout's key order and character rows disagree");
    }

    for (index = 0u; index < keyCount; index++) {
        char baseChar = layoutUsBaseChars[index];
        char shiftChar = layoutUsShiftChars[index];
        uint32_t baseDead = international ? LayoutInternationalDeadKey(baseChar)
                                          : (uint32_t)RoboInputDeadKeyNone;
        uint32_t shiftDead = international ? LayoutInternationalDeadKey(shiftChar)
                                           : (uint32_t)RoboInputDeadKeyNone;

        if (LayoutAddKey(layout, layoutUsKeyOrder[index], baseChar, shiftChar, baseDead,
                         shiftDead, diag) != RoboOk) {
            return diag ? diag->code : RoboError;
        }
    }

    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* The dump                                                            */
/* ------------------------------------------------------------------ */

Result InputLayoutWrite(const InputLayout *layout,
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
    if (!layout) {
        RiWriterText(&text, "layout <none>\n");
        return RiWriterFinish(&text, outLength, diag);
    }

    RiWriterFieldU64(&text, "layout id", (uint64_t)layout->id);
    RiWriterFieldText(&text, " name", layout->name);
    RiWriterFieldU64(&text, " mappings", (uint64_t)layout->count);
    RiWriterChar(&text, '\n');

    for (index = 0u; index < layout->count; index++) {
        const InputLayoutMapping *mapping = &layout->mappings[index];

        RiWriterText(&text, "map ");
        RiWriterText(&text, PhysicalKeyName(mapping->physical));
        RiWriterChar(&text, ' ');
        RiWriterText(&text, InputLayoutLevelName(mapping->level));
        RiWriterFieldText(&text, " logical", LogicalKeyName(mapping->logical));
        RiWriterText(&text, " text=\"");
        RiWriterText(&text, mapping->text);
        RiWriterChar(&text, '"');
        if (mapping->deadKey != (uint32_t)RoboInputDeadKeyNone) {
            RiWriterFieldText(&text, " dead", InputDeadKeyName(mapping->deadKey));
        }
        if (InputLayoutMappingIsCased(mapping)) {
            RiWriterText(&text, " flags=cased");
        }
        RiWriterChar(&text, '\n');
    }
    return RiWriterFinish(&text, outLength, diag);
}
