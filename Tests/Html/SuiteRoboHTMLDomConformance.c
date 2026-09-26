/*
 * RoboHTML Phase 17.11 DOM conformance suite (html.conformance.*).
 *
 * Every 17.x surface has its own suite proving its own behaviour against its
 * own contract. This suite is the *matrix*: it reads every capability table
 * field by field, so a stated absence cannot quietly become a feature and a
 * feature cannot quietly disappear; and it asserts the joins *between*
 * surfaces — the places where two phases must agree and neither would notice if
 * they stopped:
 *
 *   - the reference policy an anchor is held to and the policy an `<img>` is
 *     held to are the same one, or a scheme would be refused in a link and
 *     fetched in an image;
 *   - a form's collection rule is the DOM's (explicit `form` association, then
 *     the nearest ancestor), not a descendant walk;
 *   - a table's indexes are counted in document order across sections, and a
 *     detached row is not a row *of a table*;
 *   - a template's content is unreachable from the document root, and a
 *     document-scoped lookup misses it;
 *   - a custom element's callbacks follow the tree's shape, including a host
 *     that became connected with the element already inside it;
 *   - every capability gate answers PermissionDenied and never NotSupported,
 *     which is the difference between "not allowed" and "not implemented";
 *   - the whole-tree audit is clean after all of that, and building the same
 *     bytes twice produces the same dump byte for byte.
 *
 * It adds no surface of its own: every number it asserts is read off a door
 * that already existed.
 */

#include "RoboBase/RoboBase.h"
#include "RoboHTML/RoboHTML.h"
#include "RoboHTML/RoboHTMLAnchor.h"
#include "RoboHTML/RoboHTMLCustomElements.h"
#include "RoboHTML/RoboHTMLDocument.h"
#include "RoboHTML/RoboHTMLDom.h"
#include "RoboHTML/RoboHTMLDomQuery.h"
#include "RoboHTML/RoboHTMLElement.h"
#include "RoboHTML/RoboHTMLFormTree.h"
#include "RoboHTML/RoboHTMLImage.h"
#include "RoboHTML/RoboHTMLImageLoad.h"
#include "RoboHTML/RoboHTMLInput.h"
#include "RoboHTML/RoboHTMLMutation.h"
#include "RoboHTML/RoboHTMLNavigation.h"
#include "RoboHTML/RoboHTMLNode.h"
#include "RoboHTML/RoboHTMLSecurity.h"
#include "RoboHTML/RoboHTMLShadow.h"
#include "RoboHTML/RoboHTMLTable.h"
#include "RoboHTML/RoboHTMLTag.h"
#include "RoboHTML/RoboHTMLTemplate.h"
#include "RoboHTML/RoboHTMLTraversal.h"
#include "RoboHTML/RoboHTMLVersion.h"
#include "RoboTest.h"

#include "RoboHTMLDomTestSupport.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* 1. The version pin and the capability matrix                        */
/* ------------------------------------------------------------------ */

static void TestConformanceVersion(void)
{
    HtmlTemplateCapabilities tc;
    HtmlShadowCapabilities sc;
    HtmlCustomElementCapabilities cc;
    HtmlImageLoadCapabilities ic;
    HtmlSecurityPosture posture;

    RoboCheckEqInt(RoboHtmlVersionMajor, 0);
    RoboCheckEqInt(RoboHtmlVersionMinor, 16);
    RoboCheckEqInt(RoboHtmlVersionPatch, 0);
    RoboCheckStrEq(RoboHtmlVersionString, "0.16.0");
    RoboCheckStrEq(HtmlVersionString(), "0.16.0");

    /* Every record that carries a version carries *the* version: one string,
     * five readers, no sixth spelling. */
    memset(&tc, 0, sizeof(tc));
    memset(&sc, 0, sizeof(sc));
    memset(&cc, 0, sizeof(cc));
    memset(&ic, 0, sizeof(ic));
    memset(&posture, 0, sizeof(posture));
    RoboCheckEqInt(HtmlTemplateGetCapabilities(&tc), RoboOk);
    RoboCheckEqInt(HtmlShadowGetCapabilities(&sc), RoboOk);
    RoboCheckEqInt(HtmlCustomElementGetCapabilities(&cc), RoboOk);
    RoboCheckEqInt(HtmlImageLoadGetCapabilities(&ic), RoboOk);
    RoboCheckEqInt(HtmlSecurityGetPosture(&posture), RoboOk);
    RoboCheckStrEq(tc.version, RoboHtmlVersionString);
    RoboCheckStrEq(sc.version, RoboHtmlVersionString);
    RoboCheckStrEq(cc.version, RoboHtmlVersionString);
    RoboCheckStrEq(ic.version, RoboHtmlVersionString);
    RoboCheckStrEq(posture.version, RoboHtmlVersionString);
}

static void TestConformanceCapabilityMatrix(void)
{
    HtmlDomCompletionCapabilities dc;
    HtmlFormTreeCapabilities fc;
    HtmlInputCapabilities ic;
    HtmlTableCapabilities tc;
    HtmlNavigationCapabilities nc;
    HtmlTemplateCapabilities tpl;
    HtmlShadowCapabilities sh;
    HtmlCustomElementCapabilities ce;
    HtmlImageLoadCapabilities il;

    /* Pre-filled with 0xff, so a door that forgot to write a field reads as a
     * failure here instead of as stack garbage. */
    memset(&dc, 0xff, sizeof(dc));
    memset(&fc, 0xff, sizeof(fc));
    memset(&ic, 0xff, sizeof(ic));
    memset(&tc, 0xff, sizeof(tc));
    memset(&nc, 0xff, sizeof(nc));
    memset(&tpl, 0xff, sizeof(tpl));
    memset(&sh, 0xff, sizeof(sh));
    memset(&ce, 0xff, sizeof(ce));
    memset(&il, 0xff, sizeof(il));

    RoboCheckEqInt(HtmlDomCompletionGetCapabilities(&dc), RoboOk);
    RoboCheckEqInt(HtmlFormTreeGetCapabilities(&fc), RoboOk);
    RoboCheckEqInt(HtmlInputGetCapabilities(&ic), RoboOk);
    RoboCheckEqInt(HtmlTableGetCapabilities(&tc), RoboOk);
    RoboCheckEqInt(HtmlNavigationGetCapabilities(&nc), RoboOk);
    RoboCheckEqInt(HtmlTemplateGetCapabilities(&tpl), RoboOk);
    RoboCheckEqInt(HtmlShadowGetCapabilities(&sh), RoboOk);
    RoboCheckEqInt(HtmlCustomElementGetCapabilities(&ce), RoboOk);
    RoboCheckEqInt(HtmlImageLoadGetCapabilities(&il), RoboOk);

    /* Every recorded field is exactly 0 or 1 — no field is "truthy". */
    RoboCheckEqInt(dc.creation, 1);
    RoboCheckEqInt(dc.cloning, 1);
    RoboCheckEqInt(dc.textContent, 1);
    RoboCheckEqInt(dc.prepend, 1);
    RoboCheckEqInt(dc.lookupById, 1);
    RoboCheckEqInt(dc.lookupByTag, 1);
    RoboCheckEqInt(dc.lookupByClass, 1);
    RoboCheckEqInt(dc.selectorSeam, 1);
    /* ... and the three the brief names as absences are absences. */
    RoboCheckEqInt(dc.selectorEngine, 0);
    RoboCheckEqInt(dc.innerHtml, 0);

    RoboCheckEqInt(fc.association, 1);
    RoboCheckEqInt(fc.elements, 1);
    RoboCheckEqInt(fc.validation, 1);
    RoboCheckEqInt(fc.submit, 1);
    RoboCheckEqInt(fc.reset, 1);
    RoboCheckEqInt(fc.patternValidation, 0);
    RoboCheckEqInt(fc.submissionNetworking, 0);

    RoboCheckEqInt(ic.textEditing, 1);
    RoboCheckEqInt(ic.checkbox, 1);
    RoboCheckEqInt(ic.radioGroups, 1);
    RoboCheckEqInt(ic.stepping, 1);
    RoboCheckEqInt(ic.caretModel, 0);      /* a caret belongs to RoboText */
    RoboCheckEqInt(ic.eventDispatch, 0);   /* a change is a record, not an event */

    RoboCheckEqInt(tc.structure, 1);
    RoboCheckEqInt(tc.indexes, 1);
    RoboCheckEqInt(tc.spansRead, 1);
    RoboCheckEqInt(tc.spanLayout, 0);
    RoboCheckEqInt(tc.caption, 1);

    RoboCheckEqInt(nc.elementHref, 1);
    RoboCheckEqInt(nc.controller, 1);
    RoboCheckEqInt(nc.fragmentNavigation, 1);
    RoboCheckEqInt(nc.locationState, 1);
    RoboCheckEqInt(nc.documentLoad, 1);
    RoboCheckEqInt(nc.preventDefault, 0);
    RoboCheckEqInt(nc.networking, 0);

    RoboCheckEqInt(tpl.contentFragment, 1);
    RoboCheckEqInt(tpl.parserAdoption, 1);
    RoboCheckEqInt(tpl.inertContent, 1);
    RoboCheckEqInt(tpl.cloning, 1);
    RoboCheckEqInt(tpl.fragmentInsertion, 1);
    RoboCheckEqInt(tpl.setContent, 1);
    RoboCheckEqInt(tpl.inertnessQuery, 1);
    RoboCheckEqInt(tpl.insertionMode, 0);
    RoboCheckEqInt(tpl.declarativeShadowDom, 0);
    RoboCheckEqInt(tpl.scriptExecution, 0);

    RoboCheckEqInt(sh.attachShadow, 1);
    RoboCheckEqInt(sh.shadowRootQuery, 1);
    RoboCheckEqInt(sh.hostQuery, 1);
    RoboCheckEqInt(sh.lightDomPreserved, 1);
    RoboCheckEqInt(sh.boundaryLookup, 1);
    RoboCheckEqInt(sh.scopedLookup, 1);
    RoboCheckEqInt(sh.eventRetargeting, 1);
    RoboCheckEqInt(sh.focusBoundary, 1);
    RoboCheckEqInt(sh.reachabilityQuery, 1);
    RoboCheckEqInt(sh.closedMode, 0);
    RoboCheckEqInt(sh.slotting, 0);
    RoboCheckEqInt(sh.hostSelectors, 0);
    RoboCheckEqInt(sh.declarativeShadowDom, 0);
    RoboCheckEqInt(sh.secondRenderingTree, 0);
    RoboCheckEqInt(sh.hostCandidates, 0);

    RoboCheckEqInt(ce.registry, 1);
    RoboCheckEqInt(ce.nameValidation, 1);
    RoboCheckEqInt(ce.definedQuery, 1);
    RoboCheckEqInt(ce.definitionQuery, 1);
    RoboCheckEqInt(ce.createElement, 1);
    RoboCheckEqInt(ce.connectedCallback, 1);
    RoboCheckEqInt(ce.disconnectedCallback, 1);
    RoboCheckEqInt(ce.attributeChangedCallback, 1);
    RoboCheckEqInt(ce.upgradeOnDefine, 1);
    RoboCheckEqInt(ce.reachabilityDriven, 1);
    RoboCheckEqInt(ce.constructors, 0);
    RoboCheckEqInt(ce.reactionQueue, 0);
    RoboCheckEqInt(ce.customisedBuiltins, 0);
    RoboCheckEqInt(ce.scopedRegistries, 0);
    RoboCheckEqInt(ce.definedSelector, 0);
    RoboCheckEqInt(ce.callbackResults, 0);

    RoboCheckEqInt(il.fetchSeam, 1);
    RoboCheckEqInt(il.intrinsicSize, 1);
    RoboCheckEqInt(il.declaredSize, 1);
    RoboCheckEqInt(il.referencePolicy, 1);
    RoboCheckEqInt(il.layoutInvalidation, 1);
    RoboCheckEqInt(il.replacement, 1);
    RoboCheckEqInt(il.removalAbandon, 1);
    RoboCheckEqInt(il.srcset, 0);
    RoboCheckEqInt(il.responsiveImages, 0);
    RoboCheckEqInt(il.loadEvents, 0);
    RoboCheckEqInt(il.asyncLoading, 0);
    RoboCheckEqInt(il.networkAccess, 0);

    /* The security posture, which is the phase-17 batch's own statement. */
    {
        HtmlSecurityPosture posture;

        memset(&posture, 0xff, sizeof(posture));
        RoboCheckEqInt(HtmlSecurityGetPosture(&posture), RoboOk);
        RoboCheckEqInt(posture.filesystem, 0);
        RoboCheckEqInt(posture.network, 0);
        RoboCheckEqInt(posture.nativePointers, 0);
        RoboCheckEqInt(posture.processes, 0);
        RoboCheckEqInt(posture.hardware, 0);
        RoboCheckEqInt(posture.hostEnvironment, 0);
        RoboCheckEqInt(posture.scriptExecution, 0);
        RoboCheckEqInt(posture.eventListeners, 0);
        RoboCheckEqInt(posture.bytesOnlyThroughSeam, 1);
        RoboCheckEqInt(posture.hostOwnedDocument, 1);
    }
}

/* ------------------------------------------------------------------ */
/* 2. One reference policy, two callers                                */
/* ------------------------------------------------------------------ */

static void TestConformanceReferencePolicy(void)
{
    static const char *refused[] = {
        "javascript:alert(1)",
        "data:image/png;base64,AAAA",
        "file:///etc/passwd"
    };
    static const char *permitted[] = {
        "http://example.invalid/logo.png",
        "https://example.invalid/logo.png"
    };
    size_t i;

    /*
     * The two classifications are separate *views* that must agree about
     * permission. This is the join, and it is what a second scheme list would
     * break: a reference an anchor policy refuses must be a reference the image
     * path will not fetch, and the reverse.
     */
    for (i = 0u; i < sizeof(refused) / sizeof(refused[0]); i++) {
        HtmlNavigationTarget *target = NULL;
        HtmlImageReferenceClass class = RoboHtmlImageReferenceNone;
        size_t schemeLength = (size_t)(strchr(refused[i], ':') - refused[i]);

        RoboCheckEqInt(HtmlNavigationSchemeIsPermitted(refused[i],
                                                      schemeLength), 0);
        RoboCheckEqInt(HtmlImageReferenceClassify(refused[i],
                                                 strlen(refused[i]), "", 0u,
                                                 &class, NULL),
                       RoboOk);
        RoboCheckEqInt(class, RoboHtmlImageReferenceUnsupported);
        RoboCheckEqInt(HtmlNavigationTargetCreate(refused[i],
                                                 strlen(refused[i]),
                                                 "", 0u, &target, NULL),
                       RoboOk);
        /* The anchor admits the *shape* (an absolute URI) and refuses the
         * action; the image classification refuses the fetch. Both say no. */
        RoboCheckEqInt(HtmlNavigationTargetKind(target),
                       RoboHtmlTargetExternal);
        HtmlNavigationTargetDestroy(target);
    }

    for (i = 0u; i < sizeof(permitted) / sizeof(permitted[0]); i++) {
        HtmlNavigationTarget *target = NULL;
        HtmlImageReferenceClass class = RoboHtmlImageReferenceNone;
        size_t schemeLength = (size_t)(strchr(permitted[i], ':') - permitted[i]);

        RoboCheckEqInt(HtmlNavigationSchemeIsPermitted(permitted[i],
                                                      schemeLength), 1);
        RoboCheckEqInt(HtmlImageReferenceClassify(permitted[i],
                                                 strlen(permitted[i]), "", 0u,
                                                 &class, NULL),
                       RoboOk);
        RoboCheckEqInt(class, RoboHtmlImageReferenceExternal);
        RoboCheckEqInt(HtmlNavigationTargetCreate(permitted[i],
                                                 strlen(permitted[i]),
                                                 "", 0u, &target, NULL),
                       RoboOk);
        RoboCheckEqInt(HtmlNavigationTargetKind(target),
                       RoboHtmlTargetExternal);
        HtmlNavigationTargetDestroy(target);
    }

    /* A relative reference is internal to both, and a fragment names a place
     * rather than bytes — so it is INVALID as an image reference and not
     * "unsupported": the distinction a caller reports. */
    {
        HtmlNavigationTarget *target = NULL;
        HtmlImageReferenceClass class = RoboHtmlImageReferenceNone;

        RoboCheckEqInt(HtmlImageReferenceClassify("assets/logo.png", 15u, "",
                                                 0u, &class, NULL), RoboOk);
        RoboCheckEqInt(class, RoboHtmlImageReferenceLocal);
        RoboCheckEqInt(HtmlImageReferenceClassify("#section", 8u, "", 0u,
                                                 &class, NULL), RoboOk);
        RoboCheckEqInt(class, RoboHtmlImageReferenceInvalid);
        RoboCheckEqInt(HtmlNavigationTargetCreate("#section", 8u, "", 0u,
                                                 &target, NULL), RoboOk);
        RoboCheckEqInt(HtmlNavigationTargetKind(target),
                       RoboHtmlTargetFragment);
        HtmlNavigationTargetDestroy(target);
    }

    /* And the element door agrees with the standalone classifier, on a
     * document whose base is empty. */
    {
        HtmlDocumentId id = DomDocumentOf("<body><img src=\"file:///a.png\">"
                                         "<img src=\"logo.png\"></body>");
        HtmlNodeHandle root = DomRoot(id);
        HtmlNodeHandle img = DomFindTag(&root, RoboHtmlTagImg);
        HtmlImageReferenceClass first = RoboHtmlImageReferenceNone;
        HtmlImageReferenceClass second = RoboHtmlImageReferenceNone;
        HtmlNodeHandle next;

        RoboCheckEqInt(HtmlElementImageReferenceClass(&img, &first, NULL),
                       RoboOk);
        RoboCheckEqInt(first, RoboHtmlImageReferenceUnsupported);
        RoboCheckEqInt(HtmlNodeNextSibling(&img, &next, NULL), RoboOk);
        RoboCheckEqInt(HtmlElementImageReferenceClass(&next, &second, NULL),
                       RoboOk);
        RoboCheckEqInt(second, RoboHtmlImageReferenceLocal);
        RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    }
}

/* ------------------------------------------------------------------ */
/* 3. Form collection is the DOM's rule                                */
/* ------------------------------------------------------------------ */

typedef struct confFormCollection {
    const char *ids[8];
    size_t count;
} ConfFormCollection;

static Result ConfCollectVisitor(const HtmlNodeView *view, size_t depth,
                                 void *user)
{
    ConfFormCollection *out = (ConfFormCollection *)user;
    size_t length = 0u;
    const char *id;

    (void)depth;
    id = HtmlElementAttributeValue(&view->handle, "id", &length);
    if (out->count < 8u) {
        out->ids[out->count] = id;
        out->count++;
    }
    return RoboOk;
}

static void TestConformanceFormCollection(void)
{
    /*
     * The collection rule as a case: a descendant control, one nested deeper, a
     * control that names the form by `form=` and lives *outside* it, and a
     * control of another form. A descendant walk gets this wrong twice.
     */
    HtmlDocumentId id = DomDocumentOf(
        "<body>"
        "<form id=\"a\">"
        "<input id=\"inside\" name=\"i\">"
        "<div><input id=\"deep\" name=\"d\"></div>"
        "</form>"
        "<input id=\"outside\" form=\"a\" name=\"o\">"
        "<form id=\"b\"><input id=\"other\" name=\"x\"></form>"
        "</body>");
    HtmlNodeHandle root;
    HtmlNodeHandle body;
    HtmlNodeHandle formA;
    HtmlNodeHandle formB;
    ConfFormCollection collection;
    size_t count = 9u;

    root = DomRoot(id);
    body = DomFindTag(&root, RoboHtmlTagBody);
    formA = DomFindId(&root, "a");
    formB = DomFindId(&root, "b");

    memset(&collection, 0, sizeof(collection));
    RoboCheckEqInt(HtmlFormGetElements(&formA, ConfCollectVisitor, &collection,
                                       &count, NULL),
                   RoboOk);
    /* inside, deep, outside — in document order, and *not* `other`. */
    RoboCheckEqInt((int)count, 3);
    RoboCheckEqInt((int)collection.count, 3);
    RoboCheckStrEq(collection.ids[0], "inside");
    RoboCheckStrEq(collection.ids[1], "deep");
    RoboCheckStrEq(collection.ids[2], "outside");

    /* The count comes from the door's own out-param; the visitor is optional,
     * which is why a NULL one is the honest way to ask "how many". */
    count = 9u;
    RoboCheckEqInt(HtmlFormGetElements(&formB, NULL, NULL, &count, NULL),
                   RoboOk);
    RoboCheckEqInt((int)count, 1);

    /* Validation is a second door over the same collection. */
    {
        HtmlNodeHandle inside = DomFindId(&root, "inside");
        HtmlControlValidation control;
        HtmlFormValidation validation;

        memset(&control, 0, sizeof(control));
        RoboCheckEqInt(HtmlElementValidateControl(&inside, &control, NULL),
                       RoboOk);
        RoboCheckEqInt(control.validated, 1);
        RoboCheckEqInt(control.valid, 1);

        memset(&validation, 0, sizeof(validation));
        RoboCheckEqInt(HtmlFormValidate(&formA, &validation, NULL), RoboOk);
        RoboCheckEqInt((int)validation.controls, 3);
        RoboCheckEqInt(validation.valid, 1);
    }

    /* Moving the outside control into the form changes nothing about the
     * answer — the rule is evaluated on demand, not cached. (A node has one
     * parent here, so a move is a removal and an append: a graft of an
     * attached node is refused, which is the next assertion.) */
    {
        HtmlNodeHandle outside = DomFindId(&root, "outside");

        RoboCheckEqInt(HtmlNodeAppendChild(&formA, &outside, NULL),
                       RoboAlreadyExists);
        RoboCheckEqInt(HtmlNodeRemoveChild(&body, &outside, NULL), RoboOk);
        RoboCheckEqInt(HtmlNodeAppendChild(&formA, &outside, NULL), RoboOk);
        count = 9u;
        RoboCheckEqInt(HtmlFormGetElements(&formA, NULL, NULL, &count, NULL),
                       RoboOk);
        RoboCheckEqInt((int)count, 3);
    }

    DomExpectCleanTree(id);
    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
}

/* ------------------------------------------------------------------ */
/* 4. Table structure is the DOM's, not a layout grid                  */
/* ------------------------------------------------------------------ */

static void TestConformanceTableStructure(void)
{
    HtmlDocumentId id = DomDocumentOf(
        "<body><table id=\"t\">"
        "<caption>Cap</caption>"
        "<thead><tr><th>H</th></tr></thead>"
        "<tbody><tr><td>a</td><td colspan=\"2\">b</td></tr>"
        "<tr><td>c</td></tr></tbody>"
        "</table></body>");
    HtmlNodeHandle root = DomRoot(id);
    HtmlNodeHandle table;
    size_t count = 0u;
    size_t index = 9u;
    int span = 0;

    RoboCheckEqInt(HtmlNodeFindElement(&root, "table", &table, NULL),
                   RoboOk);
    /* Rows are counted across every section, in document order, and a header
     * row is a row. The *grid* is not built: `spanLayout` is 0. */
    RoboCheckEqInt(HtmlTableGetRows(&table, NULL, NULL, &count, NULL), RoboOk);
    RoboCheckEqInt((int)count, 3);
    count = 0u;
    RoboCheckEqInt(HtmlTableGetSections(&table, NULL, NULL, &count, NULL),
                   RoboOk);
    RoboCheckEqInt((int)count, 2);
    count = 0u;
    RoboCheckEqInt(HtmlTableGetBodies(&table, NULL, NULL, &count, NULL),
                   RoboOk);
    RoboCheckEqInt((int)count, 1);

    {
        /* There is no tag enumerator for table-structure elements — the tag
         * vocabulary is the *recognised* element set, and `caption` is not in
         * it — so the name search is the door. */
        HtmlNodeHandle caption;

        RoboCheckEqInt(HtmlNodeFindElement(&table, "caption", &caption,
                                           NULL),
                       RoboOk);
        RoboCheckStrEq(HtmlElementName(&caption, NULL), "caption");
    }

    /* The second body row: index 2 in document order, and its first cell's
     * index is 0 while `colspan` is read but not applied. */
    {
        HtmlNodeHandle body;
        HtmlNodeHandle row;
        HtmlNodeHandle cell;

        RoboCheckEqInt(HtmlNodeFindElement(&table, "tbody", &body, NULL),
                       RoboOk);
        RoboCheckEqInt(HtmlNodeChildAt(&body, 1u, &row, NULL), RoboOk);
        RoboCheckEqInt(HtmlTableRowIndex(&row, &index, NULL), RoboOk);
        RoboCheckEqInt((int)index, 2);
        RoboCheckEqInt(HtmlNodeChildAt(&row, 0u, &cell, NULL), RoboOk);
        RoboCheckEqInt(HtmlTableCellIndex(&cell, &index, NULL), RoboOk);
        RoboCheckEqInt((int)index, 0);

        RoboCheckEqInt(HtmlNodeChildAt(&body, 0u, &row, NULL), RoboOk);
        RoboCheckEqInt(HtmlNodeChildAt(&row, 1u, &cell, NULL), RoboOk);
        RoboCheckEqInt(HtmlTableCellColSpan(&cell, &span, NULL), RoboOk);
        RoboCheckEqInt(span, 2);
        RoboCheckEqInt(HtmlTableCellRowSpan(&cell, &span, NULL), RoboOk);
        RoboCheckEqInt(span, 1);

        /* A detached row is not a row *of a table*, which is the distinction
         * the index door is read through. */
        RoboCheckEqInt(HtmlNodeRemoveChild(&body, &row, NULL), RoboOk);
        RoboCheckEqInt(HtmlTableRowIndex(&row, &index, NULL), RoboNotFound);
    }

    DomExpectCleanTree(id);
    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
}

/* ------------------------------------------------------------------ */
/* 5. A template's content is unreachable from the document            */
/* ------------------------------------------------------------------ */

static void TestConformanceTemplateInertness(void)
{
    HtmlDocumentId id = DomDocumentOf(
        "<body><template id=\"tpl\"><p id=\"inert\">Inert</p></template>"
        "<p id=\"live\">Live</p></body>");
    HtmlNodeHandle root = DomRoot(id);
    HtmlNodeHandle template = DomFindId(&root, "tpl");
    HtmlNodeHandle content;
    HtmlNodeHandle clone;
    size_t count = 9u;
    int inert = 0;

    /* The parse adopted the children into the content fragment, so the
     * template has no light children. */
    RoboCheckEqInt(HtmlNodeChildCount(&template, &count, NULL), RoboOk);
    RoboCheckEqInt((int)count, 0);
    RoboCheckEqInt(HtmlElementTemplateIsInert(&template, &inert, NULL),
                   RoboOk);
    RoboCheckEqInt(inert, 1);
    RoboCheckEqInt(HtmlElementTemplateContent(&template, &content, NULL),
                   RoboOk);
    RoboCheck(HtmlNodeHandleIsWellformed(&content));
    RoboCheckEqInt(HtmlNodeChildCount(&content, &count, NULL), RoboOk);
    RoboCheckEqInt((int)count, 1);

    /* The document-scoped lookup misses the inert subtree and finds the live
     * one — the boundary as a query rather than as a flag. */
    {
        HtmlNodeHandle found;

        RoboCheckEqInt(HtmlDocumentGetElementById(id, "inert", &found, NULL),
                       RoboNotFound);
        RoboCheckEqInt(HtmlDocumentGetElementById(id, "live", &found, NULL),
                       RoboOk);
        RoboCheck(HtmlElementIsTag(&found, RoboHtmlTagP));
    }

    /* Cloning the content produces a *live* tree, and inserting the fragment's
     * children is how it becomes part of the document. */
    RoboCheckEqInt(HtmlElementTemplateCloneContent(&template, 1, &clone, NULL),
                   RoboOk);
    count = 9u;
    RoboCheckEqInt(HtmlNodeAppendFragmentChildren(&root, &clone, &count, NULL),
                   RoboOk);
    RoboCheckEqInt((int)count, 1);
    {
        HtmlNodeHandle found;

        RoboCheckEqInt(HtmlDocumentGetElementById(id, "inert", &found, NULL),
                       RoboOk);
        /* The template's own content is still inert: the clone is a second
         * tree, not a move. */
        RoboCheckEqInt(HtmlElementTemplateIsInert(&template, &inert, NULL),
                       RoboOk);
        RoboCheckEqInt(inert, 1);
    }

    /* Idempotence: a second adoption pass has nothing to adopt. */
    RoboCheckEqInt(HtmlDocumentAdoptTemplates(id, &count, NULL), RoboOk);
    RoboCheckEqInt((int)count, 0);

    DomExpectCleanTree(id);
    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
}

/* ------------------------------------------------------------------ */
/* 6. The shadow boundary                                              */
/* ------------------------------------------------------------------ */

static void TestConformanceShadowBoundary(void)
{
    HtmlDocumentId id = DomDocumentOf(
        "<body><div id=\"host\"><span id=\"light\">Light</span></div>"
        "<span id=\"outside\">Outside</span></body>");
    HtmlNodeHandle root = DomRoot(id);
    HtmlNodeHandle host = DomFindId(&root, "host");
    HtmlNodeHandle shadow;
    HtmlNodeHandle light;
    HtmlNodeHandle hostOut;
    size_t count = 9u;
    int inert = 0;

    RoboCheckEqInt(HtmlElementAttachShadow(&host, RoboHtmlShadowModeOpen,
                                          &shadow, NULL),
                   RoboOk);
    RoboCheck(HtmlNodeHandleIsWellformed(&shadow));

    /* The host's own children are untouched, and the host still answers as the
     * host of the root. */
    RoboCheckEqInt(HtmlNodeChildCount(&host, &count, NULL), RoboOk);
    RoboCheckEqInt((int)count, 1);
    RoboCheckEqInt(HtmlElementShadowRoot(&host, &light, NULL), RoboOk);
    RoboCheckEqInt(HtmlShadowRootHost(&shadow, &hostOut, NULL), RoboOk);
    RoboCheck(HtmlNodeHandleEqual(&hostOut, &host));

    /* A document-scoped lookup does not enter the shadow tree, and the host's
     * own light child is still reachable. */
    {
        HtmlNodeHandle found;
        HtmlNodeHandle inner;

        RoboCheckEqInt(HtmlDocumentGetElementById(id, "light", &found, NULL),
                       RoboOk);
        RoboCheckEqInt(HtmlDocumentCreateElement(id, "b", &inner, NULL),
                       RoboOk);
        RoboCheckEqInt(HtmlElementSetAttribute(&inner, "id", "inner", NULL),
                       RoboOk);
        RoboCheckEqInt(HtmlNodeAppendChild(&shadow, &inner, NULL), RoboOk);
        /* The new node is inside the shadow tree, so the document lookup
         * cannot see it... */
        RoboCheckEqInt(HtmlDocumentGetElementById(id, "inner", &found, NULL),
                       RoboNotFound);
        /* ... while a walk from the root finds it, because a shadow tree is
         * made of ordinary nodes. */
        RoboCheckEqInt(HtmlNodeFindElement(&shadow, "b", &found, NULL),
                       RoboOk);
        RoboCheckEqInt(HtmlNodeIsReachableFromDocument(&inner, &inert, NULL),
                       RoboOk);
        RoboCheckEqInt(inert, 0);
    }

    /* `closed` is refused rather than accepted-and-ignored, and the vocabulary
     * says which. */
    RoboCheckEqInt(HtmlShadowModeIsKnown(RoboHtmlShadowModeOpen), 1);
    RoboCheckEqInt(HtmlShadowModeIsKnown((HtmlShadowMode)2), 0);
    {
        HtmlNodeHandle second;

        RoboCheckEqInt(HtmlElementAttachShadow(&host, (HtmlShadowMode)2,
                                              &second, NULL),
                       RoboNotSupported);
        /* A second open root on the same host is refused, not replaced. */
        RoboCheckEqInt(HtmlElementAttachShadow(&host, RoboHtmlShadowModeOpen,
                                              &second, NULL),
                       RoboAlreadyExists);
    }

    DomExpectCleanTree(id);
    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
}

/* ------------------------------------------------------------------ */
/* 7. The custom-element lifecycle follows the tree                    */
/* ------------------------------------------------------------------ */

typedef struct confLife {
    int order[16];
    int count;
} ConfLife;

static void ConfLifePush(ConfLife *life, int which)
{
    if (life->count < 16) {
        life->order[life->count] = which;
        life->count++;
    }
}

static Result ConfOnConnected(const HtmlNodeHandle *element, void *userdata,
                              HtmlDiag *diag)
{
    (void)element;
    (void)diag;
    ConfLifePush((ConfLife *)userdata, 1);
    return RoboOk;
}

static Result ConfOnDisconnected(const HtmlNodeHandle *element, void *userdata,
                                 HtmlDiag *diag)
{
    (void)element;
    (void)diag;
    ConfLifePush((ConfLife *)userdata, 2);
    return RoboOk;
}

static Result ConfOnAttribute(const HtmlNodeHandle *element, const char *name,
                              const char *oldValue, const char *newValue,
                              void *userdata, HtmlDiag *diag)
{
    (void)element;
    (void)name;
    (void)oldValue;
    (void)newValue;
    (void)diag;
    ConfLifePush((ConfLife *)userdata, 3);
    return RoboOk;
}

static void TestConformanceCustomElementLifecycle(void)
{
    HtmlDocumentId id = DomDocumentOf(
        "<body><my-box data-v=\"1\">Box</my-box></body>");
    HtmlNodeHandle root = DomRoot(id);
    HtmlNodeHandle body = DomFindTag(&root, RoboHtmlTagBody);
    HtmlCustomElementDefinition definition;
    const char *observed[1];
    ConfLife life;

    observed[0] = "data-v";
    memset(&life, 0, sizeof(life));
    HtmlCustomElementDefinitionInit(&definition);
    definition.name = "my-box";
    definition.observedAttributes = observed;
    definition.observedCount = 1u;
    definition.callbacks.connected = ConfOnConnected;
    definition.callbacks.disconnected = ConfOnDisconnected;
    definition.callbacks.attributeChanged = ConfOnAttribute;
    definition.userdata = &life;

    /* Define over a parsed element: the upgrade pass fires both the observed
     * attribute and — because the element is connected — the connection. */
    RoboCheckEqInt(HtmlDocumentDefineCustomElement(id, &definition, NULL),
                   RoboOk);
    RoboCheckEqInt(life.count, 2);
    RoboCheckEqInt(life.order[0], 3);
    RoboCheckEqInt(life.order[1], 1);

    {
        HtmlNodeHandle element;

        RoboCheckEqInt(HtmlNodeChildAt(&body, 0u, &element, NULL), RoboOk);

        /* An observed attribute fires; an unobserved one is silent. */
        life.count = 0;
        RoboCheckEqInt(HtmlElementSetAttribute(&element, "data-v", "2", NULL),
                       RoboOk);
        RoboCheckEqInt(life.count, 1);
        RoboCheckEqInt(life.order[0], 3);
        life.count = 0;
        RoboCheckEqInt(HtmlElementSetAttribute(&element, "title", "x", NULL),
                       RoboOk);
        RoboCheckEqInt(life.count, 0);

        /* Detach: disconnected. Re-attach: connected. */
        life.count = 0;
        RoboCheckEqInt(HtmlNodeRemoveChild(&body, &element, NULL), RoboOk);
        RoboCheckEqInt(life.count, 1);
        RoboCheckEqInt(life.order[0], 2);
        life.count = 0;
        RoboCheckEqInt(HtmlNodeAppendChild(&body, &element, NULL), RoboOk);
        RoboCheckEqInt(life.count, 1);
        RoboCheckEqInt(life.order[0], 1);

        /* Insert into a *detached* subtree: silent, because the tree's shape
         * is the question. Connecting the holder connects the element. */
        RoboCheckEqInt(HtmlNodeRemoveChild(&body, &element, NULL), RoboOk);
        life.count = 0;
        {
            HtmlNodeHandle holder;

            RoboCheckEqInt(HtmlDocumentCreateElement(id, "div", &holder, NULL),
                           RoboOk);
            RoboCheckEqInt(HtmlNodeAppendChild(&holder, &element, NULL),
                           RoboOk);
            RoboCheckEqInt(life.count, 0);
            RoboCheckEqInt(HtmlNodeAppendChild(&body, &holder, NULL), RoboOk);
            RoboCheckEqInt(life.count, 1);
            RoboCheckEqInt(life.order[0], 1);
        }
    }

    /* The registry answers by name, and a redefinition is refused rather than
     * silently replacing the callbacks of live instances. */
    RoboCheckEqInt(HtmlDocumentCustomElementIsDefined(id, "my-box"), 1);
    RoboCheckEqInt((int)HtmlDocumentCustomElementCount(id), 1);
    RoboCheckEqInt(HtmlDocumentDefineCustomElement(id, &definition, NULL),
                   RoboAlreadyExists);
    RoboCheckEqInt(HtmlDocumentCustomElementIsDefined(id, "not-a-name"), 0);
    RoboCheckEqInt(HtmlCustomElementNameValidate("my-box", NULL), RoboOk);
    /* "Malformed" and "unavailable" are different answers: `div` is a
     * perfectly well-formed name this runtime will not hand out, while
     * `myrow` is not a custom element name at all. */
    RoboCheckEqInt(HtmlCustomElementNameValidate("div", NULL),
                   RoboNotSupported);
    RoboCheckEqInt(HtmlCustomElementNameValidate("myrow", NULL),
                   RoboParseError);
    RoboCheckEqInt(HtmlCustomElementNameValidate("My-Box", NULL),
                   RoboParseError);

    DomExpectCleanTree(id);
    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
}

/* ------------------------------------------------------------------ */
/* 8. Every gate refuses with the same code                            */
/* ------------------------------------------------------------------ */

static void ConfSetCaps(HtmlDocumentId id, int domMutation, int resourceLoading,
                        int navigation, int customElements, int shadowDom)
{
    HtmlDocumentCapabilities caps;

    caps.domMutation = domMutation;
    caps.resourceLoading = resourceLoading;
    caps.navigation = navigation;
    caps.customElements = customElements;
    caps.shadowDom = shadowDom;
    RoboCheckEqInt(HtmlDocumentSetCapabilities(id, &caps, NULL), RoboOk);
}

static void TestConformanceSecurityGates(void)
{
    HtmlDocumentId id = DomDocumentOf(
        "<body><a href=\"/x\">x</a><img src=\"a.png\">"
        "<div id=\"h\"></div></body>");
    HtmlNodeHandle root = DomRoot(id);
    HtmlNodeHandle body = DomFindTag(&root, RoboHtmlTagBody);
    HtmlNodeHandle anchor = DomFindTag(&root, RoboHtmlTagA);
    HtmlNodeHandle img = DomFindTag(&root, RoboHtmlTagImg);
    HtmlNodeHandle div = DomFindId(&root, "h");
    HtmlNodeHandle created;
    HtmlNodeHandle shadow;
    HtmlCustomElementDefinition definition;
    HtmlImageResolver resolver;
    HtmlImageLoadOutcome loadOutcome;
    HtmlNavigationOutcome navOutcome;
    HtmlNavigationHandler handler;

    HtmlImageResolverInit(&resolver);
    HtmlNavigationHandlerInit(&handler);
    RoboCheckEqInt(HtmlDocumentSetNavigationHandler(id, &handler, NULL),
                   RoboOk);
    ConfSetCaps(id, 0, 0, 0, 0, 0);

    /*
     * PermissionDenied is the code, and it is *the* code: each of these would
     * also be defensible as NotSupported, and a caller that saw that would
     * conclude the runtime cannot do the thing rather than that this document
     * was not allowed to.
     */
    RoboCheckEqInt(HtmlNodeAppendChild(&body, &div, NULL),
                   RoboPermissionDenied);
    RoboCheckEqInt(HtmlElementSetAttribute(&div, "a", "b", NULL),
                   RoboPermissionDenied);
    RoboCheckEqInt(HtmlElementImageLoad(&img, &resolver, &loadOutcome, NULL),
                   RoboPermissionDenied);
    RoboCheckEqInt(HtmlDocumentActivateLink(&anchor, &navOutcome, NULL),
                   RoboPermissionDenied);
    RoboCheckEqInt(HtmlElementAttachShadow(&div, RoboHtmlShadowModeOpen,
                                          &shadow, NULL),
                   RoboPermissionDenied);
    HtmlCustomElementDefinitionInit(&definition);
    definition.name = "my-box";
    RoboCheckEqInt(HtmlDocumentDefineCustomElement(id, &definition, NULL),
                   RoboPermissionDenied);

    /* None of it changed the document. */
    RoboCheckEqInt((int)HtmlDocumentImageCount(id), 0);
    RoboCheckEqInt(HtmlDocumentCustomElementCount(id), 0u);
    RoboCheckEqInt(HtmlElementShadowRoot(&div, &shadow, NULL), RoboNotFound);
    RoboCheckStrEq(DomAttr(&anchor, "href"), "/x");
    DomExpectCleanTree(id);

    /* Granted again, each door works — the capability is not a latch. */
    ConfSetCaps(id, 1, 1, 1, 1, 1);
    RoboCheckEqInt(HtmlDocumentCreateElement(id, "em", &created, NULL),
                   RoboOk);
    RoboCheckEqInt(HtmlNodeAppendChild(&body, &created, NULL), RoboOk);
    RoboCheckEqInt(HtmlElementSetAttribute(&div, "a", "b", NULL), RoboOk);
    RoboCheckEqInt(HtmlDocumentDefineCustomElement(id, &definition, NULL),
                   RoboOk);

    /* And the same six doors answer NotFound on a destroyed document, which is
     * a different fact from PermissionDenied. */
    {
        HtmlDocumentId dead = DomDocumentOf("<body><div></div></body>");
        HtmlNodeHandle deadRoot = DomRoot(dead);
        HtmlNodeHandle deadDiv = DomFindTag(&deadRoot, RoboHtmlTagDiv);
        HtmlNodeHandle deadBody = DomFindTag(&deadRoot, RoboHtmlTagBody);
        HtmlNodeHandle created;

        RoboCheckEqInt(HtmlDocumentCreateElement(dead, "em", &created, NULL),
                       RoboOk);
        RoboCheckEqInt(HtmlDocumentDestroy(dead, NULL), RoboOk);
        RoboCheckEqInt(HtmlNodeAppendChild(&deadBody, &created, NULL),
                       RoboNotFound);
        RoboCheckEqInt(HtmlElementSetAttribute(&deadDiv, "a", "b", NULL),
                       RoboNotFound);
        RoboCheckEqInt(HtmlElementImageLoad(&deadDiv, &resolver, &loadOutcome,
                                            NULL),
                       RoboNotFound);
        RoboCheckEqInt(HtmlDocumentActivateLink(&deadDiv, &navOutcome, NULL),
                       RoboNotFound);
        RoboCheckEqInt(HtmlElementAttachShadow(&deadDiv,
                                               RoboHtmlShadowModeOpen, &shadow,
                                               NULL),
                       RoboNotFound);
        RoboCheckEqInt(HtmlDocumentDefineCustomElement(dead, &definition,
                                                       NULL),
                       RoboNotFound);
    }

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
}

/* ------------------------------------------------------------------ */
/* 9. The audit holds after all of it                                  */
/* ------------------------------------------------------------------ */

static void TestConformanceLifetimeAudit(void)
{
    HtmlDocumentId id = DomDocumentOf(
        "<body>"
        "<form id=\"f\"><input name=\"a\"></form>"
        "<table id=\"t\"><tr><td>x</td></tr></table>"
        "<template id=\"tpl\"><p>inert</p></template>"
        "<div id=\"host\"></div>"
        "<my-row data-v=\"1\">r</my-row>"
        "</body>");
    HtmlNodeHandle root = DomRoot(id);
    HtmlNodeHandle host = DomFindId(&root, "host");
    HtmlNodeHandle shadow;
    HtmlSecurityReport report;
    HtmlCustomElementDefinition definition;
    size_t count = 0u;

    /* Exercise every 17.x surface against one document, then ask the model
     * whether it still agrees with itself. */
    RoboCheckEqInt(HtmlElementAttachShadow(&host, RoboHtmlShadowModeOpen,
                                          &shadow, NULL),
                   RoboOk);
    {
        HtmlNodeHandle inner;

        RoboCheckEqInt(HtmlDocumentCreateElement(id, "span", &inner, NULL),
                       RoboOk);
        RoboCheckEqInt(HtmlNodeAppendChild(&shadow, &inner, NULL), RoboOk);
    }
    HtmlCustomElementDefinitionInit(&definition);
    definition.name = "my-row";
    RoboCheckEqInt(HtmlDocumentDefineCustomElement(id, &definition, NULL),
                   RoboOk);
    {
        HtmlNodeHandle template;
        int inert = 0;

        RoboCheckEqInt(HtmlNodeFindElement(&root, "template", &template,
                                           NULL),
                       RoboOk);
        RoboCheckEqInt(HtmlElementTemplateIsInert(&template, &inert, NULL),
                       RoboOk);
        RoboCheckEqInt(inert, 1);
    }
    (void)count;

    memset(&report, 0xff, sizeof(report));
    RoboCheckEqInt(HtmlDocumentGetSecurityReport(id, &report, NULL), RoboOk);
    RoboCheckEqInt(report.treeIntact, 1);
    RoboCheck(report.reachableNodes >= 6u);
    RoboCheckEqInt((int)report.customElementDefinitions, 1);
    RoboCheckEqInt(report.capabilities.domMutation, 1);

    /* The audit is the runtime's own, and a shadow tree and a template's
     * content are legal detached roots rather than corruption. */
    DomExpectCleanTree(id);
    {
        HtmlTreeReport tree;

        memset(&tree, 0, sizeof(tree));
        RoboCheckEqInt(HtmlDocumentValidateTree(id, &tree, NULL), RoboOk);
        RoboCheckEqInt(tree.ok, 1);
        RoboCheck(tree.rootOk);
        RoboCheck((int)tree.detached >= 2); /* shadow root + template content */
    }

    /* Destroying retires every handle at once: the registry's generation field
     * is what makes that true, and this is where it is read. */
    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    RoboCheckEqInt(HtmlNodeAppendChild(&root, &host, NULL), RoboNotFound);
    RoboCheckEqInt(HtmlElementSetAttribute(&host, "a", "b", NULL),
                   RoboNotFound);
}

/* ------------------------------------------------------------------ */
/* 10. The same bytes produce the same dump                            */
/* ------------------------------------------------------------------ */

static void TestConformanceDeterminism(void)
{
    static const char markup[] =
        "<!DOCTYPE html><html><head><title>c</title></head><body>"
        "<form id=\"f\"><input name=\"a\"><input name=\"b\"></form>"
        "<table><caption>c</caption><tr><td>1</td><td>2</td></tr></table>"
        "<template><p>inert</p></template>"
        "<div id=\"host\"><span>light</span></div>"
        "<my-row data-v=\"1\">r</my-row>"
        "<a href=\"/x\">x</a><img src=\"a.png\">"
        "</body></html>";
    char first[8192];
    char second[8192];
    unsigned i;

    {
        HtmlDocumentId id = DomDocumentOf(markup);

        DomDump(id, first, sizeof(first));
        RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    }
    {
        HtmlDocumentId id = DomDocumentOf(markup);

        DomDump(id, second, sizeof(second));
        RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    }
    RoboCheckStrEq(first, second);

    /*
     * A hundred repetitions, because a dump that agrees once can still be a
     * dump of something that does not — and the comparison is byte for byte,
     * not a summary the way a digest would be.
     */
    for (i = 0u; i < 100u; i++) {
        HtmlDocumentId id = DomDocumentOf(markup);
        char again[8192];

        DomDump(id, again, sizeof(again));
        RoboCheckStrEq(again, first);
        RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    }
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

void TestRegisterHtmlDomConformance(void);

void TestRegisterHtmlDomConformance(void)
{
    TestRegister("html.conformance.version", TestConformanceVersion);
    TestRegister("html.conformance.capability_matrix",
                 TestConformanceCapabilityMatrix);
    TestRegister("html.conformance.reference_policy",
                 TestConformanceReferencePolicy);
    TestRegister("html.conformance.form_collection",
                 TestConformanceFormCollection);
    TestRegister("html.conformance.table_structure",
                 TestConformanceTableStructure);
    TestRegister("html.conformance.template_inertness",
                 TestConformanceTemplateInertness);
    TestRegister("html.conformance.shadow_boundary",
                 TestConformanceShadowBoundary);
    TestRegister("html.conformance.custom_element_lifecycle",
                 TestConformanceCustomElementLifecycle);
    TestRegister("html.conformance.security_gates",
                 TestConformanceSecurityGates);
    TestRegister("html.conformance.lifetime_audit",
                 TestConformanceLifetimeAudit);
    TestRegister("html.conformance.determinism",
                 TestConformanceDeterminism);
}
