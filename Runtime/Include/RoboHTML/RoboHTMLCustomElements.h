/*
 * RoboHTML custom elements (Phase 17.9) — the registry and its lifecycle.
 *
 * `customElements.define()` in a browser is two things: a name registry and a
 * set of three callbacks that fire when an instance's relationship to the
 * document changes. RoboHTML has no script engine, so the callbacks are C
 * function pointers a caller (RoboJS, or a host that speaks for it) installs —
 * and everything else about the feature is unchanged, which is why this is one
 * module and not a framework.
 *
 *   customElements.define("my-row", ...)      HtmlDocumentDefineCustomElement
 *        │
 *        ├── name validation                   HtmlCustomElementNameValidate
 *        │     lowercase, one '-', not a built-in, not reserved
 *        ├── the registry                      per document, by name
 *        └── upgrade                           every existing element with
 *              │                               that name gets its connected
 *              ▼                               callback now
 *   connectedCallback        fires when an element *becomes* connected
 *   disconnectedCallback     fires when it *stops* being connected
 *   attributeChangedCallback fires on an observed attribute's change
 *
 * ## Connected is a fact about the tree, not a flag
 *
 * The two structural callbacks are driven by the mutation layer, and "was
 * this element connected" is answered by reachability from the document root
 * (17.8's `HtmlNodeIsReachableFromDocument`) rather than by a stored bit. That
 * is what makes the awkward cases come out right for free:
 *
 *   - appending a custom element to a **detached** subtree fires nothing,
 *     because it is not connected yet;
 *   - then appending that subtree to the document fires connected **once**,
 *     for the element it contains;
 *   - detaching a subtree fires disconnected for the elements inside it, and
 *     they can be reconnected by putting it back.
 *
 * A registry entry is per **document**, not per module: two documents may
 * define the same name with different callbacks, and a document's death frees
 * exactly its own registry.
 *
 * ## Attribute changes are reported only for observed attributes
 *
 * `attributeChangedCallback` fires for a defined name and an attribute the
 * definition listed as observed — an unobserved attribute is a change nobody
 * asked about, so it is silent. Removal reports the old value with a NULL new
 * value and addition the reverse, which is the one shape a caller needs to
 * tell the three cases apart.
 *
 * ## What this is NOT
 *
 * No constructors (`new MyElement()`), no `customElements.whenDefined`,
 * no `customElements.upgrade` public door beyond the one call here, no
 * `:defined` selector, no `observedAttributes` getter on a JS class, no
 * reaction *queue* (callbacks run synchronously at the mutation, which the
 * tests pin), no `is=` customised built-ins, no scoped registries, and no
 * autonomous-vs-customised distinction. Callback *results* are ignored: the
 * tree change that triggered a callback has already happened, so a callback
 * cannot unwind it — see the header comment on the dispatch functions.
 * Invalid registrations are refused by name (`HtmlCustomElementNameValidate`),
 * never repaired.
 */

#ifndef ROBO_HTML_CUSTOM_ELEMENTS_H
#define ROBO_HTML_CUSTOM_ELEMENTS_H

#include "RoboBase/RoboError.h"
#include "RoboHTML/RoboHTMLDiagnostics.h"
#include "RoboHTML/RoboHTMLNode.h"
#include "RoboHTML/RoboHTMLTypes.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Longest observed-attribute list this registry keeps per definition. */
#define RoboHtmlCustomElementMaxObserved 16u

/**
 * The three lifecycle callbacks.
 *
 * Each may be NULL, which means "this definition does not care about that
 * one" — a distinction from a callback that does nothing, and the reason the
 * struct is copied rather than pointed at.
 *
 * @p userdata is the definition's own, passed to every callback unchanged.
 * Strings handed to `attributeChangedCallback` are borrowed for the duration
 * of the call; a callback that keeps one copies it.
 *
 * A non-RoboOk return is **ignored**. The tree change that produced the call
 * has already been applied and a notification cannot undo it, so propagating
 * the code would either pretend the mutation failed or leave a caller
 * believing a rollback happened. Rejecting *before* the change is a mutation's
 * job, and it does it.
 */
typedef struct HtmlCustomElementCallbacks {
    /** The element became connected to the document. */
    Result (*connected)
        (const HtmlNodeHandle *element, void *userdata, HtmlDiag *diag);
    /** The element stopped being connected to the document. */
    Result (*disconnected)
        (const HtmlNodeHandle *element, void *userdata, HtmlDiag *diag);
    /** An *observed* attribute changed; old/new may be NULL. */
    Result (*attributeChanged)
        (const HtmlNodeHandle *element, const char *name,
         const char *oldValue, const char *newValue,
         void *userdata, HtmlDiag *diag);
} HtmlCustomElementCallbacks;

/**
 * One registration.
 *
 * `name` is borrowed for the duration of the call and **copied** into the
 * registry, so a caller may pass a stack buffer. `observedAttributes` is
 * borrowed likewise and copied; a NULL list with a non-zero count is
 * RoboInvalidArgument, and a count above the maximum is RoboNotSupported
 * rather than a silent truncation (an attribute a caller asked to observe must
 * not quietly stop being observed).
 */
typedef struct HtmlCustomElementDefinition {
    const char *name;
    const char *const *observedAttributes;
    size_t observedCount;
    HtmlCustomElementCallbacks callbacks;
    void *userdata;
} HtmlCustomElementDefinition;

/** Fill @p out with an empty definition (all callbacks NULL). Safe on NULL. */
void HtmlCustomElementDefinitionInit(HtmlCustomElementDefinition *out);

/**
 * Validate @p name as a custom element name.
 *
 * The rules: non-empty; at most `RoboHtmlNameMax` bytes; ASCII lowercase
 * letter first; at least one '-'; only ASCII lowercase letters, digits, '-',
 * '_' and '.'; not the name of a built-in element; and not one of the reserved
 * names the HTML standard sets aside for future use.
 *
 * @return RoboOk; RoboInvalidArgument (NULL or empty);
 *         RoboParseError (a rule above failed — a bad first character, a
 *         forbidden character, or no `-`);
 *         RoboNotSupported (too long, or a name that is well-formed but this
 *         runtime will not hand out: a built-in element's name, or one the
 *         standard reserves for future use). "Malformed" and "unavailable"
 *         are different answers and a caller needs to tell them apart.
 */
Result HtmlCustomElementNameValidate(const char *name, HtmlDiag *diag);

/** Non-zero when @p name passes HtmlCustomElementNameValidate. */
int HtmlCustomElementNameIsValid(const char *name);

/**
 * Define @p definition's name in @p documentId and upgrade what is already
 * there.
 *
 * Upgrade means: every element in the document tree whose name matches gets
 * its `attributeChanged` callback for each observed attribute it carries, and
 * — when the element is connected — its `connected` callback. An element
 * created *after* the definition gets the same treatment from the mutation
 * that connects it, which is why no per-element "already upgraded" bit exists.
 *
 * @return RoboOk; RoboInvalidArgument (NULL definition / name, or a NULL
 *         observed entry); RoboNotFound (no such live document);
 *         RoboParseError (the name is not a valid custom element name);
 *         RoboNotSupported (too long, or too many observed attributes);
 *         RoboAlreadyExists (the name is already defined in this document —
 *         replacement would silently change the callbacks of every instance
 *         already upgraded); RoboOutOfMemory.
 */
Result HtmlDocumentDefineCustomElement(
    HtmlDocumentId documentId,
    const HtmlCustomElementDefinition *definition,
    HtmlDiag *diag);

/** Non-zero when @p name is defined in @p documentId. */
int HtmlDocumentCustomElementIsDefined(HtmlDocumentId documentId,
                                       const char *name);

/** Number of definitions in @p documentId; 0 for an unknown document. */
size_t HtmlDocumentCustomElementCount(HtmlDocumentId documentId);

/**
 * The stored definition for @p name, copied out.
 *
 * The callbacks and `userdata` are returned verbatim, and `name` /
 * `observedAttributes` **borrow from the registry**: they stay valid until the
 * document is destroyed, which is the same rule the registry itself follows.
 *
 * @return RoboOk; RoboInvalidArgument (NULL @p name or @p out);
 *         RoboNotFound (no such definition, or no such document).
 */
Result HtmlDocumentCustomElementDefinitionGet(
    HtmlDocumentId documentId,
    const char *name,
    HtmlCustomElementDefinition *out,
    HtmlDiag *diag);

/**
 * Non-zero in @p outIsCustom when @p element's name is defined as a custom
 * element in its own document.
 *
 * This is the spec's own definition of "custom" for an autonomous custom
 * element, and it deliberately does not consult the tag vocabulary: an
 * element whose name the tag table does not know is *unknown*, and one whose
 * name the document has defined is *custom*. The two facts are separate, and
 * an element can be the first without being the second.
 *
 * @return RoboOk; RoboInvalidArgument; RoboNotFound (not an element, or its
 *         document is gone).
 */
Result HtmlElementIsCustomElement(
    const HtmlNodeHandle *element,
    int *outIsCustom,
    HtmlDiag *diag);

/**
 * Run the upgrade steps for one element, on demand: its `attributeChanged`
 * callback for each observed attribute present, then `connected` when it is
 * connected.
 *
 * `HtmlDocumentDefineCustomElement` calls this for every matching element;
 * this door exists so a caller that just built an element by hand can upgrade
 * it without redefining the name. An element whose name is not defined is
 * RoboNotFound rather than a silent no-op, because "upgrade what?" is a
 * question the caller should be able to answer.
 *
 * @return RoboOk; RoboInvalidArgument; RoboNotFound (not an element, its
 *         document is gone, or the name is not defined).
 */
Result HtmlElementUpgradeCustomElement(
    const HtmlNodeHandle *element,
    HtmlDiag *diag);

/**
 * Upgrade every defined custom element currently in @p documentId's tree.
 *
 * @p outUpgraded (optional) receives how many elements were upgraded.
 *
 * @return RoboOk; RoboInvalidArgument (NULL @p diag is allowed);
 *         RoboNotFound (no such live document).
 */
Result HtmlDocumentUpgradeCustomElements(
    HtmlDocumentId documentId,
    size_t *outUpgraded,
    HtmlDiag *diag);

/**
 * What this surface actually does, field by field.
 */
typedef struct HtmlCustomElementCapabilities {
    int registry;             /* per-document, by name */
    int nameValidation;       /* lowercase, one '-', not built-in/reserved */
    int definedQuery;         /* name → defined */
    int definitionQuery;      /* name → definition */
    int createElement;        /* document.createElement with a custom name */
    int connectedCallback;    /* on becoming connected */
    int disconnectedCallback; /* on ceasing to be connected */
    int attributeChangedCallback; /* observed attributes only */
    int upgradeOnDefine;      /* existing elements upgrade at define time */
    int reachabilityDriven;   /* connection is the tree's shape, not a flag */
    int constructors;         /* 0: no `new MyElement()` */
    int reactionQueue;        /* 0: callbacks run synchronously at the mutation */
    int customisedBuiltins;   /* 0: no `is=` */
    int scopedRegistries;     /* 0: a registry is per document */
    int definedSelector;      /* 0: no `:defined` */
    int callbackResults;      /* 0: a callback's Result is ignored */
    const char *version;      /* RoboHTML's version string */
} HtmlCustomElementCapabilities;

/** Fill @p out. RoboInvalidArgument on NULL. */
Result HtmlCustomElementGetCapabilities(HtmlCustomElementCapabilities *out);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_HTML_CUSTOM_ELEMENTS_H */
