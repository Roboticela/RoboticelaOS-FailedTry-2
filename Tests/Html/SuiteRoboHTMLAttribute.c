/*
 * RoboHTML Phase 8.6 attribute-model suite.
 *
 * The attribute model is the runtime's single attribute representation: the
 * tokenizer stages it, the tree builder stores it, an element owns it, and
 * the DOM seam mutates it. This suite covers the type itself, parser
 * integration and stored order, lookup and presence, the mutation verbs,
 * Unicode in names and values, malformed input, the attribute limit, the
 * debug dump, teardown, and the ownership rules that make all of it safe.
 *
 * Note on lookups: the tree builder keeps whitespace between tags as text
 * nodes, so a child index is never an element index — elements are found by
 * name here, never by offset.
 */

#include "RoboBase/RoboBase.h"
#include "RoboHTML/RoboHTML.h"
#include "RoboHTML/RoboHTMLDocument.h"
#include "RoboHTML/RoboHTMLElement.h"
#include "RoboHTML/RoboHTMLNode.h"
#include "RoboHTML/RoboHTMLSource.h"
#include "RoboHTML/RoboHTMLTreeBuilder.h"
#include "RoboTest.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Fixture + helpers                                                   */
/* ------------------------------------------------------------------ */

static void AtFixtureBegin(void)
{
    if (!BaseIsInitialized()) {
        RoboCheckEqInt(BaseInit(), RoboOk);
    }
    if (!HtmlIsInitialized()) {
        RoboCheckEqInt(HtmlInit(), RoboOk);
    }
}

static void AtFixtureEnd(void)
{
    HtmlShutdown();
}

static HtmlDocumentId AtDoc(void)
{
    HtmlDocumentId id = RoboHtmlDocumentIdNone;

    RoboCheckEqInt(HtmlDocumentCreate(&id, NULL), RoboOk);
    RoboCheck(id != RoboHtmlDocumentIdNone);
    return id;
}

/** Build @p text into @p id with the one-shot API. */
static void AtBuild(HtmlDocumentId id, const char *text)
{
    HtmlSource src;

    HtmlSourceInit(&src);
    RoboCheckEqInt(HtmlSourceString(&src, text, NULL), RoboOk);
    RoboCheckEqInt(HtmlBuildTree(id, &src, NULL, NULL), RoboOk);
}

/** Handle of the first descendant element named @p name (invalid when none). */
static HtmlNodeHandle AtFind(HtmlDocumentId id,
                                       const char *name)
{
    HtmlNodeHandle root = HtmlDocumentRoot(id);
    HtmlNodeHandle found;

    HtmlNodeHandleClear(&found);
    if (HtmlNodeFindElement(&root, name, &found, NULL) != RoboOk) {
        HtmlNodeHandleClear(&found);
    }
    return found;
}

/** Attribute count of the first element named @p name; -1 when absent. */
static int AtCount(HtmlDocumentId id, const char *name)
{
    HtmlNodeHandle e = AtFind(id, name);
    size_t count = 0u;

    if (!HtmlElementIsElement(&e)) {
        return -1;
    }
    if (HtmlElementAttributeCount(&e, &count, NULL) != RoboOk) {
        return -1;
    }
    return (int)count;
}

/** Copy attribute @p attrName of element @p elemName into @p out.
 *  @return 1 when the attribute is present (the value may be empty). */
static int AtValue(HtmlDocumentId id,
                    const char *elemName,
                    const char *attrName,
                    char *out,
                    size_t cap)
{
    HtmlNodeHandle e = AtFind(id, elemName);
    HtmlAttribute attr;
    size_t n;

    out[0] = '\0';
    if (!HtmlElementIsElement(&e)) {
        return 0;
    }
    if (HtmlElementFindAttribute(&e, attrName, &attr, NULL) !=
        RoboOk) {
        return 0;
    }
    n = attr.valueLength;
    if (n >= cap) {
        n = cap - 1u;
    }
    if (n > 0u) {
        memcpy(out, attr.value, n);
    }
    out[n] = '\0';
    return 1;
}

/** Copy attribute @p index of element @p elemName; "" when out of range. */
static void AtAttrAt(HtmlDocumentId id,
                       const char *elemName,
                       size_t index,
                       char *nameOut,
                       size_t nameCap,
                       char *valueOut,
                       size_t valueCap)
{
    HtmlNodeHandle e = AtFind(id, elemName);
    HtmlAttribute attr;
    size_t n;

    nameOut[0] = '\0';
    valueOut[0] = '\0';
    if (!HtmlElementIsElement(&e)) {
        return;
    }
    if (HtmlElementAttributeAt(&e, index, &attr, NULL) != RoboOk) {
        return;
    }
    n = attr.nameLength;
    if (n >= nameCap) {
        n = nameCap - 1u;
    }
    if (n > 0u) {
        memcpy(nameOut, attr.name, n);
    }
    nameOut[n] = '\0';

    n = attr.valueLength;
    if (n >= valueCap) {
        n = valueCap - 1u;
    }
    if (n > 0u) {
        memcpy(valueOut, attr.value, n);
    }
    valueOut[n] = '\0';
}

/* ------------------------------------------------------------------ */
/* The type itself                                                     */
/* ------------------------------------------------------------------ */

static void TestAtTypeBasics(void)
{
    AtFixtureBegin();

    HtmlAttribute attr;

    AtFixtureBegin();

    HtmlAttributeInit(&attr);
    RoboCheckStrEq(attr.name, "");
    RoboCheckStrEq(attr.value, "");
    RoboCheckEqInt((int)attr.nameLength, 0);
    RoboCheckEqInt((int)attr.valueLength, 0);
    RoboCheckEqInt((int)attr.namespaceId, (int)RoboHtmlNamespaceNone);

    /* init is the "no attribute" state, and it must tolerate NULL. */
    HtmlAttributeInit(NULL);

    AtFixtureEnd();
}

static void TestAtTypeReadsLengths(void)
{
    AtFixtureBegin();

    HtmlDocumentId id = AtDoc();
    HtmlNodeHandle e;
    HtmlAttribute attr;

    AtBuild(id, "<!DOCTYPE html><html><body><div id=\"main\"></div></body></html>");
    e = AtFind(id, "div");
    RoboCheck(HtmlElementIsElement(&e));

    RoboCheckEqInt(HtmlElementAttributeAt(&e, 0u, &attr, NULL),
                      RoboOk);
    RoboCheckStrEq(attr.name, "id");
    RoboCheckEqInt((int)attr.nameLength, 2);
    RoboCheckStrEq(attr.value, "main");
    RoboCheckEqInt((int)attr.valueLength, 4);
    /* HTML attributes have no namespace; the field exists so the foreign
     * content phase fills it in rather than changing the type. */
    RoboCheckEqInt((int)attr.namespaceId, (int)RoboHtmlNamespaceNone);

    /* Out of range zeroes the out-struct rather than leaving it stale. */
    RoboCheckEqInt(HtmlElementAttributeAt(&e, 1u, &attr, NULL),
                      RoboNotFound);
    RoboCheckStrEq(attr.name, "");
    RoboCheckStrEq(attr.value, "");
    RoboCheckEqInt((int)attr.nameLength, 0);

    RoboCheckEqInt(HtmlElementAttributeAt(&e, 0u, NULL, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(HtmlElementAttributeAt(NULL, 0u, &attr, NULL),
                      RoboInvalidArgument);

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    AtFixtureEnd();
}

/* ------------------------------------------------------------------ */
/* Parser integration                                                  */
/* ------------------------------------------------------------------ */

static void TestAtParseIntegration(void)
{
    AtFixtureBegin();

    HtmlDocumentId id = AtDoc();
    char value[64];
    char name[32];

    AtBuild(id,
             "<!DOCTYPE html><html><body>"
             "<div id=\"main\" class=\"container\"></div>"
             "</body></html>");

    RoboCheckEqInt(AtCount(id, "div"), 2);
    AtAttrAt(id, "div", 0u, name, sizeof(name), value, sizeof(value));
    RoboCheckStrEq(name, "id");
    RoboCheckStrEq(value, "main");
    AtAttrAt(id, "div", 1u, name, sizeof(name), value, sizeof(value));
    RoboCheckStrEq(name, "class");
    RoboCheckStrEq(value, "container");

    /* An element with no attributes has an empty, working attribute set. */
    RoboCheckEqInt(AtCount(id, "body"), 0);
    RoboCheckEqInt(AtCount(id, "html"), 0);
    {
        HtmlNodeHandle body = AtFind(id, "body");

        RoboCheckEqInt(HtmlElementHasAttribute(&body, "id"), 0);
        RoboCheck(HtmlElementAttributeValue(&body, "id", NULL) == NULL);
    }

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    AtFixtureEnd();
}

static void TestAtParseOrderIsSourceOrder(void)
{
    AtFixtureBegin();

    HtmlDocumentId id = AtDoc();
    char name[32];
    char value[32];

    AtBuild(id,
             "<!DOCTYPE html><html><body>"
             "<div id=\"main\" class=\"panel\" data-x=\"1\"></div>"
             "</body></html>");

    RoboCheckEqInt(AtCount(id, "div"), 3);
    AtAttrAt(id, "div", 0u, name, sizeof(name), value, sizeof(value));
    RoboCheckStrEq(name, "id");
    RoboCheckStrEq(value, "main");
    AtAttrAt(id, "div", 1u, name, sizeof(name), value, sizeof(value));
    RoboCheckStrEq(name, "class");
    RoboCheckStrEq(value, "panel");
    AtAttrAt(id, "div", 2u, name, sizeof(name), value, sizeof(value));
    RoboCheckStrEq(name, "data-x");
    RoboCheckStrEq(value, "1");

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    AtFixtureEnd();
}

static void TestAtPresenceVersusEmpty(void)
{
    AtFixtureBegin();

    HtmlDocumentId id = AtDoc();
    HtmlNodeHandle input;
    HtmlNodeHandle body;
    HtmlNodeHandle second;
    const char *value = NULL;
    size_t length = 0u;
    size_t children = 0u;
    size_t i;
    size_t seen = 0u;
    char dump[1024];

    /* `<input disabled>` and `<input disabled="">` must both be present with
     * an empty value, and both distinct from absent. */
    AtBuild(id,
             "<!DOCTYPE html><html><body>"
             "<input disabled>"
             "<input checked=\"\">"
             "</body></html>");

    input = AtFind(id, "input");
    RoboCheck(HtmlElementIsElement(&input));
    RoboCheckEqInt(HtmlElementHasAttribute(&input, "disabled"), 1);
    RoboCheckEqInt(HtmlElementHasAttribute(&input, "checked"), 0);
    value = HtmlElementAttributeValue(&input, "disabled", &length);
    RoboCheck(value != NULL);
    RoboCheckStrEq(value, "");
    RoboCheckEqInt((int)length, 0);
    RoboCheckEqInt(HtmlElementAttributeCount(&input, &length, NULL),
                      RoboOk);
    RoboCheckEqInt((int)length, 1);

    /* The second <input> carries the explicitly empty value. */
    body = AtFind(id, "body");
    HtmlNodeHandleClear(&second);
    RoboCheckEqInt(HtmlNodeChildCount(&body, &children, NULL),
                      RoboOk);
    for (i = 0u; i < children; i++) {
        HtmlNodeHandle child;

        if (HtmlNodeChildAt(&body, i, &child, NULL) != RoboOk) {
            break;
        }
        if (!HtmlElementIsNamed(&child, "input")) {
            continue;
        }
        if (seen == 1u) {
            second = child;
            break;
        }
        seen++;
    }
    RoboCheck(HtmlElementIsElement(&second));
    RoboCheckEqInt(HtmlElementHasAttribute(&second, "checked"), 1);
    RoboCheckEqInt(HtmlElementHasAttribute(&second, "disabled"), 0);
    value = HtmlElementAttributeValue(&second, "checked", &length);
    RoboCheck(value != NULL);
    RoboCheckEqInt((int)length, 0);

    /* The dump shows attributes, and an empty value is visible as "". */
    RoboCheckEqInt(HtmlTreeDumpToBuffer(id, dump, sizeof(dump),
                                                    NULL, NULL),
                      RoboOk);
    RoboCheck(strstr(dump, "input disabled=\"\"") != NULL);
    RoboCheck(strstr(dump, "input checked=\"\"") != NULL);

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    AtFixtureEnd();
}

static void TestAtDuplicatesPreserved(void)
{
    AtFixtureBegin();

    HtmlDocumentId id = AtDoc();
    char value[32];
    char name[32];

    /* The parser does not fold repeated names and the model does not invent
     * a different answer: both records are stored, in source order, and
     * lookup answers the first. */
    AtBuild(id,
             "<!DOCTYPE html><html><body>"
             "<div id=\"one\" id=\"two\"></div>"
             "</body></html>");

    RoboCheckEqInt(AtCount(id, "div"), 2);
    AtAttrAt(id, "div", 0u, name, sizeof(name), value, sizeof(value));
    RoboCheckStrEq(name, "id");
    RoboCheckStrEq(value, "one");
    AtAttrAt(id, "div", 1u, name, sizeof(name), value, sizeof(value));
    RoboCheckStrEq(name, "id");
    RoboCheckStrEq(value, "two");
    RoboCheck(AtValue(id, "div", "id", value, sizeof(value)));
    RoboCheckStrEq(value, "one");

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    AtFixtureEnd();
}

/* ------------------------------------------------------------------ */
/* Lookup                                                              */
/* ------------------------------------------------------------------ */

static void TestAtLookup(void)
{
    AtFixtureBegin();

    HtmlDocumentId id = AtDoc();
    HtmlNodeHandle div;
    HtmlNodeHandle root;
    HtmlAttribute attr;
    size_t length = 0u;

    AtBuild(id,
             "<!DOCTYPE html><html><body>"
             "<div id=\"main\" data-value=\"hello\" aria-label=\"label\"></div>"
             "</body></html>");
    div = AtFind(id, "div");
    RoboCheck(HtmlElementIsElement(&div));

    /* Names are stored lowercase; lookup is ASCII case-insensitive, so a
     * caller may pass any case and every path agrees. */
    RoboCheckEqInt(HtmlElementFindAttribute(&div, "ID", &attr, NULL),
                      RoboOk);
    RoboCheckStrEq(attr.name, "id");
    RoboCheckStrEq(attr.value, "main");

    RoboCheckEqInt(
        HtmlElementFindAttribute(&div, "DATA-VALUE", &attr, NULL),
        RoboOk);
    RoboCheckStrEq(attr.value, "hello");

    RoboCheckEqInt(HtmlElementFindAttribute(&div, "missing", &attr,
                                                       NULL),
                      RoboNotFound);
    RoboCheckStrEq(attr.value, "");

    RoboCheckEqInt(HtmlElementHasAttribute(&div, "ARIA-LABEL"), 1);
    RoboCheckEqInt(HtmlElementHasAttribute(&div, "aria-labelledby"),
                      0);

    RoboCheck(HtmlElementAttributeValue(&div, "data-value", &length) !=
               NULL);
    RoboCheckEqInt((int)length, 5);
    RoboCheck(HtmlElementAttributeValue(&div, "nope", NULL) == NULL);

    /* NULL arguments are refused, not obeyed. */
    RoboCheckEqInt(HtmlElementFindAttribute(&div, NULL, &attr, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(HtmlElementFindAttribute(NULL, "id", &attr, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(HtmlElementHasAttribute(&div, NULL), 0);

    /* A non-element node is not an element with an empty attribute set. */
    root = HtmlDocumentRoot(id);
    RoboCheckEqInt(HtmlElementAttributeCount(&root, &length, NULL),
                      RoboNotFound);
    RoboCheckEqInt(HtmlElementHasAttribute(&root, "id"), 0);
    RoboCheck(HtmlElementAttributeValue(&root, "id", NULL) == NULL);

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    AtFixtureEnd();
}

/* ------------------------------------------------------------------ */
/* Mutation                                                            */
/* ------------------------------------------------------------------ */

static void TestAtMutationSet(void)
{
    AtFixtureBegin();

    HtmlDocumentId id = AtDoc();
    HtmlNodeHandle div;
    HtmlNodeHandle parent;
    char name[32];
    char value[64];
    HtmlNodeId nodeId;
    uint32_t generation;

    AtBuild(id,
             "<!DOCTYPE html><html><body>"
             "<div id=\"one\" class=\"panel\"></div>"
             "</body></html>");
    div = AtFind(id, "div");
    nodeId = div.nodeId;
    generation = div.generation;

    /* Update in place: the record is rewritten, not appended, and it keeps
     * its position. */
    RoboCheckEqInt(HtmlElementSetAttribute(&div, "id", "main", NULL),
                      RoboOk);
    RoboCheckEqInt(AtCount(id, "div"), 2);
    RoboCheck(AtValue(id, "div", "id", value, sizeof(value)));
    RoboCheckStrEq(value, "main");
    AtAttrAt(id, "div", 0u, name, sizeof(name), value, sizeof(value));
    RoboCheckStrEq(name, "id");

    /* A name that is not there is appended, as DOM setAttribute does. */
    RoboCheckEqInt(HtmlElementSetAttribute(&div, "title", "hi", NULL),
                      RoboOk);
    RoboCheckEqInt(AtCount(id, "div"), 3);
    AtAttrAt(id, "div", 2u, name, sizeof(name), value, sizeof(value));
    RoboCheckStrEq(name, "title");
    RoboCheckStrEq(value, "hi");

    /* The name is normalized on the way in, so it is found in any case. */
    RoboCheckEqInt(HtmlElementSetAttribute(&div, "DATA-K", "v", NULL),
                      RoboOk);
    AtAttrAt(id, "div", 3u, name, sizeof(name), value, sizeof(value));
    RoboCheckStrEq(name, "data-k");
    RoboCheckEqInt(HtmlElementHasAttribute(&div, "Data-K"), 1);

    /* Setting presence with an empty value is how a boolean attribute is
     * written: NULL means the empty string here, not "remove". */
    RoboCheckEqInt(HtmlElementSetAttribute(&div, "disabled", NULL,
                                                      NULL),
                      RoboOk);
    RoboCheckEqInt(HtmlElementHasAttribute(&div, "disabled"), 1);
    RoboCheckStrEq(HtmlElementAttributeValue(&div, "disabled", NULL),
                      "");

    /* Rewriting the payload does not touch identity or the element's place
     * in the tree: same node id, same generation, still under <body>. */
    RoboCheckEqInt((int)div.nodeId, (int)nodeId);
    RoboCheckEqInt((int)div.generation, (int)generation);
    RoboCheckEqInt(HtmlElementIsNamed(&div, "div"), 1);
    RoboCheckEqInt(HtmlElementParent(&div, &parent, NULL), RoboOk);
    RoboCheckEqInt(HtmlElementIsNamed(&parent, "body"), 1);
    RoboCheckEqInt(HtmlElementHasAttribute(&div, "id"), 1);
    RoboCheckEqInt(AtCount(id, "body"), 0);

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    AtFixtureEnd();
}

static void TestAtMutationRemove(void)
{
    AtFixtureBegin();

    HtmlDocumentId id = AtDoc();
    HtmlNodeHandle div;
    char name[32];
    char value[32];

    AtBuild(id,
             "<!DOCTYPE html><html><body>"
             "<div id=\"main\" class=\"panel\" data-x=\"1\"></div>"
             "</body></html>");
    div = AtFind(id, "div");

    RoboCheckEqInt(HtmlElementRemoveAttribute(&div, "class", NULL),
                      RoboOk);
    RoboCheckEqInt(AtCount(id, "div"), 2);
    RoboCheckEqInt(HtmlElementHasAttribute(&div, "class"), 0);
    RoboCheck(HtmlElementAttributeValue(&div, "class", NULL) == NULL);
    /* The rest keeps its relative order. */
    AtAttrAt(id, "div", 0u, name, sizeof(name), value, sizeof(value));
    RoboCheckStrEq(name, "id");
    AtAttrAt(id, "div", 1u, name, sizeof(name), value, sizeof(value));
    RoboCheckStrEq(name, "data-x");

    /* Removing something that is not there is a successful no-op, matching
     * DOM removeAttribute — it is not a presence test. */
    RoboCheckEqInt(HtmlElementRemoveAttribute(&div, "absent", NULL),
                      RoboOk);
    RoboCheckEqInt(AtCount(id, "div"), 2);

    /* Removal is case-insensitive like every other name operation. */
    RoboCheckEqInt(HtmlElementRemoveAttribute(&div, "DATA-X", NULL),
                      RoboOk);
    RoboCheckEqInt(AtCount(id, "div"), 1);

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    AtFixtureEnd();
}

static void TestAtMutationSetCollapsesDuplicates(void)
{
    AtFixtureBegin();

    HtmlDocumentId id = AtDoc();
    HtmlNodeHandle div;
    char name[32];
    char value[32];

    AtBuild(id,
             "<!DOCTYPE html><html><body>"
             "<div id=\"one\" class=\"panel\" id=\"two\"></div>"
             "</body></html>");
    div = AtFind(id, "div");
    RoboCheckEqInt(AtCount(id, "div"), 3);

    /* set collapses every match to one, at the first match's position. */
    RoboCheckEqInt(HtmlElementSetAttribute(&div, "id", "final", NULL),
                      RoboOk);
    RoboCheckEqInt(AtCount(id, "div"), 2);
    AtAttrAt(id, "div", 0u, name, sizeof(name), value, sizeof(value));
    RoboCheckStrEq(name, "id");
    RoboCheckStrEq(value, "final");
    AtAttrAt(id, "div", 1u, name, sizeof(name), value, sizeof(value));
    RoboCheckStrEq(name, "class");

    /* remove drops every match, not just the first. */
    RoboCheckEqInt(HtmlElementAddAttribute(&div, "id", "again", NULL),
                      RoboOk);
    RoboCheckEqInt(AtCount(id, "div"), 3);
    RoboCheckEqInt(HtmlElementRemoveAttribute(&div, "id", NULL),
                      RoboOk);
    RoboCheckEqInt(AtCount(id, "div"), 1);
    AtAttrAt(id, "div", 0u, name, sizeof(name), value, sizeof(value));
    RoboCheckStrEq(name, "class");

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    AtFixtureEnd();
}

static void TestAtMutationAddAppends(void)
{
    AtFixtureBegin();

    HtmlDocumentId id = AtDoc();
    HtmlNodeHandle div;
    char name[32];
    char value[32];

    AtBuild(id,
             "<!DOCTYPE html><html><body><div id=\"main\"></div></body></html>");
    div = AtFind(id, "div");

    /* add is the parser's verb: it appends and never collapses, which is the
     * only way a duplicate name can enter the model. */
    RoboCheckEqInt(HtmlElementAddAttribute(&div, "id", "second", NULL),
                      RoboOk);
    RoboCheckEqInt(AtCount(id, "div"), 2);
    AtAttrAt(id, "div", 1u, name, sizeof(name), value, sizeof(value));
    RoboCheckStrEq(value, "second");
    /* Lookup still answers the first record. */
    RoboCheck(AtValue(id, "div", "id", value, sizeof(value)));
    RoboCheckStrEq(value, "main");

    RoboCheckEqInt(HtmlElementAddAttribute(&div, "ID", "third", NULL),
                      RoboOk);
    RoboCheckEqInt(AtCount(id, "div"), 3);
    AtAttrAt(id, "div", 2u, name, sizeof(name), value, sizeof(value));
    RoboCheckStrEq(name, "id");
    RoboCheckStrEq(value, "third");

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    AtFixtureEnd();
}

static void TestAtMutationValidation(void)
{
    AtFixtureBegin();

    HtmlDocumentId id = AtDoc();
    HtmlNodeHandle div;
    HtmlNodeHandle root;
    HtmlNodeHandle ghost;
    char longName[120];
    size_t i;

    AtBuild(id,
             "<!DOCTYPE html><html><body><div id=\"main\"></div></body></html>");
    div = AtFind(id, "div");

    RoboCheckEqInt(HtmlElementSetAttribute(NULL, "id", "x", NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(HtmlElementSetAttribute(&div, NULL, "x", NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(HtmlElementSetAttribute(&div, "", "x", NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(HtmlElementSetAttribute(&div, "a b", "x", NULL),
                      RoboParseError);
    RoboCheckEqInt(HtmlElementSetAttribute(&div, "<", "x", NULL),
                      RoboParseError);

    for (i = 0u; i < sizeof(longName) - 1u; i++) {
        longName[i] = 'a';
    }
    longName[sizeof(longName) - 1u] = '\0';
    RoboCheckEqInt(
        HtmlElementSetAttribute(&div, longName, "x", NULL),
        RoboNotSupported);

    /* A refused mutation leaves the element exactly as it was. */
    RoboCheckEqInt(AtCount(id, "div"), 1);
    RoboCheckEqInt(HtmlElementHasAttribute(&div, "id"), 1);

    /* A node that is not an element cannot carry attributes. */
    root = HtmlDocumentRoot(id);
    RoboCheckEqInt(HtmlElementAddAttribute(&root, "id", "x", NULL),
                      RoboNotFound);

    /* Nor can a stale handle. */
    ghost = div;
    ghost.generation = (uint32_t)(ghost.generation + 7u);
    RoboCheckEqInt(HtmlElementAddAttribute(&ghost, "id", "x", NULL),
                      RoboNotFound);

    /* Nor a handle whose document is gone. */
    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    RoboCheckEqInt(HtmlElementAddAttribute(&div, "id", "x", NULL),
                      RoboNotFound);

    AtFixtureEnd();
}

static void TestAtMutationAfterFinalize(void)
{
    AtFixtureBegin();

    HtmlDocumentId id = AtDoc();
    HtmlNodeHandle div;
    char value[32];

    AtBuild(id,
             "<!DOCTYPE html><html><body><div id=\"main\"></div></body></html>");
    /* A completed build finalizes *tree construction*. Attributes are a
     * different operation — the DOM seam stays open, which is what makes
     * this model usable by the later DOM phase rather than parser-only. */
    RoboCheckEqInt(HtmlDocumentIsFinalized(id), 1);
    RoboCheckEqInt(HtmlDocumentAllowsConstruction(id), 0);
    div = AtFind(id, "div");

    RoboCheckEqInt(HtmlElementSetAttribute(&div, "data-live", "yes",
                                                      NULL),
                      RoboOk);
    RoboCheckEqInt(HtmlElementHasAttribute(&div, "data-live"), 1);
    RoboCheck(AtValue(id, "div", "data-live", value, sizeof(value)));
    RoboCheckStrEq(value, "yes");
    RoboCheckEqInt(HtmlElementRemoveAttribute(&div, "data-live",
                                                         NULL),
                      RoboOk);
    RoboCheckEqInt(HtmlElementHasAttribute(&div, "data-live"), 0);

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    AtFixtureEnd();
}

static void TestAtMutationMany(void)
{
    AtFixtureBegin();

    HtmlDocumentId id = AtDoc();
    HtmlNodeHandle div;
    char name[16];
    char value[16];
    int i;
    int refusedAt = -1;

    AtBuild(id, "<!DOCTYPE html><html><body><div></div></body></html>");
    div = AtFind(id, "div");
    RoboCheckEqInt(AtCount(id, "div"), 0);

    /* An element's attribute table is bounded, and the bound must be
     * enforced by refusing the write rather than by corrupting the element. */
    for (i = 0; i < 100; i++) {
        Result r;

        (void)snprintf(name, sizeof(name), "a%d", i);
        (void)snprintf(value, sizeof(value), "v%d", i);
        r = HtmlElementAddAttribute(&div, name, value, NULL);
        if (r != RoboOk) {
            RoboCheckEqInt(r, RoboNotSupported);
            refusedAt = i;
            break;
        }
    }
    RoboCheck(refusedAt > 0);
    RoboCheckEqInt(AtCount(id, "div"), refusedAt);
    RoboCheckEqInt(AtCount(id, "div"), refusedAt);

    /* The refused name really is absent, and an *update* still works at the
     * limit: reaching the cap must not make the element read-only. */
    (void)snprintf(name, sizeof(name), "a%d", refusedAt);
    RoboCheckEqInt(HtmlElementHasAttribute(&div, name), 0);
    RoboCheckEqInt(HtmlElementSetAttribute(&div, "a0", "updated",
                                                      NULL),
                      RoboOk);
    RoboCheckEqInt(AtCount(id, "div"), refusedAt);
    RoboCheck(AtValue(id, "div", "a0", value, sizeof(value)));
    RoboCheckStrEq(value, "updated");
    /* A remove at the limit frees a slot for the next add. */
    RoboCheckEqInt(HtmlElementRemoveAttribute(&div, "a1", NULL),
                      RoboOk);
    RoboCheckEqInt(AtCount(id, "div"), refusedAt - 1);
    RoboCheckEqInt(HtmlElementAddAttribute(&div, name, "late", NULL),
                      RoboOk);
    RoboCheckEqInt(AtCount(id, "div"), refusedAt);

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    AtFixtureEnd();
}

/* ------------------------------------------------------------------ */
/* Unicode                                                             */
/* ------------------------------------------------------------------ */

static void TestAtUnicodeValues(void)
{
    AtFixtureBegin();

    HtmlDocumentId id = AtDoc();
    HtmlNodeHandle div;
    char value[128];

    AtBuild(id,
             "<!DOCTYPE html><html><body>"
             "<div title=\"\xd9\x85\xd8\xb1\xd8\xad\xd8\xa8\xd8\xa7\" "
             "data-name=\"\xd9\x81\xd8\xa7\xd8\xb1\xd8\xb3\xdb\x8c\" "
             "aria-label=\"\xd8\xa7\xd8\xb1\xd8\xaf\xd9\x88\" "
             "data-text=\"\xf0\x9f\x98\x80\" "
             "data-cjk=\"\xe4\xb8\xad\xe6\x96\x87\" "
             "data-jp=\"\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e\"></div>"
             "</body></html>");

    RoboCheckEqInt(AtCount(id, "div"), 6);

    /* Arabic "marhaba": byte-for-byte, with no normalization applied. */
    RoboCheck(AtValue(id, "div", "title", value, sizeof(value)));
    RoboCheckStrEq(value, "\xd9\x85\xd8\xb1\xd8\xad\xd8\xa8\xd8\xa7");
    RoboCheck(AtValue(id, "div", "data-name", value, sizeof(value)));
    RoboCheckStrEq(value, "\xd9\x81\xd8\xa7\xd8\xb1\xd8\xb3\xdb\x8c");
    RoboCheck(AtValue(id, "div", "aria-label", value, sizeof(value)));
    RoboCheckStrEq(value, "\xd8\xa7\xd8\xb1\xd8\xaf\xd9\x88");
    /* A 4-byte emoji survives the round trip whole. */
    RoboCheck(AtValue(id, "div", "data-text", value, sizeof(value)));
    RoboCheckStrEq(value, "\xf0\x9f\x98\x80");
    RoboCheck(AtValue(id, "div", "data-cjk", value, sizeof(value)));
    RoboCheckStrEq(value, "\xe4\xb8\xad\xe6\x96\x87");
    RoboCheck(AtValue(id, "div", "data-jp", value, sizeof(value)));
    RoboCheckStrEq(value, "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e");

    /* Bytes written through the DOM seam ride the same encoder. */
    div = AtFind(id, "div");
    RoboCheckEqInt(
        HtmlElementSetAttribute(&div, "data-extra",
                                        "\xf0\x9f\x8e\x89", NULL),
        RoboOk);
    RoboCheck(AtValue(id, "div", "data-extra", value, sizeof(value)));
    RoboCheckStrEq(value, "\xf0\x9f\x8e\x89");
    /* The length is bytes, not characters — a 4-byte emoji is 4. */
    {
        size_t length = 0u;

        (void)HtmlElementAttributeValue(&div, "data-extra", &length);
        RoboCheckEqInt((int)length, 4);
    }

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    AtFixtureEnd();
}

static void TestAtUnicodeNames(void)
{
    AtFixtureBegin();

    HtmlDocumentId id = AtDoc();
    HtmlNodeHandle div;
    HtmlAttribute attr;
    size_t count = 0u;

    /* A non-ASCII attribute name: the tokenizer keeps the UTF-8 bytes and
     * the model stores and finds them unchanged. Bytes outside A-Z are never
     * case-folded, so the comparison stays byte-exact. */
    AtBuild(id,
             "<!DOCTYPE html><html><body>"
             "<div \xd0\xb4\xd0\xb0\xd1\x82\xd0\xb0=\"x\"></div>"
             "</body></html>");

    div = AtFind(id, "div");
    RoboCheck(HtmlElementIsElement(&div));
    RoboCheckEqInt(HtmlElementAttributeCount(&div, &count, NULL),
                      RoboOk);
    if (count == 1u) {
        RoboCheckEqInt(HtmlElementFindAttribute(
                              &div, "\xd0\xb4\xd0\xb0\xd1\x82\xd0\xb0", &attr,
                              NULL),
                          RoboOk);
        RoboCheckStrEq(attr.name, "\xd0\xb4\xd0\xb0\xd1\x82\xd0\xb0");
        RoboCheckStrEq(attr.value, "x");
    } else {
        /* If the tokenizer chose to drop it, it must be gone cleanly rather
         * than stored half-formed. */
        RoboCheckEqInt((int)count, 0);
    }

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    AtFixtureEnd();
}

/* ------------------------------------------------------------------ */
/* Malformed input                                                     */
/* ------------------------------------------------------------------ */

/** One hostile-attribute source plus what the parser should make of it. */
typedef struct malformedCase {
    const char *source;
    /**
     * 1 when a <div> element must exist afterwards. 0 only for the input
     * whose quoted value is never closed: the tokenizer consumes the rest of
     * the document as that value and no start tag is ever emitted, which is
     * the 8.2 tokenizer's documented behaviour, not an attribute-model
     * concern. Everything else about that case must still hold.
     */
    int expectsDiv;
} MalformedCase;

static void TestAtMalformedInput(void)
{
    static const MalformedCase cases[] = {
        {"<!DOCTYPE html><html><body><div></div></body></html>", 1},
        {"<!DOCTYPE html><html><body><div id></div></body></html>", 1},
        {"<!DOCTYPE html><html><body><div id=\"\"></div></body></html>", 1},
        {"<!DOCTYPE html><html><body><div id=\"x\"></div></body></html>", 1},
        {"<!DOCTYPE html><html><body><div id=\"x\" id=\"y\"></div></body></html>", 1},
        {"<!DOCTYPE html><html><body><div class></div></body></html>", 1},
        {"<!DOCTYPE html><html><body><div disabled></div></body></html>", 1},
        {"<!DOCTYPE html><html><body><div =bad></div></body></html>", 1},
        {"<!DOCTYPE html><html><body><div id = 'single'></div></body></html>", 1},
        {"<!DOCTYPE html><html><body><div a==b c= d= e></div></body></html>",
         1},
        {"<!DOCTYPE html><html><body><div/></body></html>", 1},
        {"<!DOCTYPE html><html><body><div id=\"unterminated></body></html>", 0}
    };
    size_t i;

    AtFixtureBegin();

    /* Every one of these must build, and every div that *was* created must
     * keep an attribute table that is internally consistent: a hostile or
     * broken attribute must never desynchronise the payload walk. */
    for (i = 0u; i < sizeof(cases) / sizeof(cases[0]); i++) {
        HtmlDocumentId id = AtDoc();
        HtmlNodeHandle div;
        HtmlElementView view;
        HtmlAttribute attr;
        char dump[2048];
        size_t count = 0u;
        size_t j;

        AtBuild(id, cases[i].source);
        div = AtFind(id, "div");
        RoboCheckEqInt(HtmlElementIsElement(&div),
                          cases[i].expectsDiv);
        if (cases[i].expectsDiv) {
            RoboCheckEqInt(HtmlElementViewGet(&div, &view, NULL),
                              RoboOk);
            RoboCheckEqInt(
                HtmlElementAttributeCount(&div, &count, NULL),
                RoboOk);
            RoboCheckEqInt((int)view.attributeCount, (int)count);
            for (j = 0u; j < count; j++) {
                RoboCheckEqInt(
                    HtmlElementAttributeAt(&div, j, &attr, NULL),
                    RoboOk);
                RoboCheck(attr.name != NULL);
                RoboCheck(attr.value != NULL);
                RoboCheckEqInt((int)attr.nameLength,
                                  (int)strlen(attr.name));
                RoboCheckEqInt((int)attr.valueLength,
                                  (int)strlen(attr.value));
            }
            RoboCheckEqInt(
                HtmlElementAttributeAt(&div, count, &attr, NULL),
                RoboNotFound);
        }
        /* The dump walks every stored attribute, so a malformed table would
         * surface here as garbage rather than as a clean tree. */
        RoboCheckEqInt(
            HtmlTreeDumpToBuffer(id, dump, sizeof(dump), NULL, NULL),
            RoboOk);
        RoboCheck(strstr(dump, "Document") != NULL);
        RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    }

    AtFixtureEnd();
}

/* ------------------------------------------------------------------ */
/* Debug output                                                        */
/* ------------------------------------------------------------------ */

static void TestAtTreeDumpShowsAttributes(void)
{
    AtFixtureBegin();

    HtmlDocumentId id = AtDoc();
    HtmlNodeHandle div;
    char dump[1024];
    size_t length = 0u;

    AtBuild(id,
             "<!DOCTYPE html><html><body>"
             "<div id=\"main\" class=\"container\"></div>"
             "<img src=\"a.png\">"
             "</body></html>");

    RoboCheckEqInt(HtmlTreeDumpToBuffer(id, dump, sizeof(dump),
                                                    &length, NULL),
                      RoboOk);
    RoboCheck(length > 0u);
    RoboCheck(strstr(dump, "div id=\"main\" class=\"container\"") != NULL);
    /* The pre-existing shape is unchanged: name, quote, value, optional
     * self-closing marker. The model now feeds it, it did not replace it. */
    RoboCheck(strstr(dump, "img src=\"a.png\"") != NULL);

    /* A mutation shows up in the dump, because the dump reads the model. */
    div = AtFind(id, "div");
    RoboCheckEqInt(HtmlElementSetAttribute(&div, "class", "updated",
                                                      NULL),
                      RoboOk);
    RoboCheckEqInt(HtmlTreeDumpToBuffer(id, dump, sizeof(dump),
                                                    NULL, NULL),
                      RoboOk);
    RoboCheck(strstr(dump, "class=\"updated\"") != NULL);
    RoboCheck(strstr(dump, "class=\"container\"") == NULL);

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    AtFixtureEnd();
}

/* ------------------------------------------------------------------ */
/* Ownership / lifetime                                                */
/* ------------------------------------------------------------------ */

static void TestAtOwnership(void)
{
    AtFixtureBegin();

    HtmlDocumentId id = AtDoc();
    HtmlDocumentId other;
    HtmlNodeHandle div;
    HtmlNodeHandle otherDiv;
    HtmlAttribute attr;

    AtBuild(id,
             "<!DOCTYPE html><html><body><div id=\"main\"></div></body></html>");
    div = AtFind(id, "div");

    /* Attributes live in the owning document's payload arena, so two
     * documents' elements are separate: the same name resolves inside its
     * own document only. */
    other = AtDoc();
    AtBuild(other,
             "<!DOCTYPE html><html><body><div id=\"elsewhere\"></div></body>"
             "</html>");
    otherDiv = AtFind(other, "div");

    RoboCheckEqInt(HtmlElementSetAttribute(&div, "data-owner", "a",
                                                      NULL),
                      RoboOk);
    RoboCheckEqInt(HtmlElementHasAttribute(&otherDiv, "data-owner"),
                      0);
    RoboCheckEqInt(HtmlElementSetAttribute(&otherDiv, "data-owner",
                                                      "b", NULL),
                      RoboOk);
    RoboCheckEqInt(HtmlElementFindAttribute(&div, "data-owner", &attr,
                                                       NULL),
                      RoboOk);
    RoboCheckStrEq(attr.value, "a");
    RoboCheckEqInt(HtmlElementFindAttribute(&otherDiv, "data-owner",
                                                       &attr, NULL),
                      RoboOk);
    RoboCheckStrEq(attr.value, "b");

    RoboCheckEqInt(HtmlDocumentDestroy(other, NULL), RoboOk);
    /* The other document is gone: its handles are inert and the first
     * document is untouched, so there is no shared state to dangle. */
    RoboCheckEqInt(HtmlElementHasAttribute(&otherDiv, "data-owner"),
                      0);
    RoboCheckEqInt(HtmlElementAddAttribute(&otherDiv, "x", "y",
                                                      NULL),
                      RoboNotFound);
    RoboCheckEqInt(HtmlElementHasAttribute(&div, "data-owner"), 1);

    /* Document teardown frees every attribute with the arena it lives in. */
    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    RoboCheckEqInt(HtmlElementHasAttribute(&div, "id"), 0);
    RoboCheck(HtmlElementAttributeValue(&div, "id", NULL) == NULL);
    RoboCheckEqInt(HtmlElementSetAttribute(&div, "id", "x", NULL),
                      RoboNotFound);
    RoboCheckEqInt(HtmlElementAttributeCount(&div, NULL, NULL),
                      RoboInvalidArgument);

    AtFixtureEnd();
}

static void TestAtTeardownWithManyMutations(void)
{
    AtFixtureBegin();

    HtmlDocumentId id = AtDoc();
    HtmlNodeHandle div;
    char name[16];
    char value[16];
    int i;

    AtBuild(id, "<!DOCTYPE html><html><body><div></div></body></html>");
    div = AtFind(id, "div");

    /* Rewriting the payload many times must neither corrupt the element nor
     * outlive the document: the arena is freed as a unit at teardown. */
    for (i = 0; i < 40; i++) {
        (void)snprintf(name, sizeof(name), "k%d", i);
        (void)snprintf(value, sizeof(value), "v%d", i);
        RoboCheckEqInt(HtmlElementAddAttribute(&div, name, value,
                                                          NULL),
                          RoboOk);
        RoboCheckEqInt(HtmlElementSetAttribute(&div, name, value,
                                                          NULL),
                          RoboOk);
        if (i % 3 == 0) {
            RoboCheckEqInt(HtmlElementRemoveAttribute(&div, name,
                                                                 NULL),
                              RoboOk);
        }
    }
    RoboCheckEqInt(HtmlElementIsNamed(&div, "div"), 1);
    RoboCheckEqInt((int)AtCount(id, "div"), 26);

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    AtFixtureEnd();
}

static void TestAtRebuildClearsAttributes(void)
{
    AtFixtureBegin();

    HtmlDocumentId id = AtDoc();
    HtmlNodeHandle div;
    HtmlNodeHandle again;
    char value[32];

    AtBuild(id,
             "<!DOCTYPE html><html><body><div id=\"first\" class=\"x\"></div>"
             "</body></html>");
    div = AtFind(id, "div");
    RoboCheckEqInt(AtCount(id, "div"), 2);
    RoboCheckEqInt(HtmlElementSetAttribute(&div, "extra", "y", NULL),
                      RoboOk);
    RoboCheckEqInt(AtCount(id, "div"), 3);

    /* Re-parsing is explicit (8.4) and replaces the tree: the new element
     * carries only what the new source said. */
    AtBuild(id,
             "<!DOCTYPE html><html><body><div id=\"second\"></div></body>"
             "</html>");
    again = AtFind(id, "div");
    RoboCheck(HtmlElementIsElement(&again));
    RoboCheckEqInt(AtCount(id, "div"), 1);
    RoboCheck(AtValue(id, "div", "id", value, sizeof(value)));
    RoboCheckStrEq(value, "second");
    RoboCheckEqInt(HtmlElementHasAttribute(&again, "extra"), 0);
    RoboCheckEqInt(HtmlElementHasAttribute(&again, "class"), 0);

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    AtFixtureEnd();
}

/* ------------------------------------------------------------------ */

void TestRegisterHtmlAttribute(void)
{
    TestRegister("html.attr.type_basics", TestAtTypeBasics);
    TestRegister("html.attr.type_reads_lengths",
                       TestAtTypeReadsLengths);
    TestRegister("html.attr.parse_integration",
                       TestAtParseIntegration);
    TestRegister("html.attr.parse_order_is_source_order",
                       TestAtParseOrderIsSourceOrder);
    TestRegister("html.attr.presence_versus_empty",
                       TestAtPresenceVersusEmpty);
    TestRegister("html.attr.duplicates_preserved",
                       TestAtDuplicatesPreserved);
    TestRegister("html.attr.lookup", TestAtLookup);
    TestRegister("html.attr.mutation_set", TestAtMutationSet);
    TestRegister("html.attr.mutation_remove", TestAtMutationRemove);
    TestRegister("html.attr.mutation_set_collapses_duplicates",
                       TestAtMutationSetCollapsesDuplicates);
    TestRegister("html.attr.mutation_add_appends",
                       TestAtMutationAddAppends);
    TestRegister("html.attr.mutation_validation",
                       TestAtMutationValidation);
    TestRegister("html.attr.mutation_after_finalize",
                       TestAtMutationAfterFinalize);
    TestRegister("html.attr.mutation_many", TestAtMutationMany);
    TestRegister("html.attr.unicode_values", TestAtUnicodeValues);
    TestRegister("html.attr.unicode_names", TestAtUnicodeNames);
    TestRegister("html.attr.malformed_input", TestAtMalformedInput);
    TestRegister("html.attr.tree_dump_shows_attributes",
                       TestAtTreeDumpShowsAttributes);
    TestRegister("html.attr.ownership", TestAtOwnership);
    TestRegister("html.attr.teardown_with_many_mutations",
                       TestAtTeardownWithManyMutations);
    TestRegister("html.attr.rebuild_clears_attributes",
                       TestAtRebuildClearsAttributes);
}
