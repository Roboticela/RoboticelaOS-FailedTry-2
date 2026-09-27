/*
 * RoboCSS display vocabulary (Phase 9.12) — see RoboCSSDisplay.h.
 *
 * One table, and everything else in this file reads it:
 *
 *   row        one value, its keyword, its outer display and its inner display
 *
 * Eight rows today: the four 9.12 shipped, the flex pair (19.3) and the grid pair
 * (19.4). Each pair is exactly the extension the table was shaped for -- a
 * block-level value and an atomic inline one sharing one inner -- and no function
 * below changed to add either. A later `contents` is a row with an outer of NONE
 * that is *not* the value `none`. Adding a row requires touching no function below,
 * which is the entire justification for the table: every question in this module
 * used to be a place a spelling could be compared wrongly, and there is now exactly
 * one place a spelling appears.
 *
 * The keywords are lowercase and hyphenated because that is how CSS writes them
 * and how `CssDisplayWrite` must print them; matching is ASCII
 * case-insensitive against these spellings and against nothing else, so
 * `inlineblock` is not a display value and neither is a prefix of one.
 */

#include "RoboCSS/RoboCSSDisplay.h"

#include "RoboCSSInternal.h"

#include <string.h>

/*
 * The vocabulary.
 *
 * `inner` for `none` is INNER_NONE rather than FLOW, and that is a fact about
 * `none` rather than a placeholder: a value that generates no box has no contents
 * to lay out. `block` and `inline` share FLOW, because sharing its parent's
 * formatting context is what makes them ordinary; `inline-block` takes a
 * formatting context of its own, which is the whole of what makes it atomic.
 */
typedef struct displayRow {
    CssDisplay value;
    const char *keyword;
    CssDisplayOuter outer;
    CssDisplayInner inner;
} DisplayRow;

static const DisplayRow displayVocabulary[] = {
    { RoboCssDisplayNone,         "none",         RoboCssDisplayOuterNone,
      RoboCssDisplayInnerNone },
    { RoboCssDisplayBlock,        "block",        RoboCssDisplayOuterBlock,
      RoboCssDisplayInnerFlow },
    { RoboCssDisplayInline,       "inline",       RoboCssDisplayOuterInline,
      RoboCssDisplayInnerFlow },
    { RoboCssDisplayInlineBlock, "inline-block", RoboCssDisplayOuterInline,
      RoboCssDisplayInnerFlowRoot },
    { RoboCssDisplayFlex,        "flex",         RoboCssDisplayOuterBlock,
      RoboCssDisplayInnerFlex },
    { RoboCssDisplayInlineFlex,  "inline-flex",  RoboCssDisplayOuterInline,
      RoboCssDisplayInnerFlex },
    /*
     * Phase 19.4 adds the grid pair, and it is the same two rows again rather than
     * the same *row* twice: `display: grid` is block-level with a grid inner and
     * `display: inline-grid` is its atomic inline sibling, exactly as the flex pair
     * is. The four functions below answer for both without naming either, which is
     * the whole claim the table was built to make.
     */
    { RoboCssDisplayGrid,        "grid",         RoboCssDisplayOuterBlock,
      RoboCssDisplayInnerGrid },
    { RoboCssDisplayInlineGrid,  "inline-grid",  RoboCssDisplayOuterInline,
      RoboCssDisplayInnerGrid }
};

#define DISPLAY_VOCABULARY_COUNT \
    (sizeof(displayVocabulary) / sizeof(displayVocabulary[0]))

/**
 * The row for @p display, or NULL when it is not one of them.
 *
 * The one lookup every accessor below goes through, so a table search exists in
 * exactly one place and a row added to the table is reachable by all of them.
 */
static const DisplayRow *DisplayRowOf(CssDisplay display)
{
    size_t index;

    for (index = 0u; index < DISPLAY_VOCABULARY_COUNT; index++) {
        if (displayVocabulary[index].value == display) {
            return &displayVocabulary[index];
        }
    }
    return NULL;
}

int CssDisplayIsKnown(CssDisplay display)
{
    return DisplayRowOf(display) != NULL;
}

const char *CssDisplayName(CssDisplay display)
{
    const DisplayRow *row = DisplayRowOf(display);

    return row ? row->keyword : "";
}

size_t CssDisplayCount(void)
{
    return DISPLAY_VOCABULARY_COUNT;
}

CssDisplay CssDisplayFromSlice(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return RoboCssDisplayNone;
    }
    for (index = 0u; index < DISPLAY_VOCABULARY_COUNT; index++) {
        if (CssBytesEqualAsciiCi(keyword.bytes, keyword.length,
                                         displayVocabulary[index].keyword)) {
            return displayVocabulary[index].value;
        }
    }
    /* Not one of them. NONE is the answer a caller reads *after* asking
     * CssDisplaySliceIsKnown — see the header: `none` is a value, so a
     * result alone cannot say whether the identifier was recognised. */
    return RoboCssDisplayNone;
}

CssDisplay CssDisplayFromText(const char *keyword)
{
    if (!keyword) {
        return RoboCssDisplayNone;
    }
    return CssDisplayFromSlice(
        CssSliceFromBytes((const uint8_t *)keyword, strlen(keyword)));
}

int CssDisplaySliceIsKnown(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return 0;
    }
    for (index = 0u; index < DISPLAY_VOCABULARY_COUNT; index++) {
        if (CssBytesEqualAsciiCi(keyword.bytes, keyword.length,
                                         displayVocabulary[index].keyword)) {
            return 1;
        }
    }
    return 0;
}

Result CssDisplayWrite(CssDisplay display,
                                     char *buffer,
                                     size_t capacity,
                                     size_t *outLength,
                                     CssDiag *diag)
{
    const DisplayRow *row;
    const char *keyword;
    size_t length;

    if (!buffer || !outLength || capacity == 0u) {
        return RoboCssDiag(diag, RoboCssDomainDisplay, RoboInvalidArgument,
                             "buffer and out_length are required");
    }
    buffer[0] = '\0';
    *outLength = 0u;
    row = DisplayRowOf(display);
    if (!row) {
        return RoboCssDiag(diag, RoboCssDomainDisplay, RoboInvalidArgument,
                             "not a display value");
    }
    keyword = row->keyword;
    length = strlen(keyword);
    if (length >= capacity) {
        /* The length it would have needed, so a caller can size a buffer from the
         * refusal rather than from a constant it has to keep in step. */
        *outLength = length;
        return RoboCssDiag(diag, RoboCssDomainDisplay, RoboNotSupported,
                             "the display keyword does not fit");
    }
    memcpy(buffer, keyword, length);
    buffer[length] = '\0';
    *outLength = length;
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Classification                                                      */
/* ------------------------------------------------------------------ */

CssDisplayOuter MakeCssDisplayOuter(CssDisplay display)
{
    const DisplayRow *row = DisplayRowOf(display);

    return row ? row->outer : RoboCssDisplayOuterNone;
}

CssDisplayInner MakeCssDisplayInner(CssDisplay display)
{
    const DisplayRow *row = DisplayRowOf(display);

    return row ? row->inner : RoboCssDisplayInnerNone;
}

int CssDisplayGeneratesBox(CssDisplay display)
{
    return MakeCssDisplayOuter(display) != RoboCssDisplayOuterNone;
}

int CssDisplayIsNone(CssDisplay display)
{
    return display == RoboCssDisplayNone;
}

int CssDisplayIsBlockLevel(CssDisplay display)
{
    return MakeCssDisplayOuter(display) == RoboCssDisplayOuterBlock;
}

int CssDisplayIsInlineLevel(CssDisplay display)
{
    return MakeCssDisplayOuter(display) == RoboCssDisplayOuterInline;
}

int CssDisplayIsInlineBlock(CssDisplay display)
{
    /* Both halves, from the table: inline-level *and* with a formatting context
     * of its own. One half alone would call `inline` atomic. */
    return MakeCssDisplayOuter(display) == RoboCssDisplayOuterInline &&
           MakeCssDisplayInner(display) == RoboCssDisplayInnerFlowRoot;
}

const char *CssDisplayOuterName(CssDisplayOuter outer)
{
    switch (outer) {
    case RoboCssDisplayOuterNone:
        return "none";
    case RoboCssDisplayOuterBlock:
        return "block";
    case RoboCssDisplayOuterInline:
        return "inline";
    default:
        return "unknown";
    }
}

const char *CssDisplayInnerName(CssDisplayInner inner)
{
    switch (inner) {
    case RoboCssDisplayInnerNone:
        return "none";
    case RoboCssDisplayInnerFlow:
        return "flow";
    case RoboCssDisplayInnerFlowRoot:
        return "flow-root";
    case RoboCssDisplayInnerFlex:
        return "flex";
    case RoboCssDisplayInnerGrid:
        return "grid";
    default:
        return "unknown";
    }
}
