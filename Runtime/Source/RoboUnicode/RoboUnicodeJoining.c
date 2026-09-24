/*
 * `Joining_Type` (UAX #44, Unicode 16.0.0) — see `RoboUnicodeJoining.h` for why it lives here.
 *
 * The table is **sorted ranges** so the lookup is a binary search, and it states only what the UCD states
 * about its terms explicitly: a scalar the table does not name answers by the property's own default rule
 * (`Mn`/`Me`/`Cf` → `Transparent`, everything else → `NonJoining`), which is why the marks of a block need
 * no row of their own.
 *
 * The rows are the Arabic script family: `Arabic` (U+0600..U+06FF), `Arabic Supplement` (U+0750..U+077F) and
 * `Arabic Extended-A` (U+08A0..U+08C8), plus the two controls every joining script obeys — U+200C `ZWNJ`,
 * which is **non-joining** and therefore the character whose whole purpose is to break a joining sequence,
 * and U+200D `ZWJ`, which is join-causing — and U+0640 `TATWEEL`, which is join-causing too. Both controls
 * are `Cf` and are therefore listed: without a row the default rule would call them transparent, and a
 * transparent `ZWNJ` is a joiner, which is the opposite of what it is for.
 *
 * Within the blocks, the rows are the ones a reader can check against the data file:
 *
 *   `U`   the non-letters (the number signs U+0600..U+0605, U+06DD, U+06D4, the digits, …) and
 *         U+0621 `HAMZA`, which is a letter that does not join;
 *   `R`   the right-joining letters — the ALEF and WAW families, `DAL`, `THAL`, `REH`, `ZAIN` and every
 *         variant of them, which is what gives Arabic its one-sided connections;
 *   `D`   every other letter: the dual-joining ones, which connect on both sides;
 *   `C`   `TATWEEL` and `ZWJ`;
 *   `T`   the diacritics the data file names explicitly (`064B..065F`, `0670`, `06D6..06DC`, `06DF..06ED`,
 *         `08CA..08E1`, `08E3..08FF`).
 *
 * `U+0674 HIGH HAMZA` is a `U` row of its own: it is a letter that does **not** join, and a range that swept
 * it into its neighbours' `R` would make it take an initial form.
 */
#include "RoboUnicode/RoboUnicodeJoining.h"

#include "RoboUnicode/RoboUnicodeProps.h"

/** One row: a half-open-free range of scalars (inclusive ends) and the type they all answer. */
typedef struct JoiningRow {
    uint32_t first;
    uint32_t last;
    uint8_t type;
} JoiningRow;

static const JoiningRow RoboJoiningRows[] = {
    /* --- Arabic U+0600..U+06FF --- */
    { 0x0600u, 0x0605u, RoboUJtNonJoining },
    { 0x0608u, 0x0608u, RoboUJtNonJoining },
    { 0x060Bu, 0x060Bu, RoboUJtNonJoining },
    { 0x0620u, 0x0620u, RoboUJtDualJoining },
    { 0x0621u, 0x0621u, RoboUJtNonJoining },
    { 0x0622u, 0x0625u, RoboUJtRightJoining },
    { 0x0626u, 0x0626u, RoboUJtDualJoining },
    { 0x0627u, 0x0627u, RoboUJtRightJoining },
    { 0x0628u, 0x0628u, RoboUJtDualJoining },
    { 0x0629u, 0x0629u, RoboUJtRightJoining },
    { 0x062Au, 0x062Eu, RoboUJtDualJoining },
    { 0x062Fu, 0x0632u, RoboUJtRightJoining },
    { 0x0633u, 0x063Fu, RoboUJtDualJoining },
    { 0x0640u, 0x0640u, RoboUJtJoinCausing },
    { 0x0641u, 0x0647u, RoboUJtDualJoining },
    { 0x0648u, 0x0648u, RoboUJtRightJoining },
    { 0x0649u, 0x064Au, RoboUJtDualJoining },
    { 0x064Bu, 0x065Fu, RoboUJtTransparent },
    { 0x066Eu, 0x066Fu, RoboUJtDualJoining },
    { 0x0670u, 0x0670u, RoboUJtTransparent },
    { 0x0671u, 0x0673u, RoboUJtRightJoining },
    { 0x0674u, 0x0674u, RoboUJtNonJoining },
    { 0x0675u, 0x0677u, RoboUJtRightJoining },
    { 0x0678u, 0x0687u, RoboUJtDualJoining },
    { 0x0688u, 0x0699u, RoboUJtRightJoining },
    { 0x069Au, 0x06BFu, RoboUJtDualJoining },
    { 0x06C0u, 0x06C0u, RoboUJtRightJoining },
    { 0x06C1u, 0x06C2u, RoboUJtDualJoining },
    { 0x06C3u, 0x06C3u, RoboUJtRightJoining },
    { 0x06C4u, 0x06CBu, RoboUJtRightJoining },
    { 0x06CCu, 0x06CCu, RoboUJtDualJoining },
    { 0x06CDu, 0x06CDu, RoboUJtRightJoining },
    { 0x06CEu, 0x06CEu, RoboUJtDualJoining },
    { 0x06CFu, 0x06CFu, RoboUJtRightJoining },
    { 0x06D0u, 0x06D1u, RoboUJtDualJoining },
    { 0x06D2u, 0x06D3u, RoboUJtRightJoining },
    { 0x06D4u, 0x06D4u, RoboUJtNonJoining },
    { 0x06D5u, 0x06D5u, RoboUJtRightJoining },
    { 0x06D6u, 0x06DCu, RoboUJtTransparent },
    { 0x06DDu, 0x06DEu, RoboUJtNonJoining },
    { 0x06DFu, 0x06E4u, RoboUJtTransparent },
    { 0x06E5u, 0x06E6u, RoboUJtNonJoining },
    { 0x06E7u, 0x06E8u, RoboUJtTransparent },
    { 0x06E9u, 0x06E9u, RoboUJtNonJoining },
    { 0x06EAu, 0x06EDu, RoboUJtTransparent },
    { 0x06EEu, 0x06EFu, RoboUJtRightJoining },
    { 0x06FAu, 0x06FCu, RoboUJtDualJoining },
    { 0x06FDu, 0x06FEu, RoboUJtNonJoining },
    { 0x06FFu, 0x06FFu, RoboUJtDualJoining },

    /* --- Arabic Supplement U+0750..U+077F: letters, all dual-joining --- */
    { 0x0750u, 0x077Fu, RoboUJtDualJoining },

    /* --- Arabic Extended-A U+08A0..U+08C8: letters; the marks answer by category --- */
    { 0x08A0u, 0x08C8u, RoboUJtDualJoining },

    /* --- The controls, which the default rule would otherwise classify by category --- */
    { 0x200Cu, 0x200Cu, RoboUJtNonJoining },
    { 0x200Du, 0x200Du, RoboUJtJoinCausing }
};

#define RoboJoiningRowCount (sizeof(RoboJoiningRows) / sizeof(RoboJoiningRows[0]))

UnicodeJoiningType UnicodeJoiningTypeOf(Uchar32 cp)
{
    size_t low = 0u;
    size_t high = RoboJoiningRowCount;

    if (cp < 0) {
        return RoboUJtNonJoining;
    }
    /* Binary search over sorted, non-overlapping rows. */
    while (low < high) {
        size_t mid = low + (high - low) / 2u;
        const JoiningRow *row = &RoboJoiningRows[mid];

        if ((uint32_t)cp < row->first) {
            high = mid;
        } else if ((uint32_t)cp > row->last) {
            low = mid + 1u;
        } else {
            return (UnicodeJoiningType)row->type;
        }
    }

    /*
     * Not named: the property's own default. `Mn`, `Me` and `Cf` are transparent, everything else is
     * non-joining — which is the rule the data file's "unlisted means U" convention encodes, extended with
     * the transparent default UAX #44 states for the three categories.
     */
    switch (UnicodeCategory(cp)) {
    case RoboUGcMn:
    case RoboUGcMe:
    case RoboUGcCf:
        return RoboUJtTransparent;
    default:
        return RoboUJtNonJoining;
    }
}

const char *UnicodeJoiningTypeShortName(UnicodeJoiningType type)
{
    switch (type) {
    case RoboUJtJoinCausing:
        return "C";
    case RoboUJtRightJoining:
        return "R";
    case RoboUJtLeftJoining:
        return "L";
    case RoboUJtDualJoining:
        return "D";
    case RoboUJtTransparent:
        return "T";
    case RoboUJtNonJoining:
    default:
        return "U";
    }
}

const char *UnicodeJoiningTypeName(UnicodeJoiningType type)
{
    switch (type) {
    case RoboUJtJoinCausing:
        return "JoinCausing";
    case RoboUJtRightJoining:
        return "RightJoining";
    case RoboUJtLeftJoining:
        return "LeftJoining";
    case RoboUJtDualJoining:
        return "DualJoining";
    case RoboUJtTransparent:
        return "Transparent";
    case RoboUJtNonJoining:
    default:
        return "NonJoining";
    }
}

int UnicodeJoiningTypeJoins(UnicodeJoiningType type)
{
    return type == RoboUJtRightJoining || type == RoboUJtLeftJoining ||
           type == RoboUJtDualJoining || type == RoboUJtJoinCausing;
}

int UnicodeJoiningTypeJoinsRight(UnicodeJoiningType type)
{
    /* `C` joins both ways, and `R` is the asymmetric one: it accepts a connection from the left. */
    return type == RoboUJtRightJoining || type == RoboUJtDualJoining || type == RoboUJtJoinCausing;
}

int UnicodeJoiningTypeJoinsLeft(UnicodeJoiningType type)
{
    return type == RoboUJtLeftJoining || type == RoboUJtDualJoining || type == RoboUJtJoinCausing;
}

const char *RoboJoiningVersionString(void)
{
    return "16.0.0";
}
