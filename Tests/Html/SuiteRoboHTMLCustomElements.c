/*
 * RoboHTML Phase 17.9 custom element suite (html.custom.*).
 *
 * Two claims, and each is only real if a *negative* case holds:
 *
 *   connected is a fact about the tree, not a flag — so inserting a custom
 *   element into a detached subtree must fire nothing, and attaching that
 *   subtree must fire exactly one `connected` (`html.custom.connected`).
 *
 *   attributeChanged is about *observed* attributes — so changing an
 *   unobserved one must stay silent (`html.custom.attributes`).
 *
 * The callbacks are C function pointers here, exactly because RoboHTML has no
 * script engine: what is under test is the registry and the dispatch, and a
 * recorder definition makes both observable without a JS layer.
 */

#include "RoboBase/RoboBase.h"
#include "RoboHTML/RoboHTML.h"
#include "RoboHTML/RoboHTMLCustomElements.h"
#include "RoboHTML/RoboHTMLDocument.h"
#include "RoboHTML/RoboHTMLDom.h"
#include "RoboHTML/RoboHTMLDomQuery.h"
#include "RoboHTML/RoboHTMLElement.h"
#include "RoboHTML/RoboHTMLMutation.h"
#include "RoboHTML/RoboHTMLNode.h"
#include "RoboHTML/RoboHTMLShadow.h"
#include "RoboHTML/RoboHTMLSource.h"
#include "RoboHTML/RoboHTMLTag.h"
#include "RoboHTML/RoboHTMLTraversal.h"
#include "RoboHTML/RoboHTMLTreeBuilder.h"
#include "RoboHTML/RoboHTMLVersion.h"
#include "RoboTest.h"

#include <stddef.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Fixture                                                             */
/* ------------------------------------------------------------------ */

static HtmlDocumentId CustomLiveDocument = RoboHtmlDocumentIdNone;

static void CustomFixtureBegin(void)
{
    if (!BaseIsInitialized()) {
        RoboCheckEqInt(BaseInit(), RoboOk);
    }
    if (!HtmlIsInitialized()) {
        RoboCheckEqInt(HtmlInit(), RoboOk);
    }
}

static HtmlDocumentId CustomDoc(void)
{
    CustomFixtureBegin();
    if (CustomLiveDocument != RoboHtmlDocumentIdNone) {
        RoboCheckEqInt(HtmlDocumentDestroy(CustomLiveDocument, NULL), RoboOk);
        CustomLiveDocument = RoboHtmlDocumentIdNone;
    }
    RoboCheckEqInt(HtmlDocumentCreate(&CustomLiveDocument, NULL), RoboOk);
    RoboCheck(CustomLiveDocument != RoboHtmlDocumentIdNone);
    return CustomLiveDocument;
}

static void CustomDocEnd(void)
{
    if (CustomLiveDocument != RoboHtmlDocumentIdNone) {
        RoboCheckEqInt(HtmlDocumentDestroy(CustomLiveDocument, NULL), RoboOk);
        CustomLiveDocument = RoboHtmlDocumentIdNone;
    }
}

static void CustomBuild(HtmlDocumentId id, const char *text)
{
    HtmlSource src;

    HtmlSourceInit(&src);
    RoboCheckEqInt(HtmlSourceString(&src, text, NULL), RoboOk);
    RoboCheckEqInt(HtmlBuildTree(id, &src, NULL, NULL), RoboOk);
}

static HtmlNodeHandle CustomRoot(HtmlDocumentId id)
{
    HtmlNodeHandle root = HtmlDocumentRoot(id);

    RoboCheck(HtmlNodeHandleIsWellformed(&root));
    return root;
}

typedef struct customTagQuery {
    HtmlTag tag;
} CustomTagQuery;

static int CustomPredicateTag(const HtmlNodeView *view, void *user)
{
    const CustomTagQuery *q = (const CustomTagQuery *)user;

    return HtmlElementIsTag(&view->handle, q->tag);
}

static int CustomTryFind(const HtmlNodeHandle *start, HtmlTag tag,
                         HtmlNodeHandle *out)
{
    CustomTagQuery q;
    Result r;

    q.tag = tag;
    HtmlNodeHandleClear(out);
    r = HtmlNodeFindFirst(start, RoboHtmlNodeFilterElements,
                          CustomPredicateTag, &q, out, NULL);
    if (r == RoboNotFound) {
        return 0;
    }
    RoboCheckEqInt(r, RoboOk);
    return HtmlNodeHandleIsWellformed(out) ? 1 : 0;
}

static HtmlNodeHandle CustomFind(const HtmlNodeHandle *start, HtmlTag tag)
{
    HtmlNodeHandle out;

    RoboCheck(CustomTryFind(start, tag, &out));
    return out;
}

/** Find the first element whose *name* is @p name — custom names are not tags. */
typedef struct customNameQuery {
    const char *name;
} CustomNameQuery;

static int CustomPredicateName(const HtmlNodeView *view, void *user)
{
    const CustomNameQuery *q = (const CustomNameQuery *)user;
    const char *name = HtmlElementName(&view->handle, NULL);

    return name && strcmp(name, q->name) == 0;
}

static int CustomTryFindName(const HtmlNodeHandle *start, const char *name,
                             HtmlNodeHandle *out)
{
    CustomNameQuery q;
    Result r;

    q.name = name;
    HtmlNodeHandleClear(out);
    r = HtmlNodeFindFirst(start, RoboHtmlNodeFilterElements,
                          CustomPredicateName, &q, out, NULL);
    if (r == RoboNotFound) {
        return 0;
    }
    RoboCheckEqInt(r, RoboOk);
    return HtmlNodeHandleIsWellformed(out) ? 1 : 0;
}

static HtmlNodeHandle CustomFindName(const HtmlNodeHandle *start,
                                     const char *name)
{
    HtmlNodeHandle out;

    RoboCheck(CustomTryFindName(start, name, &out));
    return out;
}

static size_t CustomChildCount(const HtmlNodeHandle *node)
{
    size_t count = 0u;

    RoboCheckEqInt(HtmlNodeChildCount(node, &count, NULL), RoboOk);
    return count;
}

/* ------------------------------------------------------------------ */
/* A recording definition                                              */
/* ------------------------------------------------------------------ */

#define CUSTOM_LOG_MAX 32u
#define CUSTOM_STR_MAX 64u

typedef struct customLog {
    size_t connected;
    size_t disconnected;
    size_t attributeChanged;
    /* The last attribute change, so old/new are asserted and not just
     * counted. */
    char lastName[CUSTOM_STR_MAX];
    char lastOld[CUSTOM_STR_MAX];
    char lastNew[CUSTOM_STR_MAX];
    int lastOldPresent;
    int lastNewPresent;
    /* Where the last connection happened, if it asked. */
    HtmlNodeHandle lastElement;
    size_t order[CUSTOM_LOG_MAX];
    size_t orderCount;
} CustomLog;

#define CUSTOM_ORDER_CONNECTED 1u
#define CUSTOM_ORDER_ATTRIBUTE 2u
#define CUSTOM_ORDER_DISCONNECTED 3u

static void CustomLogRecord(CustomLog *log, size_t kind)
{
    log->orderCount++;
    if (log->orderCount <= CUSTOM_LOG_MAX) {
        log->order[log->orderCount - 1u] = kind;
    }
}

static Result CustomConnected(const HtmlNodeHandle *element, void *userdata,
                              HtmlDiag *diag)
{
    CustomLog *log = (CustomLog *)userdata;

    (void)diag;
    log->connected++;
    log->lastElement = *element;
    CustomLogRecord(log, CUSTOM_ORDER_CONNECTED);
    return RoboOk;
}

static Result CustomDisconnected(const HtmlNodeHandle *element, void *userdata,
                                 HtmlDiag *diag)
{
    CustomLog *log = (CustomLog *)userdata;

    (void)diag;
    (void)element;
    log->disconnected++;
    CustomLogRecord(log, CUSTOM_ORDER_DISCONNECTED);
    return RoboOk;
}

static Result CustomAttributeChanged(const HtmlNodeHandle *element,
                                     const char *name, const char *oldValue,
                                     const char *newValue, void *userdata,
                                     HtmlDiag *diag)
{
    CustomLog *log = (CustomLog *)userdata;

    (void)diag;
    (void)element;
    log->attributeChanged++;
    memset(log->lastName, 0, sizeof(log->lastName));
    memset(log->lastOld, 0, sizeof(log->lastOld));
    memset(log->lastNew, 0, sizeof(log->lastNew));
    if (name) {
        size_t n = strlen(name);

        if (n >= sizeof(log->lastName)) {
            n = sizeof(log->lastName) - 1u;
        }
        memcpy(log->lastName, name, n);
    }
    log->lastOldPresent = oldValue != NULL;
    log->lastNewPresent = newValue != NULL;
    if (oldValue) {
        size_t n = strlen(oldValue);

        if (n >= sizeof(log->lastOld)) {
            n = sizeof(log->lastOld) - 1u;
        }
        memcpy(log->lastOld, oldValue, n);
    }
    if (newValue) {
        size_t n = strlen(newValue);

        if (n >= sizeof(log->lastNew)) {
            n = sizeof(log->lastNew) - 1u;
        }
        memcpy(log->lastNew, newValue, n);
    }
    CustomLogRecord(log, CUSTOM_ORDER_ATTRIBUTE);
    return RoboOk;
}

static void CustomLogInit(CustomLog *log)
{
    memset(log, 0, sizeof(*log));
    HtmlNodeHandleClear(&log->lastElement);
}

/** Define @p name with all three callbacks recording into @p log. */
static void CustomDefine(HtmlDocumentId id, const char *name, CustomLog *log,
                         const char *const *observed, size_t observedCount)
{
    HtmlCustomElementDefinition definition;

    HtmlCustomElementDefinitionInit(&definition);
    definition.name = name;
    definition.observedAttributes = observed;
    definition.observedCount = observedCount;
    definition.callbacks.connected = CustomConnected;
    definition.callbacks.disconnected = CustomDisconnected;
    definition.callbacks.attributeChanged = CustomAttributeChanged;
    definition.userdata = log;
    RoboCheckEqInt(HtmlDocumentDefineCustomElement(id, &definition, NULL),
                   RoboOk);
}

/* ------------------------------------------------------------------ */
/* Name validation                                                     */
/* ------------------------------------------------------------------ */

static void TestCustomCapabilities(void)
{
    HtmlCustomElementCapabilities caps;

    memset(&caps, 0xff, sizeof(caps));
    RoboCheckEqInt(HtmlCustomElementGetCapabilities(&caps), RoboOk);
    RoboCheckEqInt(caps.registry, 1);
    RoboCheckEqInt(caps.nameValidation, 1);
    RoboCheckEqInt(caps.definedQuery, 1);
    RoboCheckEqInt(caps.definitionQuery, 1);
    RoboCheckEqInt(caps.createElement, 1);
    RoboCheckEqInt(caps.connectedCallback, 1);
    RoboCheckEqInt(caps.disconnectedCallback, 1);
    RoboCheckEqInt(caps.attributeChangedCallback, 1);
    RoboCheckEqInt(caps.upgradeOnDefine, 1);
    RoboCheckEqInt(caps.reachabilityDriven, 1);
    RoboCheckEqInt(caps.constructors, 0);
    RoboCheckEqInt(caps.reactionQueue, 0);
    RoboCheckEqInt(caps.customisedBuiltins, 0);
    RoboCheckEqInt(caps.scopedRegistries, 0);
    RoboCheckEqInt(caps.definedSelector, 0);
    RoboCheckEqInt(caps.callbackResults, 0);
    RoboCheckStrEq(caps.version, RoboHtmlVersionString);

    RoboCheckEqInt(HtmlCustomElementGetCapabilities(NULL),
                   RoboInvalidArgument);
}

static void TestCustomNameValidation(void)
{
    static const char *const valid[] = {
        "my-row", "x-a", "a-b-c", "e1-e2", "a-", "my_el-row",
    };
    static const char *const invalid[] = {
        "myrow",    /* no hyphen */
        "my_el",    /* '_' is a legal character but the '-' is still required */
        "my.el",    /* '.' likewise */
        "My-Row",   /* uppercase */
        "-my-row",  /* leading hyphen */
        "_my-row",  /* leading underscore */
        "1-my-row", /* leading digit */
        "my row",   /* space */
        "my/row",   /* slash */
        "div",      /* built-in */
        "span",     /* built-in */
        "annotation-xml", /* reserved */
        "font-face",      /* reserved */
    };
    size_t i;

    for (i = 0u; i < sizeof(valid) / sizeof(valid[0]); i++) {
        RoboCheckMsg(HtmlCustomElementNameIsValid(valid[i]), valid[i]);
        RoboCheckEqInt(HtmlCustomElementNameValidate(valid[i], NULL), RoboOk);
    }
    for (i = 0u; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
        RoboCheckMsg(!HtmlCustomElementNameIsValid(invalid[i]),
                     "an invalid name was accepted");
    }

    RoboCheckEqInt(HtmlCustomElementNameValidate(NULL, NULL),
                   RoboInvalidArgument);
    RoboCheckEqInt(HtmlCustomElementNameValidate("", NULL),
                   RoboInvalidArgument);
    /* Too long is its own answer, not "bad characters". */
    {
        char longName[RoboHtmlNameMax + 16u];
        size_t n;

        for (n = 0u; n < sizeof(longName) - 1u; n++) {
            longName[n] = (n % 2u) ? '-' : 'a';
        }
        longName[sizeof(longName) - 1u] = '\0';
        RoboCheckEqInt(HtmlCustomElementNameValidate(longName, NULL),
                       RoboNotSupported);
    }
}

/* ------------------------------------------------------------------ */
/* The registry                                                        */
/* ------------------------------------------------------------------ */

static void TestCustomDefine(void)
{
    HtmlDocumentId id = CustomDoc();
    CustomLog log;
    HtmlCustomElementDefinition definition;
    static const char *const observed[] = {"kind"};

    CustomLogInit(&log);
    RoboCheckEqInt(HtmlDocumentCustomElementCount(id), 0u);
    RoboCheckEqInt(HtmlDocumentCustomElementIsDefined(id, "my-row"), 0);

    CustomDefine(id, "my-row", &log, observed, 1u);
    RoboCheckEqInt(HtmlDocumentCustomElementCount(id), 1u);
    RoboCheckEqInt(HtmlDocumentCustomElementIsDefined(id, "my-row"), 1);
    RoboCheckEqInt(HtmlDocumentCustomElementIsDefined(id, "other-row"), 0);

    memset(&definition, 0, sizeof(definition));
    RoboCheckEqInt(HtmlDocumentCustomElementDefinitionGet(id, "my-row",
                                                         &definition, NULL),
                   RoboOk);
    RoboCheckStrEq(definition.name, "my-row");
    RoboCheckEqInt((int)definition.observedCount, 1);
    if (definition.observedAttributes) {
        RoboCheckStrEq(definition.observedAttributes[0], "kind");
    }
    RoboCheck(definition.callbacks.connected == CustomConnected);
    RoboCheck(definition.callbacks.disconnected == CustomDisconnected);
    RoboCheck(definition.callbacks.attributeChanged ==
              CustomAttributeChanged);
    RoboCheck(definition.userdata == &log);

    /* A second definition of the same name would silently change the
     * callbacks of every instance already upgraded. */
    RoboCheckEqInt(HtmlDocumentDefineCustomElement(id, &definition, NULL),
                   RoboAlreadyExists);
    RoboCheckEqInt(HtmlDocumentCustomElementCount(id), 1u);

    /* An invalid name is refused by the definition path too — malformed and
     * unavailable are different answers, not one. */
    definition.name = "noseparator";
    RoboCheckEqInt(HtmlDocumentDefineCustomElement(id, &definition, NULL),
                   RoboParseError);
    definition.name = "div";
    RoboCheckEqInt(HtmlDocumentDefineCustomElement(id, &definition, NULL),
                   RoboNotSupported);
    definition.name = "annotation-xml";
    RoboCheckEqInt(HtmlDocumentDefineCustomElement(id, &definition, NULL),
                   RoboNotSupported);
    RoboCheckEqInt(HtmlDocumentCustomElementCount(id), 1u);

    /* Too many observed attributes is refused, never truncated. */
    {
        const char *many[RoboHtmlCustomElementMaxObserved + 1u];
        size_t i;

        for (i = 0u; i < RoboHtmlCustomElementMaxObserved + 1u; i++) {
            many[i] = "a";
        }
        definition.name = "many-obs";
        definition.observedAttributes = many;
        definition.observedCount = RoboHtmlCustomElementMaxObserved + 1u;
        RoboCheckEqInt(HtmlDocumentDefineCustomElement(id, &definition,
                                                       NULL),
                       RoboNotSupported);
    }

    /* A NULL entry in the observed list is refused. */
    {
        static const char *const broken[] = {NULL};

        definition.name = "broken-obs";
        definition.observedAttributes = broken;
        definition.observedCount = 1u;
        RoboCheckEqInt(HtmlDocumentDefineCustomElement(id, &definition,
                                                       NULL),
                       RoboInvalidArgument);
    }

    /* Argument errors. */
    RoboCheckEqInt(HtmlDocumentDefineCustomElement(id, NULL, NULL),
                   RoboInvalidArgument);
    definition.name = NULL;
    RoboCheckEqInt(HtmlDocumentDefineCustomElement(id, &definition, NULL),
                   RoboInvalidArgument);
    RoboCheckEqInt(HtmlDocumentCustomElementDefinitionGet(id, "my-row",
                                                          NULL, NULL),
                   RoboInvalidArgument);
    RoboCheckEqInt(HtmlDocumentCustomElementDefinitionGet(id, "nope",
                                                          &definition, NULL),
                   RoboNotFound);

    CustomDocEnd();
}

static void TestCustomRegistryIsPerDocument(void)
{
    HtmlDocumentId id = CustomDoc();
    CustomLog log;
    HtmlDocumentId other = RoboHtmlDocumentIdNone;

    CustomLogInit(&log);
    CustomDefine(id, "my-row", &log, NULL, 0u);

    RoboCheckEqInt(HtmlDocumentCreate(&other, NULL), RoboOk);
    /* A registry is per document: the second document knows nothing of the
     * first's definitions, and can define the same name. */
    RoboCheckEqInt(HtmlDocumentCustomElementIsDefined(other, "my-row"), 0);
    CustomDefine(other, "my-row", &log, NULL, 0u);
    RoboCheckEqInt(HtmlDocumentCustomElementIsDefined(other, "my-row"), 1);
    RoboCheckEqInt(HtmlDocumentCustomElementCount(other), 1u);

    /* Destroying the second document frees its registry and leaves the first
     * untouched. */
    RoboCheckEqInt(HtmlDocumentDestroy(other, NULL), RoboOk);
    RoboCheckEqInt(HtmlDocumentCustomElementIsDefined(id, "my-row"), 1);
    RoboCheckEqInt(HtmlDocumentCustomElementCount(id), 1u);

    CustomDocEnd();
}

/* ------------------------------------------------------------------ */
/* createElement and the "custom" predicate                            */
/* ------------------------------------------------------------------ */

static void TestCustomCreateElement(void)
{
    HtmlDocumentId id = CustomDoc();
    CustomLog log;
    HtmlNodeHandle docRoot;
    HtmlNodeHandle element;
    HtmlNodeHandle plain;
    int isCustom = 0;

    CustomLogInit(&log);
    CustomBuild(id, "<body><div id=\"plain\"></div></body>");
    docRoot = CustomRoot(id);
    plain = CustomFind(&docRoot, RoboHtmlTagDiv);

    CustomDefine(id, "my-row", &log, NULL, 0u);

    RoboCheckEqInt(HtmlDocumentCreateElement(id, "my-row", &element, NULL),
                   RoboOk);
    /* The tag vocabulary does not know the name — that is the *unknown* fact,
     * separate from the *custom* one. */
    RoboCheckEqInt(HtmlElementTag(&element), RoboHtmlTagUnknown);
    RoboCheckEqInt(HtmlElementIsCustomElement(&element, &isCustom, NULL),
                   RoboOk);
    RoboCheckEqInt(isCustom, 1);

    /* A built-in is not custom, and neither is an undefined name. */
    RoboCheckEqInt(HtmlElementIsCustomElement(&plain, &isCustom, NULL),
                   RoboOk);
    RoboCheckEqInt(isCustom, 0);
    {
        HtmlNodeHandle other;

        RoboCheckEqInt(HtmlDocumentCreateElement(id, "not-defined", &other,
                                                 NULL),
                       RoboOk);
        RoboCheckEqInt(HtmlElementIsCustomElement(&other, &isCustom, NULL),
                       RoboOk);
        RoboCheckEqInt(isCustom, 0);
    }

    /* An element created before its definition becomes custom when the
     * definition arrives — the name is the link, not the creation order. */
    {
        HtmlNodeHandle late;

        RoboCheckEqInt(HtmlDocumentCreateElement(id, "late-el", &late, NULL),
                       RoboOk);
        RoboCheckEqInt(HtmlElementIsCustomElement(&late, &isCustom, NULL),
                       RoboOk);
        RoboCheckEqInt(isCustom, 0);
        CustomDefine(id, "late-el", &log, NULL, 0u);
        RoboCheckEqInt(HtmlElementIsCustomElement(&late, &isCustom, NULL),
                       RoboOk);
        RoboCheckEqInt(isCustom, 1);
    }

    RoboCheckEqInt(HtmlElementIsCustomElement(&element, NULL, NULL),
                   RoboInvalidArgument);
    RoboCheckEqInt(HtmlElementIsCustomElement(NULL, &isCustom, NULL),
                   RoboInvalidArgument);

    CustomDocEnd();
}

/* ------------------------------------------------------------------ */
/* connected / disconnected                                            */
/* ------------------------------------------------------------------ */

static void TestCustomConnected(void)
{
    HtmlDocumentId id = CustomDoc();
    CustomLog log;
    HtmlNodeHandle docRoot;
    HtmlNodeHandle body;
    HtmlNodeHandle holder;
    HtmlNodeHandle element;

    CustomLogInit(&log);
    CustomBuild(id, "<body><div id=\"holder\"></div></body>");
    docRoot = CustomRoot(id);
    body = CustomFind(&docRoot, RoboHtmlTagBody);
    holder = CustomFind(&docRoot, RoboHtmlTagDiv);

    CustomDefine(id, "my-row", &log, NULL, 0u);

    RoboCheckEqInt(HtmlDocumentCreateElement(id, "my-row", &element, NULL),
                   RoboOk);
    /* Creating an element is not connecting it. */
    RoboCheckEqInt(log.connected, 0u);
    RoboCheckEqInt(log.orderCount, 0u);

    /* Detach the holder, so what follows is an append into a detached
     * subtree — which must be silent. */
    RoboCheckEqInt(HtmlNodeRemoveChild(&body, &holder, NULL), RoboOk);
    RoboCheckEqInt(log.orderCount, 0u);

    /*
     * Appending to a *detached* holder fires nothing: the element is not
     * connected, and "connected" is a question about the tree, not about
     * whether an append happened.
     */
    RoboCheckEqInt(HtmlNodeAppendChild(&holder, &element, NULL), RoboOk);
    RoboCheckEqInt(log.connected, 0u);
    RoboCheckEqInt(log.orderCount, 0u);

    /* Attaching the holder connects everything inside it — exactly once. */
    RoboCheckEqInt(HtmlNodeAppendChild(&body, &holder, NULL), RoboOk);
    RoboCheckEqInt(log.connected, 1u);
    RoboCheck(HtmlNodeHandleEqual(&log.lastElement, &element));
    RoboCheckEqInt(log.orderCount, 1u);
    RoboCheckEqInt(log.order[0], CUSTOM_ORDER_CONNECTED);

    /* Removing the element itself disconnects it. */
    RoboCheckEqInt(HtmlNodeRemoveChild(&holder, &element, NULL), RoboOk);
    RoboCheckEqInt(log.disconnected, 1u);

    /* And re-inserting connects it again — the state is the tree's, so it can
     * flip as many times as the tree does. */
    RoboCheckEqInt(HtmlNodeAppendChild(&holder, &element, NULL), RoboOk);
    RoboCheckEqInt(log.connected, 2u);
    RoboCheckEqInt(log.disconnected, 1u);

    /* Removing the holder while the element is inside disconnects both the
     * holder's subtree... */
    RoboCheckEqInt(HtmlNodeRemoveChild(&body, &holder, NULL), RoboOk);
    RoboCheckEqInt(log.disconnected, 2u);

    /* Detaching an already-detached subtree fires nothing. */
    {
        HtmlNodeHandle inner;

        RoboCheckEqInt(HtmlDocumentCreateElement(id, "inner-el", &inner,
                                                 NULL),
                       RoboOk);
        CustomDefine(id, "inner-el", &log, NULL, 0u);
        RoboCheckEqInt(HtmlNodeAppendChild(&holder, &inner, NULL), RoboOk);
        RoboCheckEqInt(log.connected, 2u);
        RoboCheckEqInt(HtmlNodeRemoveChild(&holder, &inner, NULL), RoboOk);
        RoboCheckEqInt(log.disconnected, 2u);
    }

    CustomDocEnd();
}

static void TestCustomDisconnectAllChildren(void)
{
    HtmlDocumentId id = CustomDoc();
    CustomLog log;
    HtmlNodeHandle docRoot;
    HtmlNodeHandle body;

    CustomLogInit(&log);
    CustomBuild(id, "<body></body>");
    docRoot = CustomRoot(id);
    body = CustomFind(&docRoot, RoboHtmlTagBody);
    CustomDefine(id, "my-row", &log, NULL, 0u);

    {
        size_t i;

        for (i = 0u; i < 3u; i++) {
            HtmlNodeHandle element;

            RoboCheckEqInt(HtmlDocumentCreateElement(id, "my-row", &element,
                                                     NULL),
                           RoboOk);
            RoboCheckEqInt(HtmlNodeAppendChild(&body, &element, NULL),
                           RoboOk);
        }
    }
    RoboCheckEqInt(log.connected, 3u);

    /* The bulk detach announces each removed subtree. */
    {
        size_t removed = 0u;

        RoboCheckEqInt(HtmlNodeRemoveChildren(&body, &removed, NULL), RoboOk);
        RoboCheckEqInt((int)removed, 3);
    }
    RoboCheckEqInt(log.disconnected, 3u);
    RoboCheckEqInt((int)CustomChildCount(&body), 0);

    CustomDocEnd();
}

/* ------------------------------------------------------------------ */
/* attributeChanged                                                    */
/* ------------------------------------------------------------------ */

static void TestCustomAttributes(void)
{
    HtmlDocumentId id = CustomDoc();
    CustomLog log;
    HtmlNodeHandle docRoot;
    HtmlNodeHandle body;
    HtmlNodeHandle element;
    static const char *const observed[] = {"kind", "level"};

    CustomLogInit(&log);
    CustomBuild(id, "<body></body>");
    docRoot = CustomRoot(id);
    body = CustomFind(&docRoot, RoboHtmlTagBody);

    /* Define first, then create: the ordinary order. */
    CustomDefine(id, "my-row", &log, observed, 2u);
    RoboCheckEqInt(HtmlDocumentCreateElement(id, "my-row", &element, NULL),
                   RoboOk);
    RoboCheckEqInt(log.attributeChanged, 0u);

    /* An observed attribute on a *defined* element reports old=NULL (there
     * was nothing) and the new value. */
    RoboCheckEqInt(HtmlElementSetAttribute(&element, "kind", "primary",
                                           NULL),
                   RoboOk);
    RoboCheckEqInt(log.attributeChanged, 1u);
    RoboCheckStrEq(log.lastName, "kind");
    RoboCheckEqInt(log.lastOldPresent, 0);
    RoboCheckEqInt(log.lastNewPresent, 1);
    RoboCheckStrEq(log.lastNew, "primary");

    /* Replacing it reports both sides. */
    RoboCheckEqInt(HtmlElementSetAttribute(&element, "kind", "secondary",
                                           NULL),
                   RoboOk);
    RoboCheckEqInt(log.attributeChanged, 2u);
    RoboCheckStrEq(log.lastOld, "primary");
    RoboCheckStrEq(log.lastNew, "secondary");

    /* Removing it reports the old value and no new one. */
    RoboCheckEqInt(HtmlElementRemoveAttribute(&element, "kind", NULL), RoboOk);
    RoboCheckEqInt(log.attributeChanged, 3u);
    RoboCheckStrEq(log.lastOld, "secondary");
    RoboCheckEqInt(log.lastNewPresent, 0);

    /* The second observed attribute works the same way. */
    RoboCheckEqInt(HtmlElementSetAttribute(&element, "level", "7", NULL),
                   RoboOk);
    RoboCheckEqInt(log.attributeChanged, 4u);
    RoboCheckStrEq(log.lastName, "level");

    /* An *unobserved* attribute is a change nobody asked about: silent. */
    RoboCheckEqInt(HtmlElementSetAttribute(&element, "id", "row-1", NULL),
                   RoboOk);
    RoboCheckEqInt(log.attributeChanged, 4u);
    /* `add` is the other write verb, and it observes the same rule. */
    RoboCheckEqInt(HtmlElementAddAttribute(&element, "kind", "extra", NULL),
                   RoboOk);
    RoboCheckEqInt(log.attributeChanged, 5u);
    RoboCheckStrEq(log.lastNew, "extra");

    /* A non-custom element with the same attribute says nothing. */
    RoboCheckEqInt(HtmlElementSetAttribute(&body, "kind", "x", NULL), RoboOk);
    RoboCheckEqInt(log.attributeChanged, 5u);

    /* A custom element with *no* observed attributes is silent too — the
     * callback exists but nothing was asked for. */
    {
        HtmlNodeHandle quiet;

        CustomDefine(id, "quiet-el", &log, NULL, 0u);
        RoboCheckEqInt(HtmlDocumentCreateElement(id, "quiet-el", &quiet,
                                                 NULL),
                       RoboOk);
        RoboCheckEqInt(HtmlElementSetAttribute(&quiet, "kind", "y", NULL),
                       RoboOk);
        RoboCheckEqInt(log.attributeChanged, 5u);
    }

    CustomDocEnd();
}

/* ------------------------------------------------------------------ */
/* Upgrade                                                             */
/* ------------------------------------------------------------------ */

static void TestCustomUpgradeOnDefine(void)
{
    HtmlDocumentId id = CustomDoc();
    CustomLog log;
    HtmlNodeHandle docRoot;
    HtmlNodeHandle body;
    static const char *const observed[] = {"kind"};

    CustomLogInit(&log);
    /* The elements exist *before* the definition, with an observed attribute
     * already on them. */
    CustomBuild(id, "<body><my-row kind=\"one\"></my-row>"
                    "<my-row kind=\"two\"></my-row></body>");
    docRoot = CustomRoot(id);
    body = CustomFind(&docRoot, RoboHtmlTagBody);

    /* Nothing is custom yet. */
    {
        HtmlNodeHandle first;

        RoboCheck(CustomTryFindName(&body, "my-row", &first));
        {
            int isCustom = 1;

            RoboCheckEqInt(HtmlElementIsCustomElement(&first, &isCustom,
                                                     NULL),
                           RoboOk);
            RoboCheckEqInt(isCustom, 0);
        }
        /* Upgrading before the name is defined is a miss, not a no-op. */
        RoboCheckEqInt(HtmlElementUpgradeCustomElement(&first, NULL),
                       RoboNotFound);
    }

    CustomDefine(id, "my-row", &log, observed, 1u);

    /* Both existing elements upgraded: an attribute report each, and a
     * connection each — in that order per element, because upgrade reports
     * attributes and then connection. */
    RoboCheckEqInt(log.attributeChanged, 2u);
    RoboCheckEqInt(log.connected, 2u);
    RoboCheckEqInt(log.orderCount, 4u);
    RoboCheckEqInt(log.order[0], CUSTOM_ORDER_ATTRIBUTE);
    RoboCheckEqInt(log.order[1], CUSTOM_ORDER_CONNECTED);
    RoboCheckEqInt(log.order[2], CUSTOM_ORDER_ATTRIBUTE);
    RoboCheckEqInt(log.order[3], CUSTOM_ORDER_CONNECTED);

    /* The explicit door: upgrading one element runs the same steps again. */
    {
        HtmlNodeHandle first = CustomFindName(&body, "my-row");

        RoboCheckEqInt(HtmlElementUpgradeCustomElement(&first, NULL), RoboOk);
        RoboCheckEqInt(log.attributeChanged, 3u);
        RoboCheckEqInt(log.connected, 3u);
    }

    /* Upgrading everything reports how many elements it walked. */
    {
        size_t upgraded = 99u;

        RoboCheckEqInt(HtmlDocumentUpgradeCustomElements(id, &upgraded, NULL),
                       RoboOk);
        RoboCheckEqInt((int)upgraded, 2);
    }

    CustomDocEnd();
}

/* ------------------------------------------------------------------ */
/* Shadow DOM interplay                                                */
/* ------------------------------------------------------------------ */

static void TestCustomInsideShadow(void)
{
    HtmlDocumentId id = CustomDoc();
    CustomLog log;
    HtmlNodeHandle docRoot;
    HtmlNodeHandle host;
    HtmlNodeHandle shadow;
    HtmlNodeHandle element;

    CustomLogInit(&log);
    CustomBuild(id, "<body><div id=\"host\"></div></body>");
    docRoot = CustomRoot(id);
    host = CustomFind(&docRoot, RoboHtmlTagDiv);
    CustomDefine(id, "my-row", &log, NULL, 0u);

    RoboCheckEqInt(HtmlElementAttachShadow(&host, RoboHtmlShadowModeOpen,
                                          &shadow, NULL),
                   RoboOk);
    RoboCheckEqInt(HtmlDocumentCreateElement(id, "my-row", &element, NULL),
                   RoboOk);

    /*
     * A shadow tree whose host is connected *is* connected — the walk crosses
     * the boundary to the host — so this fires, even though the node is not
     * reachable from the document root (17.8's own question, and a different
     * one).
     */
    RoboCheckEqInt(HtmlNodeAppendChild(&shadow, &element, NULL), RoboOk);
    RoboCheckEqInt(log.connected, 1u);

    /* Detaching the host disconnects the shadow tree's custom element too. */
    {
        HtmlNodeHandle body = CustomFind(&docRoot, RoboHtmlTagBody);

        RoboCheckEqInt(HtmlNodeRemoveChild(&body, &host, NULL), RoboOk);
        RoboCheckEqInt(log.disconnected, 1u);
        RoboCheckEqInt(HtmlNodeAppendChild(&body, &host, NULL), RoboOk);
        RoboCheckEqInt(log.connected, 2u);
    }

    /* And the reachability question still answers "no" for the same node,
     * because it is a light-tree question. */
    {
        int reachable = 1;

        RoboCheckEqInt(HtmlNodeIsReachableFromDocument(&element, &reachable,
                                                      NULL),
                       RoboOk);
        RoboCheckEqInt(reachable, 0);
    }

    CustomDocEnd();
}

/* ------------------------------------------------------------------ */
/* Lifetime                                                            */
/* ------------------------------------------------------------------ */

static void TestCustomStaleHandles(void)
{
    HtmlDocumentId id = CustomDoc();
    CustomLog log;
    HtmlNodeHandle docRoot;
    HtmlNodeHandle element;
    int isCustom = 1;

    CustomLogInit(&log);
    CustomBuild(id, "<body></body>");
    docRoot = CustomRoot(id);
    CustomDefine(id, "my-row", &log, NULL, 0u);
    RoboCheckEqInt(HtmlDocumentCreateElement(id, "my-row", &element, NULL),
                   RoboOk);
    RoboCheckEqInt(HtmlNodeAppendChild(&docRoot, &element, NULL), RoboOk);

    CustomDocEnd();

    /* The registry went with the document, and a stale handle is refused
     * rather than followed. */
    RoboCheckEqInt(HtmlDocumentCustomElementCount(id), 0u);
    RoboCheckEqInt(HtmlDocumentCustomElementIsDefined(id, "my-row"), 0);
    RoboCheckEqInt(HtmlElementIsCustomElement(&element, &isCustom, NULL),
                   RoboNotFound);
    RoboCheckEqInt(isCustom, 0);
    RoboCheckEqInt(HtmlElementUpgradeCustomElement(&element, NULL),
                   RoboNotFound);
    RoboCheckEqInt(HtmlDocumentDefineCustomElement(id, NULL, NULL),
                   RoboInvalidArgument);
}

static void TestCustomParseNeitherRunsNorBlocks(void)
{
    HtmlDocumentId id = CustomDoc();
    CustomLog log;
    HtmlNodeHandle docRoot;

    CustomLogInit(&log);
    CustomDefine(id, "my-row", &log, NULL, 0u);

    /*
     * Parsing markup that names a defined custom element creates the element
     * and connects it — the definition is a document fact, so the parser's
     * output is already custom.
     */
    CustomBuild(id, "<body><my-row><span>Inside</span></my-row></body>");
    docRoot = CustomRoot(id);
    RoboCheckEqInt(log.connected, 1u);
    {
        HtmlNodeHandle element = CustomFindName(&docRoot, "my-row");
        int isCustom = 0;

        RoboCheckEqInt(HtmlElementIsCustomElement(&element, &isCustom, NULL),
                       RoboOk);
        RoboCheckEqInt(isCustom, 1);
        /* The custom element is an ordinary container: its children parsed
         * normally and are reachable. */
        RoboCheckEqInt((int)CustomChildCount(&element), 1);
        {
            HtmlNodeHandle span;

            RoboCheck(CustomTryFind(&element, RoboHtmlTagSpan, &span));

            {
                int reachable = 0;

                RoboCheckEqInt(HtmlNodeIsReachableFromDocument(&span,
                                                              &reachable,
                                                              NULL),
                               RoboOk);
                RoboCheckEqInt(reachable, 1);
            }
        }
    }

    CustomDocEnd();
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

void TestRegisterHtmlCustomElements(void);

void TestRegisterHtmlCustomElements(void)
{
    TestRegister("html.custom.capabilities", TestCustomCapabilities);
    TestRegister("html.custom.name_validation", TestCustomNameValidation);
    TestRegister("html.custom.define", TestCustomDefine);
    TestRegister("html.custom.registry_per_document",
                 TestCustomRegistryIsPerDocument);
    TestRegister("html.custom.create_element", TestCustomCreateElement);
    TestRegister("html.custom.connected", TestCustomConnected);
    TestRegister("html.custom.disconnect_all_children",
                 TestCustomDisconnectAllChildren);
    TestRegister("html.custom.attributes", TestCustomAttributes);
    TestRegister("html.custom.upgrade_on_define",
                 TestCustomUpgradeOnDefine);
    TestRegister("html.custom.inside_shadow", TestCustomInsideShadow);
    TestRegister("html.custom.stale_handles", TestCustomStaleHandles);
    TestRegister("html.custom.parsed_markup",
                 TestCustomParseNeitherRunsNorBlocks);
}
