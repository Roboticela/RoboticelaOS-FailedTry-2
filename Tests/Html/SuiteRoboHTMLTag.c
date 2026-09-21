/*
 * RoboHTML Phase 8.11 element-tag suite (html.tag.*).
 *
 * The tag vocabulary is the one place that answers "what is this element?".
 * So the tests here are mostly about *integrity of that answer* rather than
 * about any single element: that the table and the enum agree, that every
 * name round-trips, that the role column is what the element model reports,
 * that the flags and the named predicates never disagree, and that an
 * unrecognised element is a supported state rather than a hole.
 *
 * The identity tests are deliberately end-to-end — parse real HTML, then ask
 * a handle what it is — because the point of the layer is that the answer a
 * *consumer* gets is the same one the table defines. A table that is correct
 * in isolation and is not consulted is worth nothing.
 */

#include "RoboBase/RoboBase.h"
#include "RoboHTML/RoboHTML.h"
#include "RoboHTML/RoboHTMLDocument.h"
#include "RoboHTML/RoboHTMLDom.h"
#include "RoboHTML/RoboHTMLElement.h"
#include "RoboHTML/RoboHTMLMutation.h"
#include "RoboHTML/RoboHTMLNode.h"
#include "RoboHTML/RoboHTMLSource.h"
#include "RoboHTML/RoboHTMLTag.h"
#include "RoboHTML/RoboHTMLText.h"
#include "RoboHTML/RoboHTMLTraversal.h"
#include "RoboHTML/RoboHTMLTreeBuilder.h"
#include "RoboTest.h"

#include <stddef.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Fixture                                                             */
/* ------------------------------------------------------------------ */

static void TgFixtureBegin(void)
{
    if (!BaseIsInitialized()) {
        RoboCheckEqInt(BaseInit(), RoboOk);
    }
    if (!HtmlIsInitialized()) {
        RoboCheckEqInt(HtmlInit(), RoboOk);
    }
}

static void TgFixtureEnd(void)
{
    HtmlShutdown();
}

static HtmlDocumentId TgDoc(void)
{
    HtmlDocumentId id = RoboHtmlDocumentIdNone;

    RoboCheckEqInt(HtmlDocumentCreate(&id, NULL), RoboOk);
    return id;
}

static void TgBuild(HtmlDocumentId id, const char *text)
{
    HtmlSource src;

    HtmlSourceInit(&src);
    RoboCheckEqInt(HtmlSourceString(&src, text, NULL), RoboOk);
    RoboCheckEqInt(HtmlBuildTree(id, &src, NULL, NULL), RoboOk);
}

static HtmlNodeHandle TgRoot(HtmlDocumentId id)
{
    HtmlNodeHandle root = HtmlDocumentRoot(id);

    RoboCheck(HtmlNodeHandleIsWellformed(&root));
    return root;
}

/* ------------------------------------------------------------------ */
/* Predicates                                                          */
/* ------------------------------------------------------------------ */

typedef struct tgTagQuery {
    HtmlTag tag;
} TgTagQuery;

/**
 * Visitor: every visited element's stored name and its identity must agree,
 * and every identity must be a defined value. Counts what it checked.
 */
static Result TgVisitIdentity(const HtmlNodeView *view,
                                       size_t depth,
                                       void *user)
{
    size_t *visited = (size_t *)user;
    HtmlTag tag = HtmlElementTag(&view->handle);

    (void)depth;
    RoboCheck(HtmlTagIsKnown(tag));
    RoboCheckEqInt((int)tag,
                      (int)HtmlTagFromName(RoboHtmlNamespaceHtml,
                                                   view->name));
    RoboCheck(HtmlElementIsTag(&view->handle, tag));
    (*visited)++;
    return RoboOk;
}

static int TgMatchTag(const HtmlNodeView *view, void *user)
{
    const TgTagQuery *q = (const TgTagQuery *)user;

    return HtmlElementIsTag(&view->handle, q->tag);
}

/** First element of @p tag at or below @p start; asserts it was found. */
static HtmlNodeHandle TgFind(const HtmlNodeHandle *start,
                                       HtmlTag tag)
{
    TgTagQuery q;
    HtmlNodeHandle out;

    q.tag = tag;
    HtmlNodeHandleClear(&out);
    RoboCheckEqInt(
        HtmlNodeFindFirst(start, RoboHtmlNodeFilterElements,
                                  TgMatchTag, &q, &out, NULL),
        RoboOk);
    RoboCheck(HtmlNodeHandleIsWellformed(&out));
    return out;
}

/** Whole subtree text of @p node into @p out (NUL-terminated). */
static void TgText(const HtmlNodeHandle *node, char *out, size_t cap)
{
    size_t length = 0u;

    out[0] = '\0';
    if (HtmlNodeSubtreeText(node, out, cap, &length, NULL) != RoboOk) {
        return;
    }
    if (length >= cap) {
        out[cap - 1u] = '\0';
    }
}

/* ------------------------------------------------------------------ */
/* The table is internally consistent                                  */
/* ------------------------------------------------------------------ */

/*
 * Every other assertion in this phase is built on the table being what it
 * claims: dense ids, rows in enum order, names that round-trip, roles that
 * are defined values. If this test is green, a failure elsewhere is about
 * behaviour; if it is red, nothing else here means anything.
 */
static void TestTgTableIsDense(void)
{
    size_t count = HtmlTagCount();
    size_t i;
    HtmlTag tag;

    TgFixtureBegin();

    RoboCheckEqInt((int)count, (int)(RoboHtmlTagCount - 1));

    for (i = 0u; i < count; i++) {
        RoboCheckEqInt(HtmlTagAt(i, &tag), RoboOk);
        /* Row order is enum order: the row at index i defines tag i + 1. */
        RoboCheckEqInt((int)tag, (int)(i + 1u));
        RoboCheck(HtmlTagIsKnown(tag));
        RoboCheck(HtmlTagName(tag)[0] != '\0');
        RoboCheck(HtmlElementClassIsKnown(HtmlTagClass(tag)));
        /* Name lookup returns the very tag the row defines. */
        RoboCheckEqInt(
            (int)HtmlTagFromName(RoboHtmlNamespaceHtml,
                                         HtmlTagName(tag)),
            (int)tag);
    }

    /* No name is used twice, and every name is lowercase. */
    for (i = 0u; i < count; i++) {
        HtmlTag a;
        size_t j;

        RoboCheckEqInt(HtmlTagAt(i, &a), RoboOk);
        for (j = 0u; j < count; j++) {
            HtmlTag b;
            const char *name;

            RoboCheckEqInt(HtmlTagAt(j, &b), RoboOk);
            name = HtmlTagName(b);
            if (i != j) {
                RoboCheck(strcmp(HtmlTagName(a), name) != 0);
            }
        }
        {
            const char *name = HtmlTagName(a);
            size_t k;

            for (k = 0u; name[k] != '\0'; k++) {
                RoboCheck(!(name[k] >= 'A' && name[k] <= 'Z'));
            }
        }
    }

    TgFixtureEnd();
}

static void TestTgUnknownAndUndefined(void)
{
    TgFixtureBegin();

    /* UNKNOWN is a defined answer with no name, role or flags. */
    RoboCheck(!HtmlTagIsKnown(RoboHtmlTagUnknown));
    RoboCheckStrEq(HtmlTagName(RoboHtmlTagUnknown), "");
    RoboCheckEqInt((int)HtmlTagClass(RoboHtmlTagUnknown),
                      (int)RoboHtmlElementClassGeneric);
    RoboCheckEqInt((int)HtmlTagFlags(RoboHtmlTagUnknown), 0);
    RoboCheck(!HtmlTagIsVoid(RoboHtmlTagUnknown));
    RoboCheck(!HtmlTagIsHeading(RoboHtmlTagUnknown));
    RoboCheck(!HtmlTagIsRoot(RoboHtmlTagUnknown));

    /* An undefined enumerator behaves exactly as UNKNOWN, never as a tag. */
    RoboCheck(!HtmlTagIsKnown((HtmlTag)(RoboHtmlTagCount)));
    RoboCheck(!HtmlTagIsKnown((HtmlTag)9999));
    RoboCheck(!HtmlTagIsKnown((HtmlTag)-1));
    RoboCheckStrEq(HtmlTagName((HtmlTag)9999), "");
    RoboCheckEqInt((int)HtmlTagClass((HtmlTag)9999),
                      (int)RoboHtmlElementClassGeneric);

    /* Out-of-range table access is NOT_FOUND, and clears the output. */
    {
        HtmlTag out = RoboHtmlTagDiv;

        RoboCheckEqInt(HtmlTagAt(HtmlTagCount(), &out),
                          RoboNotFound);
        RoboCheckEqInt((int)out, (int)RoboHtmlTagUnknown);
    }
    RoboCheckEqInt(HtmlTagAt(0u, NULL), RoboInvalidArgument);

    TgFixtureEnd();
}

static void TestTgLookupByName(void)
{
    TgFixtureBegin();

    RoboCheckEqInt((int)HtmlTagFromName(RoboHtmlNamespaceHtml,
                                                   "div"),
                      (int)RoboHtmlTagDiv);
    /* Case-insensitive in both directions: the element model owns the rule. */
    RoboCheckEqInt((int)HtmlTagFromName(RoboHtmlNamespaceHtml,
                                                   "DIV"),
                      (int)RoboHtmlTagDiv);
    RoboCheckEqInt((int)HtmlTagFromName(RoboHtmlNamespaceHtml,
                                                   "DiV"),
                      (int)RoboHtmlTagDiv);
    RoboCheckEqInt((int)HtmlTagFromName(RoboHtmlNamespaceHtml,
                                                   "Strong"),
                      (int)RoboHtmlTagStrong);

    /* Unrecognised names are UNKNOWN, never an error. */
    RoboCheckEqInt((int)HtmlTagFromName(RoboHtmlNamespaceHtml,
                                                   "my-widget"),
                      (int)RoboHtmlTagUnknown);
    RoboCheckEqInt((int)HtmlTagFromName(RoboHtmlNamespaceHtml,
                                                   "divx"),
                      (int)RoboHtmlTagUnknown);
    RoboCheckEqInt((int)HtmlTagFromName(RoboHtmlNamespaceHtml,
                                                   ""),
                      (int)RoboHtmlTagUnknown);
    RoboCheckEqInt((int)HtmlTagFromName(RoboHtmlNamespaceHtml,
                                                   NULL),
                      (int)RoboHtmlTagUnknown);

    /*
     * Identity is (namespace, name): the same spelling in another namespace
     * is a different element, and answering otherwise would be a lie.
     */
    RoboCheckEqInt((int)HtmlTagFromName(RoboHtmlNamespaceSvg,
                                                   "a"),
                      (int)RoboHtmlTagUnknown);
    RoboCheckEqInt((int)HtmlTagFromName(RoboHtmlNamespaceMathml,
                                                   "div"),
                      (int)RoboHtmlTagUnknown);
    RoboCheckEqInt((int)HtmlTagFromName(RoboHtmlNamespaceNone,
                                                   "div"),
                      (int)RoboHtmlTagUnknown);

    TgFixtureEnd();
}

static void TestTgNameAndRole(void)
{
    TgFixtureBegin();

    RoboCheckStrEq(HtmlTagName(RoboHtmlTagHtml), "html");
    RoboCheckStrEq(HtmlTagName(RoboHtmlTagHead), "head");
    RoboCheckStrEq(HtmlTagName(RoboHtmlTagBody), "body");
    RoboCheckStrEq(HtmlTagName(RoboHtmlTagTitle), "title");
    RoboCheckStrEq(HtmlTagName(RoboHtmlTagDiv), "div");
    RoboCheckStrEq(HtmlTagName(RoboHtmlTagSpan), "span");
    RoboCheckStrEq(HtmlTagName(RoboHtmlTagP), "p");
    RoboCheckStrEq(HtmlTagName(RoboHtmlTagBr), "br");
    RoboCheckStrEq(HtmlTagName(RoboHtmlTagH1), "h1");
    RoboCheckStrEq(HtmlTagName(RoboHtmlTagH6), "h6");
    RoboCheckStrEq(HtmlTagName(RoboHtmlTagUl), "ul");
    RoboCheckStrEq(HtmlTagName(RoboHtmlTagOl), "ol");
    RoboCheckStrEq(HtmlTagName(RoboHtmlTagLi), "li");
    RoboCheckStrEq(HtmlTagName(RoboHtmlTagStrong), "strong");
    RoboCheckStrEq(HtmlTagName(RoboHtmlTagEm), "em");
    RoboCheckStrEq(HtmlTagName(RoboHtmlTagCode), "code");
    RoboCheckStrEq(HtmlTagName(RoboHtmlTagPre), "pre");
    RoboCheckStrEq(HtmlTagName(RoboHtmlTagSection), "section");
    RoboCheckStrEq(HtmlTagName(RoboHtmlTagArticle), "article");
    RoboCheckStrEq(HtmlTagName(RoboHtmlTagNav), "nav");
    RoboCheckStrEq(HtmlTagName(RoboHtmlTagMain), "main");
    RoboCheckStrEq(HtmlTagName(RoboHtmlTagHeader), "header");
    RoboCheckStrEq(HtmlTagName(RoboHtmlTagFooter), "footer");
    RoboCheckStrEq(HtmlTagName(RoboHtmlTagAside), "aside");
    /* Names that need the table to be right about their spelling. */
    RoboCheckStrEq(HtmlTagName(RoboHtmlTagBlockquote),
                      "blockquote");
    RoboCheckStrEq(HtmlTagName(RoboHtmlTagFigcaption),
                      "figcaption");
    RoboCheckStrEq(HtmlTagName(RoboHtmlTagBasefont), "basefont");

    /* Roles, including the two that look alike but are not. */
    RoboCheckEqInt((int)HtmlTagClass(RoboHtmlTagHtml),
                      (int)RoboHtmlElementClassRoot);
    RoboCheckEqInt((int)HtmlTagClass(RoboHtmlTagHead),
                      (int)RoboHtmlElementClassMetadata);
    RoboCheckEqInt((int)HtmlTagClass(RoboHtmlTagTitle),
                      (int)RoboHtmlElementClassMetadata);
    RoboCheckEqInt((int)HtmlTagClass(RoboHtmlTagBody),
                      (int)RoboHtmlElementClassSection);
    RoboCheckEqInt((int)HtmlTagClass(RoboHtmlTagDiv),
                      (int)RoboHtmlElementClassSection);
    RoboCheckEqInt((int)HtmlTagClass(RoboHtmlTagP),
                      (int)RoboHtmlElementClassParagraph);
    RoboCheckEqInt((int)HtmlTagClass(RoboHtmlTagH3),
                      (int)RoboHtmlElementClassHeading);
    RoboCheckEqInt((int)HtmlTagClass(RoboHtmlTagSpan),
                      (int)RoboHtmlElementClassText);
    RoboCheckEqInt((int)HtmlTagClass(RoboHtmlTagBr),
                      (int)RoboHtmlElementClassText);
    RoboCheckEqInt((int)HtmlTagClass(RoboHtmlTagImg),
                      (int)RoboHtmlElementClassReplaced);
    RoboCheckEqInt((int)HtmlTagClass(RoboHtmlTagInput),
                      (int)RoboHtmlElementClassControl);
    /* A recognised tag whose role this runtime does not classify yet. */
    RoboCheckEqInt((int)HtmlTagClass(RoboHtmlTagCol),
                      (int)RoboHtmlElementClassGeneric);

    TgFixtureEnd();
}

/*
 * The element model's classify() and the tag vocabulary are the same answer.
 * This is the assertion that keeps Phase 8.5's role question from drifting
 * away from Phase 8.11's identity answer — the whole reason the old name
 * table was deleted rather than kept alongside.
 */
static void TestTgClassifyAgreesWithVocabulary(void)
{
    size_t count = HtmlTagCount();
    size_t i;

    TgFixtureBegin();

    for (i = 0u; i < count; i++) {
        HtmlTag tag;
        const char *name;

        RoboCheckEqInt(HtmlTagAt(i, &tag), RoboOk);
        name = HtmlTagName(tag);
        RoboCheckEqInt(
            (int)HtmlElementClassify(RoboHtmlNamespaceHtml, name),
            (int)HtmlTagClass(tag));
        /* The uppercase spelling must not change the role. */
        {
            char upper[RoboHtmlNameMax + 1u];
            size_t k;

            for (k = 0u; name[k] != '\0' && k < RoboHtmlNameMax; k++) {
                upper[k] = (name[k] >= 'a' && name[k] <= 'z')
                               ? (char)(name[k] - 'a' + 'A')
                               : name[k];
            }
            upper[k] = '\0';
            RoboCheckEqInt(
                (int)HtmlElementClassify(RoboHtmlNamespaceHtml,
                                                upper),
                (int)HtmlTagClass(tag));
        }
    }

    /* A name outside the vocabulary is GENERIC in both models. */
    RoboCheckEqInt(
        (int)HtmlElementClassify(RoboHtmlNamespaceHtml, "my-widget"),
        (int)HtmlTagClass(RoboHtmlTagUnknown));
    RoboCheckEqInt(
        (int)HtmlElementClassify(RoboHtmlNamespaceSvg, "a"),
        (int)RoboHtmlElementClassGeneric);

    TgFixtureEnd();
}

/* ------------------------------------------------------------------ */
/* Flags and predicates                                                */
/* ------------------------------------------------------------------ */

static void TestTgVoid(void)
{
    static const HtmlTag kVoid[] = {
        RoboHtmlTagBr,    RoboHtmlTagHr,    RoboHtmlTagImg,
        RoboHtmlTagInput, RoboHtmlTagMeta,  RoboHtmlTagLink,
        RoboHtmlTagWbr,   RoboHtmlTagArea,  RoboHtmlTagBase,
        RoboHtmlTagCol,   RoboHtmlTagEmbed, RoboHtmlTagFrame,
        RoboHtmlTagKeygen,
    };
    size_t i;

    TgFixtureBegin();

    for (i = 0u; i < sizeof(kVoid) / sizeof(kVoid[0]); i++) {
        RoboCheck(HtmlTagIsVoid(kVoid[i]));
    }
    /* The containers a document is built from are not void. */
    RoboCheck(!HtmlTagIsVoid(RoboHtmlTagDiv));
    RoboCheck(!HtmlTagIsVoid(RoboHtmlTagSpan));
    RoboCheck(!HtmlTagIsVoid(RoboHtmlTagP));
    RoboCheck(!HtmlTagIsVoid(RoboHtmlTagBody));
    RoboCheck(!HtmlTagIsVoid(RoboHtmlTagHtml));
    RoboCheck(!HtmlTagIsVoid(RoboHtmlTagUnknown));
    /* <br> is the one that matters to the parser: void *and* text-level. */
    RoboCheck(HtmlTagIsTextLevel(RoboHtmlTagBr));

    TgFixtureEnd();
}

static void TestTgHeadings(void)
{
    static const HtmlTag kHeadings[] = {
        RoboHtmlTagH1, RoboHtmlTagH2, RoboHtmlTagH3,
        RoboHtmlTagH4, RoboHtmlTagH5, RoboHtmlTagH6,
    };
    size_t i;

    TgFixtureBegin();

    for (i = 0u; i < sizeof(kHeadings) / sizeof(kHeadings[0]); i++) {
        RoboCheck(HtmlTagIsHeading(kHeadings[i]));
        RoboCheckEqInt((int)HtmlTagClass(kHeadings[i]),
                          (int)RoboHtmlElementClassHeading);
    }
    RoboCheck(!HtmlTagIsHeading(RoboHtmlTagDiv));
    RoboCheck(!HtmlTagIsHeading(RoboHtmlTagHgroup));
    RoboCheck(!HtmlTagIsHeading(RoboHtmlTagHead));
    RoboCheck(!HtmlTagIsHeading(RoboHtmlTagUnknown));
    /* h7 and h0 are not elements. */
    RoboCheckEqInt(
        (int)HtmlTagFromName(RoboHtmlNamespaceHtml, "h7"),
        (int)RoboHtmlTagUnknown);
    RoboCheckEqInt(
        (int)HtmlTagFromName(RoboHtmlNamespaceHtml, "h0"),
        (int)RoboHtmlTagUnknown);

    TgFixtureEnd();
}

static void TestTgLists(void)
{
    TgFixtureBegin();

    RoboCheck(HtmlTagIsList(RoboHtmlTagUl));
    RoboCheck(HtmlTagIsList(RoboHtmlTagOl));
    RoboCheck(!HtmlTagIsList(RoboHtmlTagLi));
    RoboCheck(!HtmlTagIsList(RoboHtmlTagMenu));
    RoboCheck(!HtmlTagIsList(RoboHtmlTagDl));

    RoboCheck(HtmlTagIsListItem(RoboHtmlTagLi));
    RoboCheck(!HtmlTagIsListItem(RoboHtmlTagUl));
    RoboCheck(!HtmlTagIsListItem(RoboHtmlTagUnknown));

    TgFixtureEnd();
}

static void TestTgDocumentStructureAndMetadata(void)
{
    TgFixtureBegin();

    RoboCheck(HtmlTagIsRoot(RoboHtmlTagHtml));
    RoboCheck(!HtmlTagIsRoot(RoboHtmlTagBody));
    RoboCheck(!HtmlTagIsRoot(RoboHtmlTagUnknown));

    RoboCheck(HtmlTagIsDocumentStructure(RoboHtmlTagHtml));
    RoboCheck(HtmlTagIsDocumentStructure(RoboHtmlTagHead));
    RoboCheck(HtmlTagIsDocumentStructure(RoboHtmlTagBody));
    RoboCheck(!HtmlTagIsDocumentStructure(RoboHtmlTagTitle));
    RoboCheck(!HtmlTagIsDocumentStructure(RoboHtmlTagDiv));

    RoboCheck(HtmlTagIsMetadata(RoboHtmlTagHead));
    RoboCheck(HtmlTagIsMetadata(RoboHtmlTagTitle));
    RoboCheck(HtmlTagIsMetadata(RoboHtmlTagMeta));
    RoboCheck(HtmlTagIsMetadata(RoboHtmlTagLink));
    RoboCheck(HtmlTagIsMetadata(RoboHtmlTagStyle));
    RoboCheck(HtmlTagIsMetadata(RoboHtmlTagScript));
    RoboCheck(HtmlTagIsMetadata(RoboHtmlTagNoscript));
    RoboCheck(HtmlTagIsMetadata(RoboHtmlTagTemplate));
    RoboCheck(HtmlTagIsMetadata(RoboHtmlTagBase));
    RoboCheck(!HtmlTagIsMetadata(RoboHtmlTagBody));
    RoboCheck(!HtmlTagIsMetadata(RoboHtmlTagDiv));

    /* <head> is both document structure and metadata; <html> is not metadata. */
    RoboCheck(HtmlTagIsMetadata(RoboHtmlTagHead) &&
               HtmlTagIsDocumentStructure(RoboHtmlTagHead));
    RoboCheck(!HtmlTagIsMetadata(RoboHtmlTagHtml));

    TgFixtureEnd();
}

static void TestTgSectioningAndRegions(void)
{
    TgFixtureBegin();

    /* Sectioning content. */
    RoboCheck(HtmlTagIsSectioning(RoboHtmlTagSection));
    RoboCheck(HtmlTagIsSectioning(RoboHtmlTagArticle));
    RoboCheck(HtmlTagIsSectioning(RoboHtmlTagNav));
    RoboCheck(HtmlTagIsSectioning(RoboHtmlTagAside));
    RoboCheck(!HtmlTagIsSectioning(RoboHtmlTagMain));
    RoboCheck(!HtmlTagIsSectioning(RoboHtmlTagHeader));
    RoboCheck(!HtmlTagIsSectioning(RoboHtmlTagFooter));
    RoboCheck(!HtmlTagIsSectioning(RoboHtmlTagDiv));

    /* Page-level regions are a different question with a different answer. */
    RoboCheck(HtmlTagIsRegion(RoboHtmlTagMain));
    RoboCheck(HtmlTagIsRegion(RoboHtmlTagHeader));
    RoboCheck(HtmlTagIsRegion(RoboHtmlTagFooter));
    RoboCheck(!HtmlTagIsRegion(RoboHtmlTagSection));
    RoboCheck(!HtmlTagIsRegion(RoboHtmlTagArticle));
    RoboCheck(!HtmlTagIsRegion(RoboHtmlTagDiv));
    RoboCheck(!HtmlTagIsRegion(RoboHtmlTagUnknown));

    TgFixtureEnd();
}

static void TestTgTextLevel(void)
{
    static const HtmlTag kInline[] = {
        RoboHtmlTagSpan, RoboHtmlTagStrong, RoboHtmlTagEm,
        RoboHtmlTagB,    RoboHtmlTagI,      RoboHtmlTagU,
        RoboHtmlTagSmall, RoboHtmlTagCode,  RoboHtmlTagBr,
        RoboHtmlTagA,    RoboHtmlTagAbbr,   RoboHtmlTagWbr,
    };
    size_t i;

    TgFixtureBegin();

    for (i = 0u; i < sizeof(kInline) / sizeof(kInline[0]); i++) {
        RoboCheck(HtmlTagIsTextLevel(kInline[i]));
    }
    /* Block containers are not text-level, even the small ones. */
    RoboCheck(!HtmlTagIsTextLevel(RoboHtmlTagDiv));
    RoboCheck(!HtmlTagIsTextLevel(RoboHtmlTagP));
    RoboCheck(!HtmlTagIsTextLevel(RoboHtmlTagPre));
    RoboCheck(!HtmlTagIsTextLevel(RoboHtmlTagUl));
    RoboCheck(!HtmlTagIsTextLevel(RoboHtmlTagH1));
    RoboCheck(!HtmlTagIsTextLevel(RoboHtmlTagUnknown));

    TgFixtureEnd();
}

static void TestTgPredicatesMatchTheMask(void)
{
    size_t count = HtmlTagCount();
    size_t i;

    TgFixtureBegin();

    for (i = 0u; i < count; i++) {
        HtmlTag tag;
        unsigned int flags;

        RoboCheckEqInt(HtmlTagAt(i, &tag), RoboOk);
        flags = HtmlTagFlags(tag);
        RoboCheck(((flags & RoboHtmlTagFlagVoid) != 0u) ==
                   HtmlTagIsVoid(tag));
        RoboCheck(((flags & RoboHtmlTagFlagRoot) != 0u) ==
                   HtmlTagIsRoot(tag));
        RoboCheck(((flags & RoboHtmlTagFlagDocumentStructure) != 0u) ==
                   HtmlTagIsDocumentStructure(tag));
        RoboCheck(((flags & RoboHtmlTagFlagMetadata) != 0u) ==
                   HtmlTagIsMetadata(tag));
        RoboCheck(((flags & RoboHtmlTagFlagSectioning) != 0u) ==
                   HtmlTagIsSectioning(tag));
        RoboCheck(((flags & RoboHtmlTagFlagRegion) != 0u) ==
                   HtmlTagIsRegion(tag));
        RoboCheck(((flags & RoboHtmlTagFlagHeading) != 0u) ==
                   HtmlTagIsHeading(tag));
        RoboCheck(((flags & RoboHtmlTagFlagList) != 0u) ==
                   HtmlTagIsList(tag));
        RoboCheck(((flags & RoboHtmlTagFlagListItem) != 0u) ==
                   HtmlTagIsListItem(tag));
        RoboCheck(((flags & RoboHtmlTagFlagTextLevel) != 0u) ==
                   HtmlTagIsTextLevel(tag));
        /* Only one root exists, and it is <html>. */
        if (HtmlTagIsRoot(tag)) {
            RoboCheckEqInt((int)tag, (int)RoboHtmlTagHtml);
        }
    }

    TgFixtureEnd();
}

/* ------------------------------------------------------------------ */
/* Identity through the parser                                         */
/* ------------------------------------------------------------------ */

static void TestTgParserIdentity(void)
{
    HtmlDocumentId id;
    HtmlNodeHandle root;
    HtmlNodeHandle el;
    size_t visited = 0u;

    TgFixtureBegin();
    id = TgDoc();
    TgBuild(id, "<!DOCTYPE html><html><head><title>T</title></head>"
                 "<body><div id=\"main\"><h1>Hi</h1><p>Text</p>"
                 "<span>x</span><br><ul><li>one</li></ul></div></body></html>");
    root = TgRoot(id);

    /* The document element. */
    el = TgFind(&root, RoboHtmlTagHtml);
    RoboCheckEqInt((int)HtmlElementTag(&el),
                      (int)RoboHtmlTagHtml);
    RoboCheck(HtmlElementIsTag(&el, RoboHtmlTagHtml));
    RoboCheck(HtmlTagIsRoot(HtmlElementTag(&el)));
    /* The name is still the name, and it is lowercase. */
    RoboCheckStrEq(HtmlElementName(&el, NULL), "html");

    /* A nested element is found by identity, not by path. */
    el = TgFind(&el, RoboHtmlTagLi);
    RoboCheck(HtmlElementIsTag(&el, RoboHtmlTagLi));
    RoboCheck(HtmlTagIsListItem(HtmlElementTag(&el)));

    /* Referring to the wrong tag is false, not a near miss. */
    RoboCheck(!HtmlElementIsTag(&el, RoboHtmlTagUl));
    RoboCheck(!HtmlElementIsTag(&el, RoboHtmlTagDiv));

    /*
     * Every identity in this document agrees with the name it stores — the
     * check that makes "identity is derived from (namespace, name)" a fact
     * rather than an assumption. A visitor is the right shape because it
     * hands over a fresh view per node, so nothing is ever asked to be both
     * the input and the output of a resolution.
     */
    RoboCheckEqInt(HtmlNodeWalkDescendants(
                          &root, RoboHtmlNodeFilterElements, TgVisitIdentity,
                          &visited, NULL),
                      RoboOk);
    RoboCheck(visited >= 8u);

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    TgFixtureEnd();
}

static void TestTgUppercaseSourceKeepsIdentity(void)
{
    HtmlDocumentId id;
    HtmlNodeHandle root;
    HtmlElementView view;
    HtmlNodeHandle div;
    const char *value;

    TgFixtureBegin();
    id = TgDoc();
    /* HTML tag names are case-insensitive, and stored lowercase. */
    TgBuild(id, "<DIV CLASS=\"Panel\">Hi</DIV>");
    root = TgRoot(id);

    div = TgFind(&root, RoboHtmlTagDiv);
    RoboCheckStrEq(HtmlElementName(&div, NULL), "div");
    RoboCheckEqInt(HtmlElementViewGet(&div, &view, NULL), RoboOk);
    RoboCheckEqInt((int)view.tag, (int)RoboHtmlTagDiv);
    RoboCheckStrEq(view.name, "div");
    RoboCheckEqInt((int)view.classification,
                      (int)RoboHtmlElementClassSection);
    /* Attribute names are stored lowercase too; the value is untouched. */
    RoboCheck(HtmlElementHasAttribute(&div, "class"));
    value = HtmlElementAttributeValue(&div, "class", NULL);
    RoboCheck(value != NULL);
    if (value) {
        RoboCheckStrEq(value, "Panel");
    }

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    TgFixtureEnd();
}

static void TestTgCustomElementHasNoIdentity(void)
{
    HtmlDocumentId id;
    HtmlNodeHandle root;
    HtmlNodeHandle custom;
    HtmlElementView view;
    char text[64];

    TgFixtureBegin();
    id = TgDoc();
    TgBuild(id, "<body><my-widget data-x=\"1\">hello</my-widget></body>");
    root = TgRoot(id);

    custom = TgFind(&root, RoboHtmlTagUnknown);
    /* The name survives, because an unknown type is not a lost name. */
    RoboCheckStrEq(HtmlElementName(&custom, NULL), "my-widget");
    RoboCheckEqInt((int)HtmlElementTag(&custom),
                      (int)RoboHtmlTagUnknown);
    RoboCheckEqInt(HtmlElementViewGet(&custom, &view, NULL),
                      RoboOk);
    RoboCheckEqInt((int)view.tag, (int)RoboHtmlTagUnknown);
    RoboCheckEqInt((int)view.classification,
                      (int)RoboHtmlElementClassGeneric);

    /* It is a real element: attributes, children and text all work. */
    {
        const char *value =
            HtmlElementAttributeValue(&custom, "data-x", NULL);

        RoboCheck(value != NULL);
        if (value) {
            RoboCheckStrEq(value, "1");
        }
    }
    TgText(&custom, text, sizeof(text));
    RoboCheckStrEq(text, "hello");

    /*
     * UNKNOWN is answerable — "this element has no recognised identity" —
     * and it is the absence of identity, not a shared one: asking whether it
     * is a div is false, and asking whether it is unknown is true.
     */
    RoboCheck(HtmlElementIsTag(&custom, RoboHtmlTagUnknown));
    RoboCheck(!HtmlElementIsTag(&custom, RoboHtmlTagDiv));
    RoboCheck(!HtmlElementIsTag(&custom, RoboHtmlTagBody));

    /* A body element in the same document is not "unknown". */
    {
        HtmlNodeHandle body = TgFind(&root, RoboHtmlTagBody);

        RoboCheck(!HtmlElementIsTag(&body, RoboHtmlTagUnknown));
        RoboCheck(HtmlElementIsTag(&body, RoboHtmlTagBody));
    }

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    TgFixtureEnd();
}

static void TestTgNonElementHandlesHaveNoIdentity(void)
{
    HtmlDocumentId id;
    HtmlNodeHandle root;
    HtmlNodeHandle text;
    HtmlNodeHandle stale;

    TgFixtureBegin();
    id = TgDoc();
    TgBuild(id, "<p>Hello</p>");
    root = TgRoot(id);

    RoboCheckEqInt(HtmlNodeFindFirst(
                          &root, RoboHtmlNodeFilterText, NULL, NULL, &text,
                          NULL),
                      RoboOk);
    RoboCheck(HtmlNodeHandleIsWellformed(&text));

    /*
     * A text node has no tag, and — the point of the test — it does not
     * satisfy UNKNOWN either. "No recognised identity" is a statement about
     * an element, so it cannot be true of something that is not one.
     */
    RoboCheckEqInt((int)HtmlElementTag(&text),
                      (int)RoboHtmlTagUnknown);
    RoboCheck(!HtmlElementIsTag(&text, RoboHtmlTagUnknown));
    RoboCheck(!HtmlElementIsTag(&text, RoboHtmlTagP));

    /* The document node is not an element either. */
    RoboCheck(!HtmlElementIsTag(&root, RoboHtmlTagUnknown));
    RoboCheck(!HtmlElementIsTag(&root, RoboHtmlTagHtml));
    RoboCheck(!HtmlElementIsTag(&root, RoboHtmlTagBody));

    /* NULL and a stale handle: no crash, no tag. */
    RoboCheckEqInt((int)HtmlElementTag(NULL),
                      (int)RoboHtmlTagUnknown);
    RoboCheck(!HtmlElementIsTag(NULL, RoboHtmlTagDiv));
    RoboCheck(!HtmlElementIsTag(NULL, RoboHtmlTagUnknown));

    stale = TgFind(&root, RoboHtmlTagP);
    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    /* The document is gone, so the handle resolves to nothing. */
    RoboCheckEqInt((int)HtmlElementTag(&stale),
                      (int)RoboHtmlTagUnknown);
    RoboCheck(!HtmlElementIsTag(&stale, RoboHtmlTagP));
    RoboCheck(!HtmlElementIsTag(&stale, RoboHtmlTagUnknown));

    TgFixtureEnd();
}

static void TestTgUndefinedTagQueryIsNotUnknown(void)
{
    HtmlDocumentId id;
    HtmlNodeHandle root;
    HtmlNodeHandle custom;
    HtmlNodeHandle div;

    TgFixtureBegin();
    id = TgDoc();
    TgBuild(id, "<div><my-widget>w</my-widget></div>");
    root = TgRoot(id);
    div = TgFind(&root, RoboHtmlTagDiv);
    custom = TgFind(&root, RoboHtmlTagUnknown);

    /*
     * A bogus enumerator is a bug in the caller, so it must not behave like
     * "no identity" — otherwise a mistyped tag would silently match every
     * custom element in the document.
     */
    RoboCheck(!HtmlElementIsTag(&div, (HtmlTag)9999));
    RoboCheck(!HtmlElementIsTag(&custom, (HtmlTag)9999));
    RoboCheck(!HtmlElementIsTag(&div,
                                         (HtmlTag)RoboHtmlTagCount));

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    TgFixtureEnd();
}

static void TestTgIdentitySurvivesMutation(void)
{
    HtmlDocumentId id;
    HtmlNodeHandle root;
    HtmlNodeHandle div;
    HtmlNodeHandle heading;
    HtmlNodeHandle moved;

    TgFixtureBegin();
    id = TgDoc();
    TgBuild(id, "<body><div><h1>Hi</h1></div></body>");
    root = TgRoot(id);
    div = TgFind(&root, RoboHtmlTagDiv);
    heading = TgFind(&root, RoboHtmlTagH1);

    /* Detach the heading, then put it back under a new parent: a moved
     * element keeps its identity, because identity is its name — and the
     * kind of element it is does not depend on where it sits. */
    RoboCheckEqInt(HtmlNodeRemoveChild(&div, &heading, NULL),
                      RoboOk);
    RoboCheckEqInt((int)HtmlElementTag(&heading),
                      (int)RoboHtmlTagH1);
    RoboCheck(HtmlElementIsTag(&heading, RoboHtmlTagH1));
    RoboCheck(HtmlTagIsHeading(HtmlElementTag(&heading)));

    {
        HtmlNodeHandle body = TgFind(&root, RoboHtmlTagBody);

        RoboCheckEqInt(HtmlNodeAppendChild(&body, &heading, NULL),
                          RoboOk);
        moved = TgFind(&body, RoboHtmlTagH1);
        RoboCheck(HtmlNodeHandleEqual(&moved, &heading));
    }
    RoboCheckEqInt((int)HtmlElementTag(&heading),
                      (int)RoboHtmlTagH1);

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    TgFixtureEnd();
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

void TestRegisterHtmlTag(void)
{
    TestRegister("html.tag.table_is_dense", TestTgTableIsDense);
    TestRegister("html.tag.unknown_and_undefined",
                       TestTgUnknownAndUndefined);
    TestRegister("html.tag.lookup_by_name", TestTgLookupByName);
    TestRegister("html.tag.name_and_role", TestTgNameAndRole);
    TestRegister("html.tag.classify_agrees_with_vocabulary",
                       TestTgClassifyAgreesWithVocabulary);
    TestRegister("html.tag.void", TestTgVoid);
    TestRegister("html.tag.headings", TestTgHeadings);
    TestRegister("html.tag.lists", TestTgLists);
    TestRegister("html.tag.document_structure_and_metadata",
                       TestTgDocumentStructureAndMetadata);
    TestRegister("html.tag.sectioning_and_regions",
                       TestTgSectioningAndRegions);
    TestRegister("html.tag.text_level", TestTgTextLevel);
    TestRegister("html.tag.predicates_match_the_mask",
                       TestTgPredicatesMatchTheMask);
    TestRegister("html.tag.parser_identity", TestTgParserIdentity);
    TestRegister("html.tag.uppercase_source_keeps_identity",
                       TestTgUppercaseSourceKeepsIdentity);
    TestRegister("html.tag.custom_element_has_no_identity",
                       TestTgCustomElementHasNoIdentity);
    TestRegister("html.tag.non_element_handles_have_no_identity",
                       TestTgNonElementHandlesHaveNoIdentity);
    TestRegister("html.tag.undefined_tag_query_is_not_unknown",
                       TestTgUndefinedTagQueryIsNotUnknown);
    TestRegister("html.tag.identity_survives_mutation",
                       TestTgIdentitySurvivesMutation);
}
