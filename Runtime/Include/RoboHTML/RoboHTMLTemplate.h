/*
 * RoboHTML templates (Phase 17.7) — `<template>` and its inert content.
 *
 * A template is an element whose children are *not* its children. That single
 * sentence is the whole phase, and the reason it needs a module:
 *
 *   <template><p class="row">One</p></template>
 *        │
 *        ├── the element       lives in the document tree; renders nothing,
 *        │                     contributes no layout, is looked up normally
 *        │
 *        └── template.content  a DocumentFragment that is a *root*: it has no
 *              │               parent, so nothing walks to it from the
 *              └── children    document, nothing styles it, nothing renders it
 *
 * ## Inert, and provably so
 *
 * The content fragment has **no parent**, which is the one property that makes
 * its subtree unreachable from the document root. So "inert" is not a flag
 * this module maintains and a caller has to trust: it is the tree's own shape,
 * and `HtmlElementTemplateIsInert` answers it by asking the tree
 * (`HtmlNodeContains` from the root). Document lookup (`getElementById`,
 * traversal, CSS matching, layout) walks from the root, so the same fact is
 * what keeps a template's contents out of every one of those answers.
 *
 * ## The parser adopts, and that is a post-pass not a mode
 *
 * Phase 8's tree builder nests a `<template>`'s children under the element as
 * it parses, because the builder has no "in template" insertion mode. Rather
 * than add one — a second parsing algorithm to keep in step with the first —
 * `HtmlBuildTree` ends by calling `HtmlDocumentAdoptTemplates`, which moves each
 * template element's children into a fresh fragment and records it.
 *
 * The observable result is the standard one: after a parse, `<template>` has no
 * light children and `template.content` holds them. The move is a real
 * reparent (remove, then append), so no node ever has two parents, and it
 * happens *after* the tree is complete, which is why no insertion-mode
 * behaviour changes and no existing document parses differently.
 *
 * A document created through the DOM rather than the parser is not adopted
 * automatically — a caller that appended children to a template directly can
 * call the same door. This module is honest that the invariant is one the
 * *parse* guarantees rather than one the tree can enforce for every writer: a
 * template element is a container like any other to `HtmlNodeAppendChild`, and
 * `HtmlElementTemplateContent` does not steal children behind the caller's
 * back.
 *
 * ## Cloning is the use, and insertion is the one operation it needs
 *
 * `content.cloneNode(true)` already works (17.1) because the fragment is an
 * ordinary node. What was missing is what a caller then does with it: a cloned
 * fragment's children already have a parent, so appending them one by one is
 * refused. The DOM's answer — `parent.appendChild(fragment)` inserts the
 * fragment's children — is `HtmlNodeAppendFragmentChildren` in
 * `RoboHTMLMutation.h`, and it is what makes "clone, then insert" one call
 * rather than a loop the caller has to write.
 *
 * ## What this is NOT
 *
 * No declarative Shadow DOM (`<template shadowrootmode>` is 0 in the
 * capability table), no "in template" insertion mode, no template *ownership*
 * of the nodes a caller inserts, no slotting, no `content` getter on a JS
 * wrapper object, no script or resource execution — the inert fragment is
 * exactly the mechanism that keeps a template's markup from running, and
 * RoboHTML runs no script at all. Style/layout invalidation for a cloned
 * insert is the mutation's own (17.1's), not a second path.
 */

#ifndef ROBO_HTML_TEMPLATE_H
#define ROBO_HTML_TEMPLATE_H

#include "RoboBase/RoboError.h"
#include "RoboHTML/RoboHTMLDiagnostics.h"
#include "RoboHTML/RoboHTMLNode.h"
#include "RoboHTML/RoboHTMLTypes.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * `template.content` — the element's content fragment, created on first ask.
 *
 * @p out is borrowed: the fragment is owned by the document (it is a real
 * registered node and stays valid for the document's life). It always has no
 * parent — that is what makes it content rather than a child.
 *
 * @return RoboOk; RoboInvalidArgument (NULL argument); RoboNotFound (not a
 *         live element, or its document is gone); RoboNotSupported (the
 *         element is not a `<template>`); RoboOutOfMemory.
 */
Result HtmlElementTemplateContent(
    const HtmlNodeHandle *element,
    HtmlNodeHandle *out,
    HtmlDiag *diag);

/**
 * Replace `element`'s content fragment with @p fragment.
 *
 * @p fragment must be a fragment of the *same* document that currently has no
 * parent, so "the content of a template" cannot also be a subtree of the
 * document. The fragment previously recorded (if any) keeps its nodes alive
 * but is no longer this element's content; nothing is destroyed, because a
 * caller may still hold it.
 *
 * @return RoboOk; RoboInvalidArgument (NULL argument, or a non-fragment);
 *         RoboNotFound (element or fragment does not resolve);
 *         RoboNotSupported (not a `<template>`); RoboAlreadyExists (the
 *         fragment already has a parent).
 */
Result HtmlElementTemplateSetContent(
    const HtmlNodeHandle *element,
    const HtmlNodeHandle *fragment,
    HtmlDiag *diag);

/**
 * Clone the element's content — `template.content.cloneNode(deep)` — as one
 * call, so a caller cannot forget that the clone belongs to the same document.
 *
 * @p out owns nothing but is a real node; it is a root (no parent) until the
 * caller inserts its children.
 *
 * @return RoboOk; the same structural errors as HtmlElementTemplateContent;
 *         as HtmlNodeClone otherwise.
 */
Result HtmlElementTemplateCloneContent(
    const HtmlNodeHandle *element,
    int deep,
    HtmlNodeHandle *out,
    HtmlDiag *diag);

/**
 * Non-zero in @p outInert when @p element's content is *not* reachable from
 * the document root — i.e. when its content fragment has no parent.
 *
 * Answered by asking the tree (`HtmlNodeContains` from the document root)
 * rather than by reading a flag, so a caller gets the shape that exists right
 * now. A template with no content fragment yet is inert (there is nothing to
 * reach).
 *
 * @return RoboOk; RoboInvalidArgument; RoboNotFound; RoboNotSupported.
 */
Result HtmlElementTemplateIsInert(
    const HtmlNodeHandle *element,
    int *outInert,
    HtmlDiag *diag);

/**
 * Adopt every template element in @p documentId whose children are still in
 * the light tree: create the content fragment, move the children into it, and
 * record the mapping. Idempotent — a template that already has content is left
 * alone.
 *
 * `HtmlBuildTree` runs this at the end of a parse; a caller building a
 * document through the DOM can run it after appending children to a template.
 *
 * @p outAdopted (optional) receives how many elements were adopted, which is
 * 0 for a document with no templates — the common case, and one that costs no
 * allocation.
 *
 * @return RoboOk; RoboInvalidArgument (NULL @p diag is allowed);
 *         RoboNotFound (no such live document); RoboOutOfMemory (the element
 *         list or a fragment could not be created — templates already adopted
 *         stay adopted).
 */
Result HtmlDocumentAdoptTemplates(
    HtmlDocumentId documentId,
    size_t *outAdopted,
    HtmlDiag *diag);

/**
 * What this surface actually does, field by field.
 *
 * The three zeros are the phase's stated non-goals rather than oversights:
 * there is no "in template" insertion mode (the parse adopts instead),
 * no declarative Shadow DOM, and no script or resource execution reached
 * through a template.
 */
typedef struct HtmlTemplateCapabilities {
    int contentFragment;   /* template.content */
    int parserAdoption;    /* HtmlBuildTree adopts a parsed template's children */
    int inertContent;      /* content is a parentless root, so unreachable */
    int cloning;           /* content.cloneNode(deep) */
    int fragmentInsertion; /* HtmlNodeAppendFragmentChildren */
    int setContent;        /* replace the content fragment */
    int inertnessQuery;    /* HtmlElementTemplateIsInert */
    int insertionMode;     /* 0: no "in template" mode; the parse adopts */
    int declarativeShadowDom; /* 0: no `shadowrootmode` attribute */
    int scriptExecution;   /* 0: RoboHTML runs no script, template or not */
    const char *version;   /* RoboHTML's version string */
} HtmlTemplateCapabilities;

/** Fill @p out. RoboInvalidArgument on NULL. */
Result HtmlTemplateGetCapabilities(HtmlTemplateCapabilities *out);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_HTML_TEMPLATE_H */
