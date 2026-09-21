/*
 * RoboHTML element tag identity (Phase 8.11) — the vocabulary table.
 *
 * One table is the whole answer to "what is `<div>`?": its canonical name,
 * its coarse role, and its intrinsic flags. The role table that Phase 8.5
 * kept by name is gone; HtmlElementClassify now reads the role from
 * this row, so the element model and the tag model are one model.
 *
 * Rows are declared in enum order. That is a load-bearing choice, not
 * cosmetics: the enumerators are dense and sequential from 1, so
 * `tagName(tag)` is `kTags[tag - 1].name` with no lookup structure, no
 * hash and no second index. robo_html_tag_table_is_dense in the suite
 * asserts the row order, the ids and the round-trip, which is what makes a
 * future edit that inserts a tag worth trusting.
 *
 * Void-ness lives here rather than in the tree builder because it is a fact
 * about the *element type* and not a parsing decision: `<br>` has no
 * children because `br` is void, and the parser merely obeys. The tree
 * builder's IsVoidElement now asks this table, so the runtime has one
 * answer to that question instead of two that could drift.
 *
 * Kept in the tree builder, deliberately: the start-tag table that closes an
 * open `<p>`, the head-content list and the text-family table. Those are
 * parsing *algorithm* tables — they decide what the tokenizer does next and
 * include contextual entries, so moving them into an element-identity table
 * would confuse identity with parser policy.
 */

#include "RoboHTML/RoboHTMLTag.h"

#include "RoboHTMLInternal.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* The vocabulary                                                      */
/* ------------------------------------------------------------------ */

typedef struct RoboTagRow {
    /** Enumerator this row defines; must equal (row index + 1). */
    HtmlTag tag;
    /** Canonical lowercase name; the bytes the table owns. */
    const char *name;
    /** Coarse role; the single definition of the element's class. */
    HtmlElementClass cls;
    /** Mask of ROBO_HTML_TAG_FLAG_*. */
    unsigned int flags;
} TagRow;

/*
 * Grouped by what the element *is*, in the order the enumerators are
 * declared: the basic elements this phase introduces first, then the rest of
 * the vocabulary Phase 8.5 already classified by name, promoted to identity
 * with their roles unchanged.
 */
static const TagRow kTags[] = {
    /* -- Document structure (Phase 8.11) ------------------------------ */
    {RoboHtmlTagHtml, "html", RoboHtmlElementClassRoot,
     RoboHtmlTagFlagRoot | RoboHtmlTagFlagDocumentStructure},
    {RoboHtmlTagHead, "head", RoboHtmlElementClassMetadata,
     RoboHtmlTagFlagMetadata | RoboHtmlTagFlagDocumentStructure},
    {RoboHtmlTagBody, "body", RoboHtmlElementClassSection,
     RoboHtmlTagFlagDocumentStructure},
    {RoboHtmlTagTitle, "title", RoboHtmlElementClassMetadata,
     RoboHtmlTagFlagMetadata},

    /* -- Structural content (Phase 8.11) ----------------------------- */
    {RoboHtmlTagDiv, "div", RoboHtmlElementClassSection,
     RoboHtmlTagFlagNone},
    {RoboHtmlTagSpan, "span", RoboHtmlElementClassText,
     RoboHtmlTagFlagTextLevel},
    {RoboHtmlTagP, "p", RoboHtmlElementClassParagraph,
     RoboHtmlTagFlagNone},
    {RoboHtmlTagBr, "br", RoboHtmlElementClassText,
     RoboHtmlTagFlagVoid | RoboHtmlTagFlagTextLevel},

    /* -- Headings (Phase 8.11) --------------------------------------- */
    {RoboHtmlTagH1, "h1", RoboHtmlElementClassHeading,
     RoboHtmlTagFlagHeading},
    {RoboHtmlTagH2, "h2", RoboHtmlElementClassHeading,
     RoboHtmlTagFlagHeading},
    {RoboHtmlTagH3, "h3", RoboHtmlElementClassHeading,
     RoboHtmlTagFlagHeading},
    {RoboHtmlTagH4, "h4", RoboHtmlElementClassHeading,
     RoboHtmlTagFlagHeading},
    {RoboHtmlTagH5, "h5", RoboHtmlElementClassHeading,
     RoboHtmlTagFlagHeading},
    {RoboHtmlTagH6, "h6", RoboHtmlElementClassHeading,
     RoboHtmlTagFlagHeading},

    /* -- Lists (Phase 8.11) ------------------------------------------ */
    {RoboHtmlTagUl, "ul", RoboHtmlElementClassSection,
     RoboHtmlTagFlagList},
    {RoboHtmlTagOl, "ol", RoboHtmlElementClassSection,
     RoboHtmlTagFlagList},
    {RoboHtmlTagLi, "li", RoboHtmlElementClassSection,
     RoboHtmlTagFlagListItem},

    /* -- Inline semantics (Phase 8.11) ------------------------------- */
    {RoboHtmlTagStrong, "strong", RoboHtmlElementClassText,
     RoboHtmlTagFlagTextLevel},
    {RoboHtmlTagEm, "em", RoboHtmlElementClassText,
     RoboHtmlTagFlagTextLevel},
    {RoboHtmlTagB, "b", RoboHtmlElementClassText,
     RoboHtmlTagFlagTextLevel},
    {RoboHtmlTagI, "i", RoboHtmlElementClassText,
     RoboHtmlTagFlagTextLevel},
    {RoboHtmlTagU, "u", RoboHtmlElementClassText,
     RoboHtmlTagFlagTextLevel},
    {RoboHtmlTagSmall, "small", RoboHtmlElementClassText,
     RoboHtmlTagFlagTextLevel},
    {RoboHtmlTagCode, "code", RoboHtmlElementClassText,
     RoboHtmlTagFlagTextLevel},
    {RoboHtmlTagPre, "pre", RoboHtmlElementClassSection,
     RoboHtmlTagFlagNone},

    /* -- Generic semantic regions (Phase 8.11) ----------------------- */
    {RoboHtmlTagSection, "section", RoboHtmlElementClassSection,
     RoboHtmlTagFlagSectioning},
    {RoboHtmlTagArticle, "article", RoboHtmlElementClassSection,
     RoboHtmlTagFlagSectioning},
    {RoboHtmlTagNav, "nav", RoboHtmlElementClassSection,
     RoboHtmlTagFlagSectioning},
    {RoboHtmlTagMain, "main", RoboHtmlElementClassSection,
     RoboHtmlTagFlagRegion},
    {RoboHtmlTagHeader, "header", RoboHtmlElementClassSection,
     RoboHtmlTagFlagRegion},
    {RoboHtmlTagFooter, "footer", RoboHtmlElementClassSection,
     RoboHtmlTagFlagRegion},
    {RoboHtmlTagAside, "aside", RoboHtmlElementClassSection,
     RoboHtmlTagFlagSectioning},

    /* -- Remaining document metadata --------------------------------- */
    {RoboHtmlTagBase, "base", RoboHtmlElementClassMetadata,
     RoboHtmlTagFlagMetadata | RoboHtmlTagFlagVoid},
    {RoboHtmlTagBasefont, "basefont", RoboHtmlElementClassMetadata,
     RoboHtmlTagFlagMetadata | RoboHtmlTagFlagVoid},
    {RoboHtmlTagBgsound, "bgsound", RoboHtmlElementClassMetadata,
     RoboHtmlTagFlagMetadata | RoboHtmlTagFlagVoid},
    {RoboHtmlTagLink, "link", RoboHtmlElementClassMetadata,
     RoboHtmlTagFlagMetadata | RoboHtmlTagFlagVoid},
    {RoboHtmlTagMeta, "meta", RoboHtmlElementClassMetadata,
     RoboHtmlTagFlagMetadata | RoboHtmlTagFlagVoid},
    {RoboHtmlTagNoscript, "noscript", RoboHtmlElementClassMetadata,
     RoboHtmlTagFlagMetadata},
    {RoboHtmlTagScript, "script", RoboHtmlElementClassMetadata,
     RoboHtmlTagFlagMetadata},
    {RoboHtmlTagStyle, "style", RoboHtmlElementClassMetadata,
     RoboHtmlTagFlagMetadata},
    {RoboHtmlTagTemplate, "template", RoboHtmlElementClassMetadata,
     RoboHtmlTagFlagMetadata},

    /* -- Remaining block containers ---------------------------------- */
    {RoboHtmlTagAddress, "address", RoboHtmlElementClassSection,
     RoboHtmlTagFlagNone},
    {RoboHtmlTagBlockquote, "blockquote", RoboHtmlElementClassSection,
     RoboHtmlTagFlagNone},
    {RoboHtmlTagDd, "dd", RoboHtmlElementClassSection,
     RoboHtmlTagFlagNone},
    {RoboHtmlTagDetails, "details", RoboHtmlElementClassSection,
     RoboHtmlTagFlagNone},
    {RoboHtmlTagDl, "dl", RoboHtmlElementClassSection,
     RoboHtmlTagFlagNone},
    {RoboHtmlTagDt, "dt", RoboHtmlElementClassSection,
     RoboHtmlTagFlagNone},
    {RoboHtmlTagFieldset, "fieldset", RoboHtmlElementClassSection,
     RoboHtmlTagFlagFormAssociated},
    {RoboHtmlTagFigcaption, "figcaption",
     RoboHtmlElementClassSection, RoboHtmlTagFlagNone},
    {RoboHtmlTagFigure, "figure", RoboHtmlElementClassSection,
     RoboHtmlTagFlagNone},
    {RoboHtmlTagHgroup, "hgroup", RoboHtmlElementClassSection,
     RoboHtmlTagFlagNone},
    {RoboHtmlTagHr, "hr", RoboHtmlElementClassSection,
     RoboHtmlTagFlagVoid},
    {RoboHtmlTagLegend, "legend", RoboHtmlElementClassSection,
     RoboHtmlTagFlagNone},
    {RoboHtmlTagMenu, "menu", RoboHtmlElementClassSection,
     RoboHtmlTagFlagNone},
    {RoboHtmlTagSummary, "summary", RoboHtmlElementClassSection,
     RoboHtmlTagFlagNone},

    /* -- Remaining text-level elements ------------------------------- */
    {RoboHtmlTagA, "a", RoboHtmlElementClassText,
     RoboHtmlTagFlagTextLevel},
    {RoboHtmlTagAbbr, "abbr", RoboHtmlElementClassText,
     RoboHtmlTagFlagTextLevel},
    {RoboHtmlTagBdi, "bdi", RoboHtmlElementClassText,
     RoboHtmlTagFlagTextLevel},
    {RoboHtmlTagBdo, "bdo", RoboHtmlElementClassText,
     RoboHtmlTagFlagTextLevel},
    {RoboHtmlTagCite, "cite", RoboHtmlElementClassText,
     RoboHtmlTagFlagTextLevel},
    {RoboHtmlTagData, "data", RoboHtmlElementClassText,
     RoboHtmlTagFlagTextLevel},
    {RoboHtmlTagDel, "del", RoboHtmlElementClassText,
     RoboHtmlTagFlagTextLevel},
    {RoboHtmlTagDfn, "dfn", RoboHtmlElementClassText,
     RoboHtmlTagFlagTextLevel},
    {RoboHtmlTagIns, "ins", RoboHtmlElementClassText,
     RoboHtmlTagFlagTextLevel},
    {RoboHtmlTagKbd, "kbd", RoboHtmlElementClassText,
     RoboHtmlTagFlagTextLevel},
    {RoboHtmlTagMark, "mark", RoboHtmlElementClassText,
     RoboHtmlTagFlagTextLevel},
    {RoboHtmlTagQ, "q", RoboHtmlElementClassText,
     RoboHtmlTagFlagTextLevel},
    {RoboHtmlTagRp, "rp", RoboHtmlElementClassText,
     RoboHtmlTagFlagTextLevel},
    {RoboHtmlTagRt, "rt", RoboHtmlElementClassText,
     RoboHtmlTagFlagTextLevel},
    {RoboHtmlTagRuby, "ruby", RoboHtmlElementClassText,
     RoboHtmlTagFlagTextLevel},
    {RoboHtmlTagS, "s", RoboHtmlElementClassText,
     RoboHtmlTagFlagTextLevel},
    {RoboHtmlTagSamp, "samp", RoboHtmlElementClassText,
     RoboHtmlTagFlagTextLevel},
    {RoboHtmlTagSub, "sub", RoboHtmlElementClassText,
     RoboHtmlTagFlagTextLevel},
    {RoboHtmlTagSup, "sup", RoboHtmlElementClassText,
     RoboHtmlTagFlagTextLevel},
    {RoboHtmlTagTime, "time", RoboHtmlElementClassText,
     RoboHtmlTagFlagTextLevel},
    {RoboHtmlTagVar, "var", RoboHtmlElementClassText,
     RoboHtmlTagFlagTextLevel},
    {RoboHtmlTagWbr, "wbr", RoboHtmlElementClassText,
     RoboHtmlTagFlagVoid | RoboHtmlTagFlagTextLevel},

    /* -- Replaced / embedded content --------------------------------- */
    {RoboHtmlTagArea, "area", RoboHtmlElementClassReplaced,
     RoboHtmlTagFlagVoid},
    {RoboHtmlTagAudio, "audio", RoboHtmlElementClassReplaced,
     RoboHtmlTagFlagNone},
    {RoboHtmlTagCanvas, "canvas", RoboHtmlElementClassReplaced,
     RoboHtmlTagFlagNone},
    {RoboHtmlTagEmbed, "embed", RoboHtmlElementClassReplaced,
     RoboHtmlTagFlagVoid},
    {RoboHtmlTagIframe, "iframe", RoboHtmlElementClassReplaced,
     RoboHtmlTagFlagNone},
    {RoboHtmlTagImg, "img", RoboHtmlElementClassReplaced,
     RoboHtmlTagFlagVoid},
    {RoboHtmlTagMap, "map", RoboHtmlElementClassReplaced,
     RoboHtmlTagFlagNone},
    {RoboHtmlTagObject, "object", RoboHtmlElementClassReplaced,
     RoboHtmlTagFlagFormAssociated},
    {RoboHtmlTagParam, "param", RoboHtmlElementClassReplaced,
     RoboHtmlTagFlagVoid},
    {RoboHtmlTagPicture, "picture", RoboHtmlElementClassReplaced,
     RoboHtmlTagFlagNone},
    {RoboHtmlTagSource, "source", RoboHtmlElementClassReplaced,
     RoboHtmlTagFlagVoid},
    {RoboHtmlTagTrack, "track", RoboHtmlElementClassReplaced,
     RoboHtmlTagFlagVoid},
    {RoboHtmlTagVideo, "video", RoboHtmlElementClassReplaced,
     RoboHtmlTagFlagNone},

    /* -- Form controls ----------------------------------------------- */
    /*
     * The two form flags (Phase 8.14) are columns of the type, like void:
     * which elements can have a form owner, and which a <label> can attach
     * to, are facts about the element, not parser policy. Note they are not
     * the same set — <output> is form-associated but not labelable, and
     * <label> is neither.
     */
    {RoboHtmlTagButton, "button", RoboHtmlElementClassControl,
     RoboHtmlTagFlagFormAssociated | RoboHtmlTagFlagLabelable},
    {RoboHtmlTagDatalist, "datalist", RoboHtmlElementClassControl,
     RoboHtmlTagFlagNone},
    {RoboHtmlTagForm, "form", RoboHtmlElementClassControl,
     RoboHtmlTagFlagNone},
    {RoboHtmlTagInput, "input", RoboHtmlElementClassControl,
     RoboHtmlTagFlagVoid | RoboHtmlTagFlagFormAssociated |
         RoboHtmlTagFlagLabelable},
    {RoboHtmlTagLabel, "label", RoboHtmlElementClassControl,
     RoboHtmlTagFlagNone},
    {RoboHtmlTagMeter, "meter", RoboHtmlElementClassControl,
     RoboHtmlTagFlagLabelable},
    {RoboHtmlTagOptgroup, "optgroup", RoboHtmlElementClassControl,
     RoboHtmlTagFlagNone},
    {RoboHtmlTagOption, "option", RoboHtmlElementClassControl,
     RoboHtmlTagFlagNone},
    {RoboHtmlTagOutput, "output", RoboHtmlElementClassControl,
     RoboHtmlTagFlagFormAssociated | RoboHtmlTagFlagLabelable},
    {RoboHtmlTagProgress, "progress", RoboHtmlElementClassControl,
     RoboHtmlTagFlagLabelable},
    {RoboHtmlTagSelect, "select", RoboHtmlElementClassControl,
     RoboHtmlTagFlagFormAssociated | RoboHtmlTagFlagLabelable},
    {RoboHtmlTagTextarea, "textarea", RoboHtmlElementClassControl,
     RoboHtmlTagFlagFormAssociated | RoboHtmlTagFlagLabelable},

    /* -- Void elements with no classified role yet -------------------- */
    {RoboHtmlTagCol, "col", RoboHtmlElementClassGeneric,
     RoboHtmlTagFlagVoid},
    {RoboHtmlTagFrame, "frame", RoboHtmlElementClassGeneric,
     RoboHtmlTagFlagVoid},
    {RoboHtmlTagKeygen, "keygen", RoboHtmlElementClassGeneric,
     RoboHtmlTagFlagVoid},
};

/** Rows in @c kTags — the same number as RoboHtmlTagCount - 1. */
#define TAG_ROW_COUNT (sizeof(kTags) / sizeof(kTags[0]))

/* ------------------------------------------------------------------ */
/* Identity                                                            */
/* ------------------------------------------------------------------ */

/**
 * Row for @p tag, or NULL when @p tag is UNKNOWN/undefined.
 *
 * Rows are declared in enum order and the enumerators are dense from 1, so
 * the row for a tag is the row at `tag - 1` — no lookup structure, and the
 * suite's density test is what keeps that true.
 */
static const TagRow *GetRoboTagRow(HtmlTag tag)
{
    size_t index;

    if (tag <= RoboHtmlTagUnknown || (int)tag >= RoboHtmlTagCount) {
        return NULL;
    }
    index = (size_t)tag - 1u;
    if (index >= TAG_ROW_COUNT || kTags[index].tag != tag) {
        return NULL;
    }
    return &kTags[index];
}

int HtmlTagIsKnown(HtmlTag tag)
{
    return GetRoboTagRow(tag) != NULL;
}

const char *HtmlTagName(HtmlTag tag)
{
    const TagRow *row = GetRoboTagRow(tag);

    return row ? row->name : "";
}

HtmlElementClass HtmlTagClass(HtmlTag tag)
{
    const TagRow *row = GetRoboTagRow(tag);

    return row ? row->cls : RoboHtmlElementClassGeneric;
}

unsigned int HtmlTagFlags(HtmlTag tag)
{
    const TagRow *row = GetRoboTagRow(tag);

    return row ? row->flags : RoboHtmlTagFlagNone;
}

HtmlTag HtmlTagFromName(HtmlNamespace ns,
                                        const char *localName)
{
    size_t i;

    if (!localName || localName[0] == '\0') {
        return RoboHtmlTagUnknown;
    }
    /*
     * Only the HTML vocabulary is recognised: an SVG <a> is not an HTML <a>,
     * so resolving it to RoboHtmlTagA would be a lie. Foreign content
     * resolves to UNKNOWN, which is the honest answer until a phase that
     * owns namespace switching says otherwise.
     */
    if (ns != RoboHtmlNamespaceHtml) {
        return RoboHtmlTagUnknown;
    }
    for (i = 0u; i < TAG_ROW_COUNT; i++) {
        /* ASCII case-insensitive in both directions, so a caller may pass
         * either case — the element model owns the one comparison rule. */
        if (HtmlNameEqual(kTags[i].name, localName)) {
            return kTags[i].tag;
        }
    }
    return RoboHtmlTagUnknown;
}

size_t HtmlTagCount(void)
{
    return TAG_ROW_COUNT;
}

Result HtmlTagAt(size_t index, HtmlTag *out)
{
    if (!out) {
        return RoboInvalidArgument;
    }
    if (index >= TAG_ROW_COUNT) {
        *out = RoboHtmlTagUnknown;
        return RoboNotFound;
    }
    *out = kTags[index].tag;
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Predicates                                                          */
/* ------------------------------------------------------------------ */

static int TagHasFlag(HtmlTag tag, unsigned int flag)
{
    return (HtmlTagFlags(tag) & flag) != 0u;
}

int HtmlTagIsVoid(HtmlTag tag)
{
    return TagHasFlag(tag, RoboHtmlTagFlagVoid);
}

int HtmlTagIsRoot(HtmlTag tag)
{
    return TagHasFlag(tag, RoboHtmlTagFlagRoot);
}

int HtmlTagIsDocumentStructure(HtmlTag tag)
{
    return TagHasFlag(tag, RoboHtmlTagFlagDocumentStructure);
}

int HtmlTagIsMetadata(HtmlTag tag)
{
    return TagHasFlag(tag, RoboHtmlTagFlagMetadata);
}

int HtmlTagIsSectioning(HtmlTag tag)
{
    return TagHasFlag(tag, RoboHtmlTagFlagSectioning);
}

int HtmlTagIsRegion(HtmlTag tag)
{
    return TagHasFlag(tag, RoboHtmlTagFlagRegion);
}

int HtmlTagIsHeading(HtmlTag tag)
{
    return TagHasFlag(tag, RoboHtmlTagFlagHeading);
}

int HtmlTagIsList(HtmlTag tag)
{
    return TagHasFlag(tag, RoboHtmlTagFlagList);
}

int HtmlTagIsListItem(HtmlTag tag)
{
    return TagHasFlag(tag, RoboHtmlTagFlagListItem);
}

int HtmlTagIsTextLevel(HtmlTag tag)
{
    return TagHasFlag(tag, RoboHtmlTagFlagTextLevel);
}

int HtmlTagIsFormAssociated(HtmlTag tag)
{
    return TagHasFlag(tag, RoboHtmlTagFlagFormAssociated);
}

int HtmlTagIsLabelable(HtmlTag tag)
{
    return TagHasFlag(tag, RoboHtmlTagFlagLabelable);
}
