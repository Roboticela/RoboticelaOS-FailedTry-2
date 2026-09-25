/*
 * RoboInput keyboard layout suite (Phase 13.9/13.10) — `input.layout.*`.
 *
 * Seven cases, and the ones that carry the weight are the *rules* rather than the tables:
 *
 *   `vocabulary`   the three layout ids, the four levels and the six dead keys, asserted positionally with
 *                  their names, because they are the numbers a saved recording carries;
 *
 *   `levels`       `InputLayoutLevelFor`, which is the whole semantics of a layout in one function: shift,
 *                  the two spellings of AltGr, and caps lock applied **only** to a cased key -- which is
 *                  the rule that makes `Shift+1` still `!` with caps lock on;
 *
 *   `mapping`      one mapping's own consistency and its refusals, including the two that are contracts
 *                  rather than typo guards: a flag bit this build does not define, and a dead key whose
 *                  mapping carries no spacing form (a key that would swallow itself);
 *
 *   `builtin`      both shipped tables: their size, their order, and the lookups that decide whether they
 *                  are right -- `a`/`A`, `1`/`!`, space, an unmapped key, and the international table's five
 *                  dead accents on `'`, `` ` ``, `^`, `~` and `"`;
 *
 *   `capslock`     the rule asserted through the *table* rather than the level function, because that is
 *                  where a cased flag that was never set would show up;
 *
 *   `table`        `SetMapping`'s three behaviours: add, replace in place, and the bounded table refusing
 *                  rather than dropping;
 *
 *   `dump`         the text, asserted for its summary line and its determinism, and the narrow buffer
 *                  answering `?` rather than a prefix that reads as a whole layout.
 *
 * `InputDeadKeyCompose` is asserted over one row per accent plus every *negative* that matters (`tilde + e`
 * has no form, and neither does `grave + y`): a compose table that returned something for those would
 * insert a character nobody typed.
 */

#include "RoboInputTestSupport.h"

/* ------------------------------------------------------------------ */
/* Vocabulary                                                          */
/* ------------------------------------------------------------------ */

static void TestLayoutVocabulary(void)
{
    RoboCheckEqInt((int)RoboInputLayoutNone, 0);
    RoboCheckEqInt((int)RoboInputLayoutUsAscii, 1);
    RoboCheckEqInt((int)RoboInputLayoutUsInternational, 2);
    RoboCheckEqInt((int)RoboInputLayoutIdCount, 3);
    RoboCheckStrEq(InputLayoutIdName(RoboInputLayoutNone), "none");
    RoboCheckStrEq(InputLayoutIdName(RoboInputLayoutUsAscii), "us-ascii");
    RoboCheckStrEq(InputLayoutIdName(RoboInputLayoutUsInternational), "us-international");
    RoboCheckStrEq(InputLayoutIdName((InputLayoutId)99), "unknown");

    RoboCheckEqInt((int)RoboInputLayoutLevelBase, 0);
    RoboCheckEqInt((int)RoboInputLayoutLevelShift, 1);
    RoboCheckEqInt((int)RoboInputLayoutLevelAltGr, 2);
    RoboCheckEqInt((int)RoboInputLayoutLevelShiftAltGr, 3);
    RoboCheckEqInt((int)RoboInputLayoutLevelCount, 4);
    RoboCheckStrEq(InputLayoutLevelName(RoboInputLayoutLevelBase), "base");
    RoboCheckStrEq(InputLayoutLevelName(RoboInputLayoutLevelShift), "shift");
    RoboCheckStrEq(InputLayoutLevelName(RoboInputLayoutLevelAltGr), "altgr");
    RoboCheckStrEq(InputLayoutLevelName(RoboInputLayoutLevelShiftAltGr), "shift-altgr");
    RoboCheckStrEq(InputLayoutLevelName((InputLayoutLevel)9), "unknown");
    RoboCheck(InputLayoutLevelIsKnown(RoboInputLayoutLevelBase));
    RoboCheck(InputLayoutLevelIsKnown(RoboInputLayoutLevelShiftAltGr));
    RoboCheck(!InputLayoutLevelIsKnown((InputLayoutLevel)4));
    RoboCheck(!InputLayoutLevelIsKnown((InputLayoutLevel)-1));

    RoboCheckEqInt((int)RoboInputDeadKeyNone, 0);
    RoboCheckEqInt((int)RoboInputDeadKeyAcute, 1);
    RoboCheckEqInt((int)RoboInputDeadKeyGrave, 2);
    RoboCheckEqInt((int)RoboInputDeadKeyCircumflex, 3);
    RoboCheckEqInt((int)RoboInputDeadKeyTilde, 4);
    RoboCheckEqInt((int)RoboInputDeadKeyDiaeresis, 5);
    RoboCheckEqInt((int)RoboInputDeadKeyCount, 6);
    RoboCheckStrEq(InputDeadKeyName(RoboInputDeadKeyNone), "none");
    RoboCheckStrEq(InputDeadKeyName(RoboInputDeadKeyAcute), "acute");
    RoboCheckStrEq(InputDeadKeyName(RoboInputDeadKeyGrave), "grave");
    RoboCheckStrEq(InputDeadKeyName(RoboInputDeadKeyCircumflex), "circumflex");
    RoboCheckStrEq(InputDeadKeyName(RoboInputDeadKeyTilde), "tilde");
    RoboCheckStrEq(InputDeadKeyName(RoboInputDeadKeyDiaeresis), "diaeresis");
    RoboCheckStrEq(InputDeadKeyName(99u), "unknown");
    RoboCheck(!InputDeadKeyIsKnown((uint32_t)RoboInputDeadKeyNone));
    RoboCheck(InputDeadKeyIsKnown((uint32_t)RoboInputDeadKeyAcute));
    RoboCheck(InputDeadKeyIsKnown((uint32_t)RoboInputDeadKeyDiaeresis));
    RoboCheck(!InputDeadKeyIsKnown((uint32_t)RoboInputDeadKeyCount));

    /* The spacing form is what a user sees when the accent stands alone; the combining form is what a
     * *mark* is. Both are asserted, because the pipeline emits the first and a future IME would need the
     * second. */
    RoboCheckEqInt((int)InputDeadKeySpacingScalar((uint32_t)RoboInputDeadKeyAcute), 0x00B4);
    RoboCheckEqInt((int)InputDeadKeySpacingScalar((uint32_t)RoboInputDeadKeyGrave), 0x0060);
    RoboCheckEqInt((int)InputDeadKeySpacingScalar((uint32_t)RoboInputDeadKeyCircumflex), 0x005E);
    RoboCheckEqInt((int)InputDeadKeySpacingScalar((uint32_t)RoboInputDeadKeyTilde), 0x007E);
    RoboCheckEqInt((int)InputDeadKeySpacingScalar((uint32_t)RoboInputDeadKeyDiaeresis), 0x00A8);
    RoboCheckEqInt((int)InputDeadKeySpacingScalar((uint32_t)RoboInputDeadKeyNone), 0);
    RoboCheckEqInt((int)InputDeadKeySpacingScalar(99u), 0);
    RoboCheckEqInt((int)InputDeadKeyCombiningScalar((uint32_t)RoboInputDeadKeyAcute), 0x0301);
    RoboCheckEqInt((int)InputDeadKeyCombiningScalar((uint32_t)RoboInputDeadKeyGrave), 0x0300);
    RoboCheckEqInt((int)InputDeadKeyCombiningScalar((uint32_t)RoboInputDeadKeyCircumflex), 0x0302);
    RoboCheckEqInt((int)InputDeadKeyCombiningScalar((uint32_t)RoboInputDeadKeyTilde), 0x0303);
    RoboCheckEqInt((int)InputDeadKeyCombiningScalar((uint32_t)RoboInputDeadKeyDiaeresis), 0x0308);
    RoboCheckEqInt((int)InputDeadKeyCombiningScalar((uint32_t)RoboInputDeadKeyNone), 0);

    /* One pair per accent, then the ones the table deliberately has no answer for. */
    RoboCheckEqInt((int)InputDeadKeyCompose((uint32_t)RoboInputDeadKeyAcute, 'e'), 0x00E9);
    RoboCheckEqInt((int)InputDeadKeyCompose((uint32_t)RoboInputDeadKeyGrave, 'a'), 0x00E0);
    RoboCheckEqInt((int)InputDeadKeyCompose((uint32_t)RoboInputDeadKeyCircumflex, 'i'), 0x00EE);
    RoboCheckEqInt((int)InputDeadKeyCompose((uint32_t)RoboInputDeadKeyTilde, 'n'), 0x00F1);
    RoboCheckEqInt((int)InputDeadKeyCompose((uint32_t)RoboInputDeadKeyTilde, 'o'), 0x00F5);
    RoboCheckEqInt((int)InputDeadKeyCompose((uint32_t)RoboInputDeadKeyDiaeresis, 'u'), 0x00FC);
    RoboCheckEqInt((int)InputDeadKeyCompose((uint32_t)RoboInputDeadKeyAcute, 'y'), 0x00FD);
    RoboCheckEqInt((int)InputDeadKeyCompose((uint32_t)RoboInputDeadKeyDiaeresis, 'Y'), 0x0178);
    RoboCheckEqInt((int)InputDeadKeyCompose((uint32_t)RoboInputDeadKeyAcute, 'A'), 0x00C1);
    RoboCheckEqInt((int)InputDeadKeyCompose((uint32_t)RoboInputDeadKeyCircumflex, 'U'), 0x00DB);
    /* Zero, and each for a different reason: no such pair, no such letter, and no accent at all. */
    RoboCheckEqInt((int)InputDeadKeyCompose((uint32_t)RoboInputDeadKeyTilde, 'e'), 0);
    RoboCheckEqInt((int)InputDeadKeyCompose((uint32_t)RoboInputDeadKeyGrave, 'y'), 0);
    RoboCheckEqInt((int)InputDeadKeyCompose((uint32_t)RoboInputDeadKeyAcute, 'q'), 0);
    RoboCheckEqInt((int)InputDeadKeyCompose((uint32_t)RoboInputDeadKeyAcute, 'm'), 0);
    RoboCheckEqInt((int)InputDeadKeyCompose(99u, 'e'), 0);
    RoboCheckEqInt((int)InputDeadKeyCompose((uint32_t)RoboInputDeadKeyNone, 'e'), 0);
    RoboCheckEqInt((int)InputDeadKeyCompose((uint32_t)RoboInputDeadKeyAcute, (Uchar32)0u), 0);
}

/* ------------------------------------------------------------------ */
/* The level rule                                                      */
/* ------------------------------------------------------------------ */

static void TestLayoutLevels(void)
{
    InputModifiers none = RoboInputModifiersNone;

    RoboCheckEqInt((int)InputLayoutLevelFor(none, 0), (int)RoboInputLayoutLevelBase);
    RoboCheckEqInt((int)InputLayoutLevelFor(none, 1), (int)RoboInputLayoutLevelBase);

    RoboCheckEqInt((int)InputLayoutLevelFor(RoboKeyModShift, 0), (int)RoboInputLayoutLevelShift);
    RoboCheckEqInt((int)InputLayoutLevelFor(RoboKeyModShift, 1), (int)RoboInputLayoutLevelShift);

    /* Caps lock inverts shift for a cased key and does **nothing** to an uncased one: the half that keeps
     * `Shift+1` as `!`. */
    RoboCheckEqInt((int)InputLayoutLevelFor(RoboKeyModCapsLock, 1), (int)RoboInputLayoutLevelShift);
    RoboCheckEqInt((int)InputLayoutLevelFor(RoboKeyModCapsLock, 0), (int)RoboInputLayoutLevelBase);
    RoboCheckEqInt((int)InputLayoutLevelFor(RoboKeyModCapsLock | RoboKeyModShift, 1),
                   (int)RoboInputLayoutLevelBase);
    RoboCheckEqInt((int)InputLayoutLevelFor(RoboKeyModCapsLock | RoboKeyModShift, 0),
                   (int)RoboInputLayoutLevelShift);

    /* The two spellings of AltGr, and the one key that is *not* it: a bare left Alt is a modifier a menu
     * accelerator reads, not a third level. */
    RoboCheckEqInt((int)InputLayoutLevelFor(RoboKeyModAltRight, 0), (int)RoboInputLayoutLevelAltGr);
    RoboCheckEqInt((int)InputLayoutLevelFor(RoboKeyModCtrl | RoboKeyModAlt, 0),
                   (int)RoboInputLayoutLevelAltGr);
    RoboCheckEqInt((int)InputLayoutLevelFor(RoboKeyModAltLeft, 0), (int)RoboInputLayoutLevelBase);
    RoboCheckEqInt((int)InputLayoutLevelFor(RoboKeyModAltRight | RoboKeyModShift, 1),
                   (int)RoboInputLayoutLevelShiftAltGr);
    /* An unrecognised bit is masked off rather than believed: the level vocabulary is this build's. */
    RoboCheckEqInt((int)InputLayoutLevelFor((InputModifiers)(1u << 30), 0),
                   (int)RoboInputLayoutLevelBase);

    /* The control-chord rule, which is the *pipeline's* and lives here so a shortcut layer and a text layer
     * cannot disagree about it. */
    RoboCheck(!InputModifiersIsControlChord(none));
    RoboCheck(!InputModifiersIsControlChord(RoboKeyModShift));
    RoboCheck(!InputModifiersIsControlChord(RoboKeyModAltRight));
    RoboCheck(!InputModifiersIsControlChord(RoboKeyModCtrl | RoboKeyModAlt));
    RoboCheck(!InputModifiersIsControlChord(RoboKeyModCapsLock));
    RoboCheck(InputModifiersIsControlChord(RoboKeyModCtrl));
    RoboCheck(InputModifiersIsControlChord(RoboKeyModMeta));
    RoboCheck(InputModifiersIsControlChord(RoboKeyModCtrl | RoboKeyModShift));
    RoboCheck(InputModifiersIsControlChord(RoboKeyModCtrl | RoboKeyModAlt | RoboKeyModMeta));
}

/* ------------------------------------------------------------------ */
/* One mapping                                                         */
/* ------------------------------------------------------------------ */

static void TestLayoutMapping(void)
{
    InputDiag diag;
    InputLayoutMapping mapping;
    InputLayoutMapping other;

    InputLayoutMappingInit(&mapping);
    RoboCheck(!InputLayoutMappingIsValid(&mapping));
    RoboCheckEqInt(mapping.deadKey, (int)RoboInputDeadKeyNone);
    RoboCheckStrEq(mapping.text, "");
    RoboCheck(!InputLayoutMappingIsCased(&mapping));
    RoboCheck(!InputLayoutMappingIsCased(NULL));

    RoboCheckEqInt(InputLayoutMappingSet(&mapping, RoboPhysicalKeyA, RoboInputLayoutLevelBase,
                                         RoboLogicalKeyA, "a", (uint32_t)RoboInputDeadKeyNone,
                                         RoboInputLayoutMappingFlagCased, &diag),
                   RoboOk);
    RoboCheck(InputLayoutMappingIsValid(&mapping));
    RoboCheck(InputLayoutMappingIsCased(&mapping));
    RoboCheckStrEq(mapping.text, "a");

    /* A NULL text is a key that produces nothing, which is a legitimate entry: a layout may name a key's
     * identity without giving it a character. */
    {
        InputLayoutMapping empty;

        InputLayoutMappingInit(&empty);
        RoboCheckEqInt(InputLayoutMappingSet(&empty, RoboPhysicalKeyF1, RoboInputLayoutLevelBase,
                                             RoboLogicalKeyF1, NULL, (uint32_t)RoboInputDeadKeyNone,
                                             0u, &diag),
                       RoboOk);
        RoboCheck(InputLayoutMappingIsValid(&empty));
        RoboCheckStrEq(empty.text, "");
    }

    other = mapping;
    RoboCheck(InputLayoutMappingEqual(&mapping, &other));
    other.text[0] = 'b';
    RoboCheck(!InputLayoutMappingEqual(&mapping, &other));
    RoboCheck(InputLayoutMappingEqual(NULL, NULL));
    RoboCheck(!InputLayoutMappingEqual(&mapping, NULL));
    RoboCheck(!InputLayoutMappingEqual(NULL, &mapping));

    /* The refusals. Each is a different rule, and every one of them clears the record rather than leaving a
     * mixture of the refused call and the previous accepted one. */
    {
        InputLayoutMapping refused;

        InputLayoutMappingInit(&refused);
        RoboCheckEqInt(InputLayoutMappingSet(&refused, RoboPhysicalKeyUnknown, RoboInputLayoutLevelBase,
                                             RoboLogicalKeyA, "a", (uint32_t)RoboInputDeadKeyNone, 0u,
                                             &diag),
                       RoboInvalidArgument);
        RoboCheckStrEq(refused.text, "");
        RoboCheckEqInt(InputLayoutMappingSet(&refused, RoboPhysicalKeyA, (InputLayoutLevel)4,
                                             RoboLogicalKeyA, "a", (uint32_t)RoboInputDeadKeyNone, 0u,
                                             &diag),
                       RoboInvalidArgument);
        RoboCheckEqInt(InputLayoutMappingSet(&refused, RoboPhysicalKeyA, RoboInputLayoutLevelBase,
                                             RoboLogicalKeyA, "a", (uint32_t)RoboInputDeadKeyNone,
                                             (uint32_t)2, &diag),
                       RoboInvalidArgument);
        RoboCheckEqInt(InputLayoutMappingSet(&refused, RoboPhysicalKeyA, RoboInputLayoutLevelBase,
                                             RoboLogicalKeyA, "a", 99u, 0u, &diag),
                       RoboInvalidArgument);
        RoboCheckEqInt(InputLayoutMappingSet(&refused, RoboPhysicalKeyA, RoboInputLayoutLevelBase,
                                             RoboLogicalKeyUnknown, "a", (uint32_t)RoboInputDeadKeyNone,
                                             0u, &diag),
                       RoboInvalidArgument);
        /* A dead key **must** carry its spacing form: a mapping that armed an accent and produced no
         * character would swallow the key that followed it with nothing to show. */
        RoboCheckEqInt(InputLayoutMappingSet(&refused, RoboPhysicalKeyQuote, RoboInputLayoutLevelBase,
                                             RoboLogicalKeyQuote, NULL,
                                             (uint32_t)RoboInputDeadKeyAcute, 0u, &diag),
                       RoboInvalidArgument);
        RoboCheckEqInt(InputLayoutMappingSet(&refused, RoboPhysicalKeyA, RoboInputLayoutLevelBase,
                                             RoboLogicalKeyA,
                                             "123456789012345678901234567890123",
                                             (uint32_t)RoboInputDeadKeyNone, 0u, &diag),
                       RoboInvalidArgument);
        RoboCheckEqInt(InputLayoutMappingSet(NULL, RoboPhysicalKeyA, RoboInputLayoutLevelBase,
                                             RoboLogicalKeyA, "a", (uint32_t)RoboInputDeadKeyNone, 0u,
                                             &diag),
                       RoboInvalidArgument);
        RoboCheckStrEq(refused.text, "");
    }
}

/* ------------------------------------------------------------------ */
/* The built-ins                                                       */
/* ------------------------------------------------------------------ */

/** Look up one mapping, asserting it exists, and hand back the text. */
static const char *LayoutTextAt(const InputLayout *layout, PhysicalKey physical, InputLayoutLevel level)
{
    static char text[RoboInputTextMax] = "";
    InputLayoutMapping mapping;
    InputDiag diag;

    if (InputLayoutLookup(layout, physical, level, &mapping, &diag) != RoboOk) {
        RoboCheckMsg(0, "the layout has no mapping for a key the case expects");
        return "";
    }
    strncpy(text, mapping.text, sizeof(text) - 1u);
    text[sizeof(text) - 1u] = '\0';
    return text;
}

static void TestLayoutBuiltin(void)
{
    InputLayout us;
    InputLayout international;
    InputDiag diag;
    InputLayoutMapping mapping;

    RoboCheckEqInt(InputLayoutCreateBuiltin(&us, RoboInputLayoutUsAscii, &diag), RoboOk);
    RoboCheckEqInt(us.id, (int)RoboInputLayoutUsAscii);
    RoboCheckStrEq(us.name, "us-ascii");
    /* 48 positions -- 26 letters, 10 digits, 11 punctuation and space -- at two levels each. */
    RoboCheckEqInt((int)InputLayoutMappingCount(&us), 96);

    RoboCheckStrEq(LayoutTextAt(&us, RoboPhysicalKeyA, RoboInputLayoutLevelBase), "a");
    RoboCheckStrEq(LayoutTextAt(&us, RoboPhysicalKeyA, RoboInputLayoutLevelShift), "A");
    RoboCheckStrEq(LayoutTextAt(&us, RoboPhysicalKeyZ, RoboInputLayoutLevelBase), "z");
    RoboCheckStrEq(LayoutTextAt(&us, RoboPhysicalKeyZ, RoboInputLayoutLevelShift), "Z");
    RoboCheckStrEq(LayoutTextAt(&us, RoboPhysicalKeyDigit1, RoboInputLayoutLevelBase), "1");
    RoboCheckStrEq(LayoutTextAt(&us, RoboPhysicalKeyDigit1, RoboInputLayoutLevelShift), "!");
    RoboCheckStrEq(LayoutTextAt(&us, RoboPhysicalKeyDigit0, RoboInputLayoutLevelShift), ")");
    RoboCheckStrEq(LayoutTextAt(&us, RoboPhysicalKeySemicolon, RoboInputLayoutLevelBase), ";");
    RoboCheckStrEq(LayoutTextAt(&us, RoboPhysicalKeySemicolon, RoboInputLayoutLevelShift), ":");
    RoboCheckStrEq(LayoutTextAt(&us, RoboPhysicalKeyQuote, RoboInputLayoutLevelBase), "'");
    RoboCheckStrEq(LayoutTextAt(&us, RoboPhysicalKeyQuote, RoboInputLayoutLevelShift), "\"");
    RoboCheckStrEq(LayoutTextAt(&us, RoboPhysicalKeyBackquote, RoboInputLayoutLevelBase), "`");
    RoboCheckStrEq(LayoutTextAt(&us, RoboPhysicalKeyBackquote, RoboInputLayoutLevelShift), "~");
    RoboCheckStrEq(LayoutTextAt(&us, RoboPhysicalKeySpace, RoboInputLayoutLevelBase), " ");
    RoboCheckStrEq(LayoutTextAt(&us, RoboPhysicalKeySlash, RoboInputLayoutLevelShift), "?");

    /* A key with no character at all: an arrow, a function key, a modifier. Not found rather than mapped to
     * nothing, because "the layout has no opinion" and "this key deliberately types nothing" are different
     * facts and a caller that could not tell them apart would map every key twice. */
    RoboCheckEqInt(InputLayoutLookup(&us, RoboPhysicalKeyF1, RoboInputLayoutLevelBase, &mapping, &diag),
                   RoboNotFound);
    RoboCheckEqInt(InputLayoutLookup(&us, RoboPhysicalKeyArrowLeft, RoboInputLayoutLevelBase, &mapping,
                                     &diag),
                   RoboNotFound);
    RoboCheckEqInt(InputLayoutLookup(&us, RoboPhysicalKeyCapsLock, RoboInputLayoutLevelBase, &mapping,
                                     &diag),
                   RoboNotFound);
    /* The US table has no third level, which is what makes AltGr a key that produces nothing rather than a
     * key that produces the base character twice. */
    RoboCheckEqInt(InputLayoutLookup(&us, RoboPhysicalKeyE, RoboInputLayoutLevelAltGr, &mapping, &diag),
                   RoboNotFound);

    /* The identity flags: a letter carries `CASED`, punctuation does not, and that is what caps lock reads. */
    RoboCheckEqInt(InputLayoutLookup(&us, RoboPhysicalKeyA, RoboInputLayoutLevelBase, &mapping, &diag),
                   RoboOk);
    RoboCheck(InputLayoutMappingIsCased(&mapping));
    RoboCheckEqInt(mapping.logical, (int)RoboLogicalKeyA);
    RoboCheckEqInt(InputLayoutLookup(&us, RoboPhysicalKeyDigit1, RoboInputLayoutLevelBase, &mapping,
                                     &diag),
                   RoboOk);
    RoboCheck(!InputLayoutMappingIsCased(&mapping));
    RoboCheckEqInt(mapping.logical, (int)RoboLogicalKeyDigit1);

    /* --- the international table ------------------------------------- */
    RoboCheckEqInt(InputLayoutCreateBuiltin(&international, RoboInputLayoutUsInternational, &diag),
                   RoboOk);
    RoboCheckEqInt(international.id, (int)RoboInputLayoutUsInternational);
    RoboCheckStrEq(international.name, "us-international");
    RoboCheckEqInt((int)InputLayoutMappingCount(&international), 96);

    /* The five dead accents, each on the key US-International puts it on, and the *spacing form* as the
     * mapping's text: `´` for the acute, `¨` for the diaeresis, and the plain characters for grave,
     * circumflex and tilde. */
    RoboCheckEqInt(InputLayoutLookup(&international, RoboPhysicalKeyQuote, RoboInputLayoutLevelBase,
                                     &mapping, &diag),
                   RoboOk);
    RoboCheckEqInt(mapping.deadKey, (int)RoboInputDeadKeyAcute);
    RoboCheckStrEq(mapping.text, "\xC2\xB4");
    /* It is a dead accent on a **punctuation** key, so it is not cased: caps lock must not invert its level,
     * because an accent is not a letter. */
    RoboCheck(!InputLayoutMappingIsCased(&mapping));
    RoboCheckEqInt(InputLayoutLookup(&international, RoboPhysicalKeyQuote, RoboInputLayoutLevelShift,
                                     &mapping, &diag),
                   RoboOk);
    RoboCheckEqInt(mapping.deadKey, (int)RoboInputDeadKeyDiaeresis);
    RoboCheckStrEq(mapping.text, "\xC2\xA8");
    RoboCheckEqInt(InputLayoutLookup(&international, RoboPhysicalKeyBackquote, RoboInputLayoutLevelBase,
                                     &mapping, &diag),
                   RoboOk);
    RoboCheckEqInt(mapping.deadKey, (int)RoboInputDeadKeyGrave);
    RoboCheckStrEq(mapping.text, "`");
    RoboCheckEqInt(InputLayoutLookup(&international, RoboPhysicalKeyBackquote, RoboInputLayoutLevelShift,
                                     &mapping, &diag),
                   RoboOk);
    RoboCheckEqInt(mapping.deadKey, (int)RoboInputDeadKeyTilde);
    RoboCheckStrEq(mapping.text, "~");
    /* `^` lives on shift+6, so the *digit* still types a digit and only the shifted level arms an accent --
     * the case that separates "this key is a dead key" from "this key is dead at every level". */
    RoboCheckEqInt(InputLayoutLookup(&international, RoboPhysicalKeyDigit6, RoboInputLayoutLevelBase,
                                     &mapping, &diag),
                   RoboOk);
    RoboCheckEqInt(mapping.deadKey, (int)RoboInputDeadKeyNone);
    RoboCheckStrEq(mapping.text, "6");
    RoboCheckEqInt(InputLayoutLookup(&international, RoboPhysicalKeyDigit6, RoboInputLayoutLevelShift,
                                     &mapping, &diag),
                   RoboOk);
    RoboCheckEqInt(mapping.deadKey, (int)RoboInputDeadKeyCircumflex);
    RoboCheckStrEq(mapping.text, "^");
    /* A key the international table leaves alone is the same character it is on US ASCII. */
    RoboCheckStrEq(LayoutTextAt(&international, RoboPhysicalKeyE, RoboInputLayoutLevelBase), "e");
    RoboCheckStrEq(LayoutTextAt(&international, RoboPhysicalKeySlash, RoboInputLayoutLevelBase), "/");

    /* The table is in `(physical, level)` order: base before shift for one key, and one key's entries before
     * the next's. That order is what the dump and two runs' byte-for-byte equality rest on. */
    RoboCheckEqInt(InputLayoutMappingAt(&international, 0u, &mapping, &diag), RoboOk);
    RoboCheckEqInt(mapping.physical, (int)RoboPhysicalKeyA);
    RoboCheckEqInt(mapping.level, (int)RoboInputLayoutLevelBase);
    RoboCheckEqInt(InputLayoutMappingAt(&international, 1u, &mapping, &diag), RoboOk);
    RoboCheckEqInt(mapping.physical, (int)RoboPhysicalKeyA);
    RoboCheckEqInt(mapping.level, (int)RoboInputLayoutLevelShift);
    RoboCheckEqInt(InputLayoutMappingAt(&international, 2u, &mapping, &diag), RoboOk);
    RoboCheckEqInt(mapping.physical, (int)RoboPhysicalKeyB);

    /* The refusals: no layout, no such built-in, no level, past the end, and a NULL out. */
    RoboCheckEqInt(InputLayoutCreateBuiltin(NULL, RoboInputLayoutUsAscii, &diag), RoboInvalidArgument);
    RoboCheckEqInt(InputLayoutCreateBuiltin(&us, RoboInputLayoutNone, &diag), RoboInvalidArgument);
    RoboCheckEqInt(InputLayoutCreateBuiltin(&us, (InputLayoutId)77, &diag), RoboInvalidArgument);
    RoboCheckEqInt(InputLayoutLookup(NULL, RoboPhysicalKeyA, RoboInputLayoutLevelBase, &mapping, &diag),
                   RoboInvalidArgument);
    RoboCheckEqInt(InputLayoutLookup(&us, RoboPhysicalKeyA, RoboInputLayoutLevelBase, NULL, &diag),
                   RoboInvalidArgument);
    RoboCheckEqInt(InputLayoutLookup(&us, RoboPhysicalKeyA, (InputLayoutLevel)7, &mapping, &diag),
                   RoboInvalidArgument);
    RoboCheckEqInt(InputLayoutMappingAt(&us, 96u, &mapping, &diag), RoboNotFound);
    RoboCheckEqInt(InputLayoutMappingAt(NULL, 0u, &mapping, &diag), RoboInvalidArgument);
    RoboCheckEqInt((int)InputLayoutMappingCount(NULL), 0);
}

/* ------------------------------------------------------------------ */
/* Caps lock through the table                                         */
/* ------------------------------------------------------------------ */

static void TestLayoutCapsLock(void)
{
    InputLayout us;
    InputLayoutMapping mapping;
    InputDiag diag;
    InputLayoutLevel level;

    RoboCheckEqInt(InputLayoutCreateBuiltin(&us, RoboInputLayoutUsAscii, &diag), RoboOk);

    /* `Shift+1` with caps lock on is still `!`: the digit is uncased, so caps lock does not invert shift,
     * and the level the pipeline asks for is shift. */
    RoboCheckEqInt(InputLayoutLookup(&us, RoboPhysicalKeyDigit1, RoboInputLayoutLevelBase, &mapping,
                                     &diag),
                   RoboOk);
    level = InputLayoutLevelFor(RoboKeyModCapsLock | RoboKeyModShift,
                                InputLayoutMappingIsCased(&mapping));
    RoboCheckEqInt((int)level, (int)RoboInputLayoutLevelShift);

    /* `a` with caps lock on is `A`: a letter *is* cased, so the invert applies. */
    RoboCheckEqInt(InputLayoutLookup(&us, RoboPhysicalKeyA, RoboInputLayoutLevelBase, &mapping, &diag),
                   RoboOk);
    level = InputLayoutLevelFor(RoboKeyModCapsLock, InputLayoutMappingIsCased(&mapping));
    RoboCheckEqInt((int)level, (int)RoboInputLayoutLevelShift);
    RoboCheckStrEq(LayoutTextAt(&us, RoboPhysicalKeyA, level), "A");

    /* ... and `a` with caps lock **and** shift is `a`, which is the same rule read the other way. */
    RoboCheckEqInt((int)InputLayoutLevelFor(RoboKeyModCapsLock | RoboKeyModShift, 1),
                   (int)RoboInputLayoutLevelBase);
}

/* ------------------------------------------------------------------ */
/* The table                                                           */
/* ------------------------------------------------------------------ */

static void TestLayoutTable(void)
{
    InputLayout layout;
    InputLayoutMapping mapping;
    InputDiag diag;
    uint32_t index;

    InputLayoutInit(&layout);
    RoboCheckEqInt((int)InputLayoutMappingCount(&layout), 0);
    RoboCheckEqInt(layout.id, (int)RoboInputLayoutNone);
    RoboCheckStrEq(layout.name, "");
    RoboCheckEqInt(InputLayoutSetName(&layout, "test", &diag), RoboOk);
    RoboCheckStrEq(layout.name, "test");
    RoboCheckEqInt(InputLayoutSetName(&layout, "this-name-is-far-too-long-for-the-record", &diag),
                   RoboInvalidArgument);
    RoboCheckStrEq(layout.name, "test");
    RoboCheckEqInt(InputLayoutSetName(&layout, NULL, &diag), RoboOk);
    RoboCheckStrEq(layout.name, "");
    RoboCheckEqInt(InputLayoutSetName(NULL, "test", &diag), RoboInvalidArgument);

    /* Add out of order and read back in order: the setters keep the table sorted. */
    InputLayoutMappingInit(&mapping);
    RoboCheckEqInt(InputLayoutMappingSet(&mapping, RoboPhysicalKeyZ, RoboInputLayoutLevelBase,
                                         RoboLogicalKeyZ, "z", (uint32_t)RoboInputDeadKeyNone,
                                         RoboInputLayoutMappingFlagCased, &diag),
                   RoboOk);
    RoboCheckEqInt(InputLayoutSetMapping(&layout, &mapping, &diag), RoboOk);
    InputLayoutMappingInit(&mapping);
    RoboCheckEqInt(InputLayoutMappingSet(&mapping, RoboPhysicalKeyA, RoboInputLayoutLevelShift,
                                         RoboLogicalKeyA, "A", (uint32_t)RoboInputDeadKeyNone,
                                         RoboInputLayoutMappingFlagCased, &diag),
                   RoboOk);
    RoboCheckEqInt(InputLayoutSetMapping(&layout, &mapping, &diag), RoboOk);
    InputLayoutMappingInit(&mapping);
    RoboCheckEqInt(InputLayoutMappingSet(&mapping, RoboPhysicalKeyA, RoboInputLayoutLevelBase,
                                         RoboLogicalKeyA, "a", (uint32_t)RoboInputDeadKeyNone,
                                         RoboInputLayoutMappingFlagCased, &diag),
                   RoboOk);
    RoboCheckEqInt(InputLayoutSetMapping(&layout, &mapping, &diag), RoboOk);
    RoboCheckEqInt((int)InputLayoutMappingCount(&layout), 3);

    RoboCheckEqInt(InputLayoutMappingAt(&layout, 0u, &mapping, &diag), RoboOk);
    RoboCheckEqInt(mapping.physical, (int)RoboPhysicalKeyA);
    RoboCheckEqInt(mapping.level, (int)RoboInputLayoutLevelBase);
    RoboCheckEqInt(InputLayoutMappingAt(&layout, 1u, &mapping, &diag), RoboOk);
    RoboCheckEqInt(mapping.level, (int)RoboInputLayoutLevelShift);
    RoboCheckEqInt(InputLayoutMappingAt(&layout, 2u, &mapping, &diag), RoboOk);
    RoboCheckEqInt(mapping.physical, (int)RoboPhysicalKeyZ);

    /* Replacing: the same key at the same level overwrites rather than appending, which is what makes a
     * file that lists a key twice mean the second entry. */
    InputLayoutMappingInit(&mapping);
    RoboCheckEqInt(InputLayoutMappingSet(&mapping, RoboPhysicalKeyA, RoboInputLayoutLevelBase,
                                         RoboLogicalKeyA, "x", (uint32_t)RoboInputDeadKeyNone, 0u,
                                         &diag),
                   RoboOk);
    RoboCheckEqInt(InputLayoutSetMapping(&layout, &mapping, &diag), RoboOk);
    RoboCheckEqInt((int)InputLayoutMappingCount(&layout), 3);
    RoboCheckStrEq(LayoutTextAt(&layout, RoboPhysicalKeyA, RoboInputLayoutLevelBase), "x");

    /* The bound: fill it, then refuse the next *new* key rather than dropping it. */
    InputLayoutInit(&layout);
    for (index = 0u; index < RoboInputLayoutMaxMappings; index++) {
        PhysicalKey physical = (PhysicalKey)(1u + (index / RoboInputLayoutLevelCount));
        InputLayoutLevel level = (InputLayoutLevel)(index % RoboInputLayoutLevelCount);

        InputLayoutMappingInit(&mapping);
        if (InputLayoutMappingSet(&mapping, physical, level, RoboLogicalKeyA, "a",
                                  (uint32_t)RoboInputDeadKeyNone, 0u, &diag) != RoboOk) {
            RoboCheckMsg(0, "the case's own mapping was refused");
            break;
        }
        if (InputLayoutSetMapping(&layout, &mapping, &diag) != RoboOk) {
            RoboCheckMsg(0, "the table refused a mapping before it was full");
            break;
        }
    }
    RoboCheckEqInt((int)InputLayoutMappingCount(&layout), (int)RoboInputLayoutMaxMappings);
    InputLayoutMappingInit(&mapping);
    RoboCheckEqInt(InputLayoutMappingSet(&mapping, RoboPhysicalKeySlash, RoboInputLayoutLevelBase,
                                         RoboLogicalKeySlash, "/", (uint32_t)RoboInputDeadKeyNone, 0u,
                                         &diag),
                   RoboOk);
    RoboCheckEqInt(InputLayoutSetMapping(&layout, &mapping, &diag), RoboNotSupported);
    RoboCheckEqInt((int)InputLayoutMappingCount(&layout), (int)RoboInputLayoutMaxMappings);
    /* ... while *replacing* still works on a full table, because it does not grow it. */
    RoboCheckEqInt(InputLayoutSetMapping(&layout, &layout.mappings[0], &diag), RoboOk);

    RoboCheckEqInt(InputLayoutSetMapping(NULL, &mapping, &diag), RoboInvalidArgument);
    RoboCheckEqInt(InputLayoutSetMapping(&layout, NULL, &diag), RoboInvalidArgument);
    InputLayoutMappingInit(&mapping);
    RoboCheckEqInt(InputLayoutSetMapping(&layout, &mapping, &diag), RoboInvalidArgument);
    InputLayoutInit(NULL);
    InputLayoutSort(NULL);
}

/* ------------------------------------------------------------------ */
/* The dump                                                            */
/* ------------------------------------------------------------------ */

static void TestLayoutDump(void)
{
    /* Static rather than heap: a layout dump is the biggest text this module produces, and a suite that
     * allocated it would be testing the allocator's leak counter as much as the layout's own text. */
    static char text[RoboInputLayoutDumpMax];
    static char second[RoboInputLayoutDumpMax];
    InputLayout us;
    InputDiag diag;
    size_t length = 0u;
    size_t secondLength = 0u;

    RoboCheckEqInt(InputLayoutCreateBuiltin(&us, RoboInputLayoutUsAscii, &diag), RoboOk);
    RoboCheckEqInt(InputLayoutWrite(&us, text, RoboInputLayoutDumpMax, &length, &diag), RoboOk);
    RoboCheck(length > 0u);
    RoboCheck(strncmp(text, "layout id=1 name=us-ascii mappings=96\n", 38u) == 0);
    /* One line per mapping, and the first key's two levels in order. */
    RoboCheck(strstr(text, "map A base logical=A text=\"a\" flags=cased\n") != NULL);
    RoboCheck(strstr(text, "map A shift logical=A text=\"A\" flags=cased\n") != NULL);
    RoboCheck(strstr(text, "map Digit1 shift logical=Digit1 text=\"!\"\n") != NULL);
    RoboCheck(strstr(text, "map Space base logical=Space text=\" \"\n") != NULL);

    /* Byte-for-byte determinism across two calls, which is what a replay of a recording needs. */
    RoboCheckEqInt(InputLayoutWrite(&us, second, RoboInputLayoutDumpMax, &secondLength, &diag), RoboOk);
    RoboCheckEqInt((int)secondLength, (int)length);
    RoboCheck(strcmp(text, second) == 0);

    /* A dead key's line names the accent, so a reader does not have to decode its spacing form. */
    {
        InputLayout international;

        RoboCheckEqInt(InputLayoutCreateBuiltin(&international, RoboInputLayoutUsInternational, &diag),
                       RoboOk);
        RoboCheckEqInt(InputLayoutWrite(&international, text, RoboInputLayoutDumpMax, &length, &diag),
                       RoboOk);
        RoboCheck(strncmp(text, "layout id=2 name=us-international mappings=96\n", 45u) == 0);
        RoboCheck(strstr(text, "map Quote base logical=Quote text=\"\xC2\xB4\" dead=acute\n") != NULL);
        RoboCheck(strstr(text, "map Digit6 shift logical=Digit6 text=\"^\" dead=circumflex\n") != NULL);
    }

    /* A dump of nothing, and a buffer too small for the text: `?` rather than a prefix that reads as a whole
     * layout, with the needed length reported. */
    RoboCheckEqInt(InputLayoutWrite(NULL, text, RoboInputLayoutDumpMax, &length, &diag), RoboOk);
    RoboCheckStrEq(text, "layout <none>\n");
    {
        char small[16];
        size_t needed = 0u;

        RoboCheckEqInt(InputLayoutWrite(&us, small, sizeof(small), &needed, &diag), RoboNotSupported);
        RoboCheck(needed > sizeof(small));
        RoboCheckStrEq(small, "?");
        RoboCheckEqInt(diag.domain, RoboInputDomainDump);
    }
    RoboCheckEqInt(InputLayoutWrite(&us, NULL, RoboInputLayoutDumpMax, &length, &diag),
                   RoboInvalidArgument);
    RoboCheckEqInt(InputLayoutWrite(&us, text, 0u, &length, &diag), RoboInvalidArgument);
    RoboCheckEqInt(InputLayoutWrite(&us, text, RoboInputLayoutDumpMax, NULL, &diag), RoboInvalidArgument);
}

void TestRegisterInputLayout(void)
{
    TestRegister("input.layout.vocabulary", TestLayoutVocabulary);
    TestRegister("input.layout.levels", TestLayoutLevels);
    TestRegister("input.layout.mapping", TestLayoutMapping);
    TestRegister("input.layout.builtin", TestLayoutBuiltin);
    TestRegister("input.layout.capslock", TestLayoutCapsLock);
    TestRegister("input.layout.table", TestLayoutTable);
    TestRegister("input.layout.dump", TestLayoutDump);
}
