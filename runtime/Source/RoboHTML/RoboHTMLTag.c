/*
 * RoboHTML element tag identity (Phase 8.11) — the vocabulary table.
 *
 * One table is the whole answer to "what is `<div>`?": its canonical name,
 * its coarse role, and its intrinsic flags. The role table that Phase 8.5
 * kept by name is gone; robo_html_element_classify now reads the role from
 * this row, so the element model and the tag model are one model.
 *
 * Rows are declared in enum order. That is a load-bearing choice, not
 * cosmetics: the enumerators are dense and sequential from 1, so
 * `tag_name(tag)` is `k_tags[tag - 1].name` with no lookup structure, no
 * hash and no second index. robo_html_tag_table_is_dense in the suite
 * asserts the row order, the ids and the round-trip, which is what makes a
 * future edit that inserts a tag worth trusting.
 *
 * Void-ness lives here rather than in the tree builder because it is a fact
 * about the *element type* and not a parsing decision: `<br>` has no
 * children because `br` is void, and the parser merely obeys. The tree
 * builder's is_void_element now asks this table, so the runtime has one
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

typedef struct tag_row {
    /** Enumerator this row defines; must equal (row index + 1). */
    robo_html_tag_t tag;
    /** Canonical lowercase name; the bytes the table owns. */
    const char *name;
    /** Coarse role; the single definition of the element's class. */
    robo_html_element_class_t cls;
    /** Mask of ROBO_HTML_TAG_FLAG_*. */
    unsigned int flags;
} tag_row_t;

/*
 * Grouped by what the element *is*, in the order the enumerators are
 * declared: the basic elements this phase introduces first, then the rest of
 * the vocabulary Phase 8.5 already classified by name, promoted to identity
 * with their roles unchanged.
 */
static const tag_row_t k_tags[] = {
    /* -- Document structure (Phase 8.11) ------------------------------ */
    {ROBO_HTML_TAG_HTML, "html", ROBO_HTML_ELEMENT_CLASS_ROOT,
     ROBO_HTML_TAG_FLAG_ROOT | ROBO_HTML_TAG_FLAG_DOCUMENT_STRUCTURE},
    {ROBO_HTML_TAG_HEAD, "head", ROBO_HTML_ELEMENT_CLASS_METADATA,
     ROBO_HTML_TAG_FLAG_METADATA | ROBO_HTML_TAG_FLAG_DOCUMENT_STRUCTURE},
    {ROBO_HTML_TAG_BODY, "body", ROBO_HTML_ELEMENT_CLASS_SECTION,
     ROBO_HTML_TAG_FLAG_DOCUMENT_STRUCTURE},
    {ROBO_HTML_TAG_TITLE, "title", ROBO_HTML_ELEMENT_CLASS_METADATA,
     ROBO_HTML_TAG_FLAG_METADATA},

    /* -- Structural content (Phase 8.11) ----------------------------- */
    {ROBO_HTML_TAG_DIV, "div", ROBO_HTML_ELEMENT_CLASS_SECTION,
     ROBO_HTML_TAG_FLAG_NONE},
    {ROBO_HTML_TAG_SPAN, "span", ROBO_HTML_ELEMENT_CLASS_TEXT,
     ROBO_HTML_TAG_FLAG_TEXT_LEVEL},
    {ROBO_HTML_TAG_P, "p", ROBO_HTML_ELEMENT_CLASS_PARAGRAPH,
     ROBO_HTML_TAG_FLAG_NONE},
    {ROBO_HTML_TAG_BR, "br", ROBO_HTML_ELEMENT_CLASS_TEXT,
     ROBO_HTML_TAG_FLAG_VOID | ROBO_HTML_TAG_FLAG_TEXT_LEVEL},

    /* -- Headings (Phase 8.11) --------------------------------------- */
    {ROBO_HTML_TAG_H1, "h1", ROBO_HTML_ELEMENT_CLASS_HEADING,
     ROBO_HTML_TAG_FLAG_HEADING},
    {ROBO_HTML_TAG_H2, "h2", ROBO_HTML_ELEMENT_CLASS_HEADING,
     ROBO_HTML_TAG_FLAG_HEADING},
    {ROBO_HTML_TAG_H3, "h3", ROBO_HTML_ELEMENT_CLASS_HEADING,
     ROBO_HTML_TAG_FLAG_HEADING},
    {ROBO_HTML_TAG_H4, "h4", ROBO_HTML_ELEMENT_CLASS_HEADING,
     ROBO_HTML_TAG_FLAG_HEADING},
    {ROBO_HTML_TAG_H5, "h5", ROBO_HTML_ELEMENT_CLASS_HEADING,
     ROBO_HTML_TAG_FLAG_HEADING},
    {ROBO_HTML_TAG_H6, "h6", ROBO_HTML_ELEMENT_CLASS_HEADING,
     ROBO_HTML_TAG_FLAG_HEADING},

    /* -- Lists (Phase 8.11) ------------------------------------------ */
    {ROBO_HTML_TAG_UL, "ul", ROBO_HTML_ELEMENT_CLASS_SECTION,
     ROBO_HTML_TAG_FLAG_LIST},
    {ROBO_HTML_TAG_OL, "ol", ROBO_HTML_ELEMENT_CLASS_SECTION,
     ROBO_HTML_TAG_FLAG_LIST},
    {ROBO_HTML_TAG_LI, "li", ROBO_HTML_ELEMENT_CLASS_SECTION,
     ROBO_HTML_TAG_FLAG_LIST_ITEM},

    /* -- Inline semantics (Phase 8.11) ------------------------------- */
    {ROBO_HTML_TAG_STRONG, "strong", ROBO_HTML_ELEMENT_CLASS_TEXT,
     ROBO_HTML_TAG_FLAG_TEXT_LEVEL},
    {ROBO_HTML_TAG_EM, "em", ROBO_HTML_ELEMENT_CLASS_TEXT,
     ROBO_HTML_TAG_FLAG_TEXT_LEVEL},
    {ROBO_HTML_TAG_B, "b", ROBO_HTML_ELEMENT_CLASS_TEXT,
     ROBO_HTML_TAG_FLAG_TEXT_LEVEL},
    {ROBO_HTML_TAG_I, "i", ROBO_HTML_ELEMENT_CLASS_TEXT,
     ROBO_HTML_TAG_FLAG_TEXT_LEVEL},
    {ROBO_HTML_TAG_U, "u", ROBO_HTML_ELEMENT_CLASS_TEXT,
     ROBO_HTML_TAG_FLAG_TEXT_LEVEL},
    {ROBO_HTML_TAG_SMALL, "small", ROBO_HTML_ELEMENT_CLASS_TEXT,
     ROBO_HTML_TAG_FLAG_TEXT_LEVEL},
    {ROBO_HTML_TAG_CODE, "code", ROBO_HTML_ELEMENT_CLASS_TEXT,
     ROBO_HTML_TAG_FLAG_TEXT_LEVEL},
    {ROBO_HTML_TAG_PRE, "pre", ROBO_HTML_ELEMENT_CLASS_SECTION,
     ROBO_HTML_TAG_FLAG_NONE},

    /* -- Generic semantic regions (Phase 8.11) ----------------------- */
    {ROBO_HTML_TAG_SECTION, "section", ROBO_HTML_ELEMENT_CLASS_SECTION,
     ROBO_HTML_TAG_FLAG_SECTIONING},
    {ROBO_HTML_TAG_ARTICLE, "article", ROBO_HTML_ELEMENT_CLASS_SECTION,
     ROBO_HTML_TAG_FLAG_SECTIONING},
    {ROBO_HTML_TAG_NAV, "nav", ROBO_HTML_ELEMENT_CLASS_SECTION,
     ROBO_HTML_TAG_FLAG_SECTIONING},
    {ROBO_HTML_TAG_MAIN, "main", ROBO_HTML_ELEMENT_CLASS_SECTION,
     ROBO_HTML_TAG_FLAG_REGION},
    {ROBO_HTML_TAG_HEADER, "header", ROBO_HTML_ELEMENT_CLASS_SECTION,
     ROBO_HTML_TAG_FLAG_REGION},
    {ROBO_HTML_TAG_FOOTER, "footer", ROBO_HTML_ELEMENT_CLASS_SECTION,
     ROBO_HTML_TAG_FLAG_REGION},
    {ROBO_HTML_TAG_ASIDE, "aside", ROBO_HTML_ELEMENT_CLASS_SECTION,
     ROBO_HTML_TAG_FLAG_SECTIONING},

    /* -- Remaining document metadata --------------------------------- */
    {ROBO_HTML_TAG_BASE, "base", ROBO_HTML_ELEMENT_CLASS_METADATA,
     ROBO_HTML_TAG_FLAG_METADATA | ROBO_HTML_TAG_FLAG_VOID},
    {ROBO_HTML_TAG_BASEFONT, "basefont", ROBO_HTML_ELEMENT_CLASS_METADATA,
     ROBO_HTML_TAG_FLAG_METADATA | ROBO_HTML_TAG_FLAG_VOID},
    {ROBO_HTML_TAG_BGSOUND, "bgsound", ROBO_HTML_ELEMENT_CLASS_METADATA,
     ROBO_HTML_TAG_FLAG_METADATA | ROBO_HTML_TAG_FLAG_VOID},
    {ROBO_HTML_TAG_LINK, "link", ROBO_HTML_ELEMENT_CLASS_METADATA,
     ROBO_HTML_TAG_FLAG_METADATA | ROBO_HTML_TAG_FLAG_VOID},
    {ROBO_HTML_TAG_META, "meta", ROBO_HTML_ELEMENT_CLASS_METADATA,
     ROBO_HTML_TAG_FLAG_METADATA | ROBO_HTML_TAG_FLAG_VOID},
    {ROBO_HTML_TAG_NOSCRIPT, "noscript", ROBO_HTML_ELEMENT_CLASS_METADATA,
     ROBO_HTML_TAG_FLAG_METADATA},
    {ROBO_HTML_TAG_SCRIPT, "script", ROBO_HTML_ELEMENT_CLASS_METADATA,
     ROBO_HTML_TAG_FLAG_METADATA},
    {ROBO_HTML_TAG_STYLE, "style", ROBO_HTML_ELEMENT_CLASS_METADATA,
     ROBO_HTML_TAG_FLAG_METADATA},
    {ROBO_HTML_TAG_TEMPLATE, "template", ROBO_HTML_ELEMENT_CLASS_METADATA,
     ROBO_HTML_TAG_FLAG_METADATA},

    /* -- Remaining block containers ---------------------------------- */
    {ROBO_HTML_TAG_ADDRESS, "address", ROBO_HTML_ELEMENT_CLASS_SECTION,
     ROBO_HTML_TAG_FLAG_NONE},
    {ROBO_HTML_TAG_BLOCKQUOTE, "blockquote", ROBO_HTML_ELEMENT_CLASS_SECTION,
     ROBO_HTML_TAG_FLAG_NONE},
    {ROBO_HTML_TAG_DD, "dd", ROBO_HTML_ELEMENT_CLASS_SECTION,
     ROBO_HTML_TAG_FLAG_NONE},
    {ROBO_HTML_TAG_DETAILS, "details", ROBO_HTML_ELEMENT_CLASS_SECTION,
     ROBO_HTML_TAG_FLAG_NONE},
    {ROBO_HTML_TAG_DL, "dl", ROBO_HTML_ELEMENT_CLASS_SECTION,
     ROBO_HTML_TAG_FLAG_NONE},
    {ROBO_HTML_TAG_DT, "dt", ROBO_HTML_ELEMENT_CLASS_SECTION,
     ROBO_HTML_TAG_FLAG_NONE},
    {ROBO_HTML_TAG_FIELDSET, "fieldset", ROBO_HTML_ELEMENT_CLASS_SECTION,
     ROBO_HTML_TAG_FLAG_NONE},
    {ROBO_HTML_TAG_FIGCAPTION, "figcaption",
     ROBO_HTML_ELEMENT_CLASS_SECTION, ROBO_HTML_TAG_FLAG_NONE},
    {ROBO_HTML_TAG_FIGURE, "figure", ROBO_HTML_ELEMENT_CLASS_SECTION,
     ROBO_HTML_TAG_FLAG_NONE},
    {ROBO_HTML_TAG_HGROUP, "hgroup", ROBO_HTML_ELEMENT_CLASS_SECTION,
     ROBO_HTML_TAG_FLAG_NONE},
    {ROBO_HTML_TAG_HR, "hr", ROBO_HTML_ELEMENT_CLASS_SECTION,
     ROBO_HTML_TAG_FLAG_VOID},
    {ROBO_HTML_TAG_LEGEND, "legend", ROBO_HTML_ELEMENT_CLASS_SECTION,
     ROBO_HTML_TAG_FLAG_NONE},
    {ROBO_HTML_TAG_MENU, "menu", ROBO_HTML_ELEMENT_CLASS_SECTION,
     ROBO_HTML_TAG_FLAG_NONE},
    {ROBO_HTML_TAG_SUMMARY, "summary", ROBO_HTML_ELEMENT_CLASS_SECTION,
     ROBO_HTML_TAG_FLAG_NONE},

    /* -- Remaining text-level elements ------------------------------- */
    {ROBO_HTML_TAG_A, "a", ROBO_HTML_ELEMENT_CLASS_TEXT,
     ROBO_HTML_TAG_FLAG_TEXT_LEVEL},
    {ROBO_HTML_TAG_ABBR, "abbr", ROBO_HTML_ELEMENT_CLASS_TEXT,
     ROBO_HTML_TAG_FLAG_TEXT_LEVEL},
    {ROBO_HTML_TAG_BDI, "bdi", ROBO_HTML_ELEMENT_CLASS_TEXT,
     ROBO_HTML_TAG_FLAG_TEXT_LEVEL},
    {ROBO_HTML_TAG_BDO, "bdo", ROBO_HTML_ELEMENT_CLASS_TEXT,
     ROBO_HTML_TAG_FLAG_TEXT_LEVEL},
    {ROBO_HTML_TAG_CITE, "cite", ROBO_HTML_ELEMENT_CLASS_TEXT,
     ROBO_HTML_TAG_FLAG_TEXT_LEVEL},
    {ROBO_HTML_TAG_DATA, "data", ROBO_HTML_ELEMENT_CLASS_TEXT,
     ROBO_HTML_TAG_FLAG_TEXT_LEVEL},
    {ROBO_HTML_TAG_DEL, "del", ROBO_HTML_ELEMENT_CLASS_TEXT,
     ROBO_HTML_TAG_FLAG_TEXT_LEVEL},
    {ROBO_HTML_TAG_DFN, "dfn", ROBO_HTML_ELEMENT_CLASS_TEXT,
     ROBO_HTML_TAG_FLAG_TEXT_LEVEL},
    {ROBO_HTML_TAG_INS, "ins", ROBO_HTML_ELEMENT_CLASS_TEXT,
     ROBO_HTML_TAG_FLAG_TEXT_LEVEL},
    {ROBO_HTML_TAG_KBD, "kbd", ROBO_HTML_ELEMENT_CLASS_TEXT,
     ROBO_HTML_TAG_FLAG_TEXT_LEVEL},
    {ROBO_HTML_TAG_MARK, "mark", ROBO_HTML_ELEMENT_CLASS_TEXT,
     ROBO_HTML_TAG_FLAG_TEXT_LEVEL},
    {ROBO_HTML_TAG_Q, "q", ROBO_HTML_ELEMENT_CLASS_TEXT,
     ROBO_HTML_TAG_FLAG_TEXT_LEVEL},
    {ROBO_HTML_TAG_RP, "rp", ROBO_HTML_ELEMENT_CLASS_TEXT,
     ROBO_HTML_TAG_FLAG_TEXT_LEVEL},
    {ROBO_HTML_TAG_RT, "rt", ROBO_HTML_ELEMENT_CLASS_TEXT,
     ROBO_HTML_TAG_FLAG_TEXT_LEVEL},
    {ROBO_HTML_TAG_RUBY, "ruby", ROBO_HTML_ELEMENT_CLASS_TEXT,
     ROBO_HTML_TAG_FLAG_TEXT_LEVEL},
    {ROBO_HTML_TAG_S, "s", ROBO_HTML_ELEMENT_CLASS_TEXT,
     ROBO_HTML_TAG_FLAG_TEXT_LEVEL},
    {ROBO_HTML_TAG_SAMP, "samp", ROBO_HTML_ELEMENT_CLASS_TEXT,
     ROBO_HTML_TAG_FLAG_TEXT_LEVEL},
    {ROBO_HTML_TAG_SUB, "sub", ROBO_HTML_ELEMENT_CLASS_TEXT,
     ROBO_HTML_TAG_FLAG_TEXT_LEVEL},
    {ROBO_HTML_TAG_SUP, "sup", ROBO_HTML_ELEMENT_CLASS_TEXT,
     ROBO_HTML_TAG_FLAG_TEXT_LEVEL},
    {ROBO_HTML_TAG_TIME, "time", ROBO_HTML_ELEMENT_CLASS_TEXT,
     ROBO_HTML_TAG_FLAG_TEXT_LEVEL},
    {ROBO_HTML_TAG_VAR, "var", ROBO_HTML_ELEMENT_CLASS_TEXT,
     ROBO_HTML_TAG_FLAG_TEXT_LEVEL},
    {ROBO_HTML_TAG_WBR, "wbr", ROBO_HTML_ELEMENT_CLASS_TEXT,
     ROBO_HTML_TAG_FLAG_VOID | ROBO_HTML_TAG_FLAG_TEXT_LEVEL},

    /* -- Replaced / embedded content --------------------------------- */
    {ROBO_HTML_TAG_AREA, "area", ROBO_HTML_ELEMENT_CLASS_REPLACED,
     ROBO_HTML_TAG_FLAG_VOID},
    {ROBO_HTML_TAG_AUDIO, "audio", ROBO_HTML_ELEMENT_CLASS_REPLACED,
     ROBO_HTML_TAG_FLAG_NONE},
    {ROBO_HTML_TAG_CANVAS, "canvas", ROBO_HTML_ELEMENT_CLASS_REPLACED,
     ROBO_HTML_TAG_FLAG_NONE},
    {ROBO_HTML_TAG_EMBED, "embed", ROBO_HTML_ELEMENT_CLASS_REPLACED,
     ROBO_HTML_TAG_FLAG_VOID},
    {ROBO_HTML_TAG_IFRAME, "iframe", ROBO_HTML_ELEMENT_CLASS_REPLACED,
     ROBO_HTML_TAG_FLAG_NONE},
    {ROBO_HTML_TAG_IMG, "img", ROBO_HTML_ELEMENT_CLASS_REPLACED,
     ROBO_HTML_TAG_FLAG_VOID},
    {ROBO_HTML_TAG_MAP, "map", ROBO_HTML_ELEMENT_CLASS_REPLACED,
     ROBO_HTML_TAG_FLAG_NONE},
    {ROBO_HTML_TAG_OBJECT, "object", ROBO_HTML_ELEMENT_CLASS_REPLACED,
     ROBO_HTML_TAG_FLAG_NONE},
    {ROBO_HTML_TAG_PARAM, "param", ROBO_HTML_ELEMENT_CLASS_REPLACED,
     ROBO_HTML_TAG_FLAG_VOID},
    {ROBO_HTML_TAG_PICTURE, "picture", ROBO_HTML_ELEMENT_CLASS_REPLACED,
     ROBO_HTML_TAG_FLAG_NONE},
    {ROBO_HTML_TAG_SOURCE, "source", ROBO_HTML_ELEMENT_CLASS_REPLACED,
     ROBO_HTML_TAG_FLAG_VOID},
    {ROBO_HTML_TAG_TRACK, "track", ROBO_HTML_ELEMENT_CLASS_REPLACED,
     ROBO_HTML_TAG_FLAG_VOID},
    {ROBO_HTML_TAG_VIDEO, "video", ROBO_HTML_ELEMENT_CLASS_REPLACED,
     ROBO_HTML_TAG_FLAG_NONE},

    /* -- Form controls ----------------------------------------------- */
    {ROBO_HTML_TAG_BUTTON, "button", ROBO_HTML_ELEMENT_CLASS_CONTROL,
     ROBO_HTML_TAG_FLAG_NONE},
    {ROBO_HTML_TAG_DATALIST, "datalist", ROBO_HTML_ELEMENT_CLASS_CONTROL,
     ROBO_HTML_TAG_FLAG_NONE},
    {ROBO_HTML_TAG_FORM, "form", ROBO_HTML_ELEMENT_CLASS_CONTROL,
     ROBO_HTML_TAG_FLAG_NONE},
    {ROBO_HTML_TAG_INPUT, "input", ROBO_HTML_ELEMENT_CLASS_CONTROL,
     ROBO_HTML_TAG_FLAG_VOID},
    {ROBO_HTML_TAG_LABEL, "label", ROBO_HTML_ELEMENT_CLASS_CONTROL,
     ROBO_HTML_TAG_FLAG_NONE},
    {ROBO_HTML_TAG_METER, "meter", ROBO_HTML_ELEMENT_CLASS_CONTROL,
     ROBO_HTML_TAG_FLAG_NONE},
    {ROBO_HTML_TAG_OPTGROUP, "optgroup", ROBO_HTML_ELEMENT_CLASS_CONTROL,
     ROBO_HTML_TAG_FLAG_NONE},
    {ROBO_HTML_TAG_OPTION, "option", ROBO_HTML_ELEMENT_CLASS_CONTROL,
     ROBO_HTML_TAG_FLAG_NONE},
    {ROBO_HTML_TAG_OUTPUT, "output", ROBO_HTML_ELEMENT_CLASS_CONTROL,
     ROBO_HTML_TAG_FLAG_NONE},
    {ROBO_HTML_TAG_PROGRESS, "progress", ROBO_HTML_ELEMENT_CLASS_CONTROL,
     ROBO_HTML_TAG_FLAG_NONE},
    {ROBO_HTML_TAG_SELECT, "select", ROBO_HTML_ELEMENT_CLASS_CONTROL,
     ROBO_HTML_TAG_FLAG_NONE},
    {ROBO_HTML_TAG_TEXTAREA, "textarea", ROBO_HTML_ELEMENT_CLASS_CONTROL,
     ROBO_HTML_TAG_FLAG_NONE},

    /* -- Void elements with no classified role yet -------------------- */
    {ROBO_HTML_TAG_COL, "col", ROBO_HTML_ELEMENT_CLASS_GENERIC,
     ROBO_HTML_TAG_FLAG_VOID},
    {ROBO_HTML_TAG_FRAME, "frame", ROBO_HTML_ELEMENT_CLASS_GENERIC,
     ROBO_HTML_TAG_FLAG_VOID},
    {ROBO_HTML_TAG_KEYGEN, "keygen", ROBO_HTML_ELEMENT_CLASS_GENERIC,
     ROBO_HTML_TAG_FLAG_VOID},
};

/** Rows in @c k_tags — the same number as ROBO_HTML_TAG_COUNT - 1. */
#define TAG_ROW_COUNT (sizeof(k_tags) / sizeof(k_tags[0]))

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
static const tag_row_t *tag_row(robo_html_tag_t tag)
{
    size_t index;

    if (tag <= ROBO_HTML_TAG_UNKNOWN || (int)tag >= ROBO_HTML_TAG_COUNT) {
        return NULL;
    }
    index = (size_t)tag - 1u;
    if (index >= TAG_ROW_COUNT || k_tags[index].tag != tag) {
        return NULL;
    }
    return &k_tags[index];
}

int robo_html_tag_is_known(robo_html_tag_t tag)
{
    return tag_row(tag) != NULL;
}

const char *robo_html_tag_name(robo_html_tag_t tag)
{
    const tag_row_t *row = tag_row(tag);

    return row ? row->name : "";
}

robo_html_element_class_t robo_html_tag_class(robo_html_tag_t tag)
{
    const tag_row_t *row = tag_row(tag);

    return row ? row->cls : ROBO_HTML_ELEMENT_CLASS_GENERIC;
}

unsigned int robo_html_tag_flags(robo_html_tag_t tag)
{
    const tag_row_t *row = tag_row(tag);

    return row ? row->flags : ROBO_HTML_TAG_FLAG_NONE;
}

robo_html_tag_t robo_html_tag_from_name(robo_html_namespace_t ns,
                                        const char *local_name)
{
    size_t i;

    if (!local_name || local_name[0] == '\0') {
        return ROBO_HTML_TAG_UNKNOWN;
    }
    /*
     * Only the HTML vocabulary is recognised: an SVG <a> is not an HTML <a>,
     * so resolving it to ROBO_HTML_TAG_A would be a lie. Foreign content
     * resolves to UNKNOWN, which is the honest answer until a phase that
     * owns namespace switching says otherwise.
     */
    if (ns != ROBO_HTML_NAMESPACE_HTML) {
        return ROBO_HTML_TAG_UNKNOWN;
    }
    for (i = 0u; i < TAG_ROW_COUNT; i++) {
        /* ASCII case-insensitive in both directions, so a caller may pass
         * either case — the element model owns the one comparison rule. */
        if (robo_html_name_equal(k_tags[i].name, local_name)) {
            return k_tags[i].tag;
        }
    }
    return ROBO_HTML_TAG_UNKNOWN;
}

size_t robo_html_tag_count(void)
{
    return TAG_ROW_COUNT;
}

robo_result_t robo_html_tag_at(size_t index, robo_html_tag_t *out)
{
    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (index >= TAG_ROW_COUNT) {
        *out = ROBO_HTML_TAG_UNKNOWN;
        return ROBO_NOT_FOUND;
    }
    *out = k_tags[index].tag;
    return ROBO_OK;
}

/* ------------------------------------------------------------------ */
/* Predicates                                                          */
/* ------------------------------------------------------------------ */

static int tag_has_flag(robo_html_tag_t tag, unsigned int flag)
{
    return (robo_html_tag_flags(tag) & flag) != 0u;
}

int robo_html_tag_is_void(robo_html_tag_t tag)
{
    return tag_has_flag(tag, ROBO_HTML_TAG_FLAG_VOID);
}

int robo_html_tag_is_root(robo_html_tag_t tag)
{
    return tag_has_flag(tag, ROBO_HTML_TAG_FLAG_ROOT);
}

int robo_html_tag_is_document_structure(robo_html_tag_t tag)
{
    return tag_has_flag(tag, ROBO_HTML_TAG_FLAG_DOCUMENT_STRUCTURE);
}

int robo_html_tag_is_metadata(robo_html_tag_t tag)
{
    return tag_has_flag(tag, ROBO_HTML_TAG_FLAG_METADATA);
}

int robo_html_tag_is_sectioning(robo_html_tag_t tag)
{
    return tag_has_flag(tag, ROBO_HTML_TAG_FLAG_SECTIONING);
}

int robo_html_tag_is_region(robo_html_tag_t tag)
{
    return tag_has_flag(tag, ROBO_HTML_TAG_FLAG_REGION);
}

int robo_html_tag_is_heading(robo_html_tag_t tag)
{
    return tag_has_flag(tag, ROBO_HTML_TAG_FLAG_HEADING);
}

int robo_html_tag_is_list(robo_html_tag_t tag)
{
    return tag_has_flag(tag, ROBO_HTML_TAG_FLAG_LIST);
}

int robo_html_tag_is_list_item(robo_html_tag_t tag)
{
    return tag_has_flag(tag, ROBO_HTML_TAG_FLAG_LIST_ITEM);
}

int robo_html_tag_is_text_level(robo_html_tag_t tag)
{
    return tag_has_flag(tag, ROBO_HTML_TAG_FLAG_TEXT_LEVEL);
}
