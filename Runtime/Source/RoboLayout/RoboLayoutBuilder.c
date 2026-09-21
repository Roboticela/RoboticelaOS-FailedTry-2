/*
 * RoboLayout's builder (Phase 10.1) — see RoboLayoutBuilder.h.
 *
 * One rule, applied recursively: an element generates a box unless its computed
 * `display` says otherwise. The recursion is bounded by the document's own node
 * count, which RoboHTML already enforces, so a malformed tree cannot nest deeper
 * than RoboHtmlMaxNodesPerDocument levels -- and the guard is written
 * explicitly anyway, because "it cannot happen" is the assumption that turns a
 * stack overflow into a mystery.
 */

#include "RoboLayout/RoboLayoutBuilder.h"

#include "RoboHTML/RoboHTMLNode.h"
#include "RoboHTML/RoboHTMLTraversal.h"
#include "RoboHTML/RoboHTMLTypes.h"

/* The one text-layer fact the builder needs: whether a text node carries anything
 * a line could hold (10.5). The predicate lives there rather than here because the
 * rule is CSS's collapsible-whitespace set, and a second copy of that set is a
 * second answer to what "whitespace" means. */
#include "RoboLayout/RoboLayoutText.h"

#include <string.h>

/** Everything one build carries down the recursion. */
typedef struct layoutBuildContext {
    LayoutTree *tree;
    const LayoutStyleSource *styles;
    LayoutBuildReport report;
} LayoutBuildContext;

/**
 * Build @p element's box's children, and their descendants.
 *
 * @p parentBox is the box of @p element; every child *element* becomes a box
 * under it, and every child *text node* carrying anything becomes a text object
 * under it (10.5). An element that generates no box is skipped, and so are its
 * descendants: a subtree with `display: none` on its root generates nothing,
 * which is the same rule applied one level down rather than a special case.
 *
 * @p parentStyle is @p element's own computed style, threaded down rather than
 * looked up again -- a text object borrows its parent element's style
 * (RoboLayoutBuilder.h), and re-asking the source per text node would be one extra
 * callback invocation per node for a value the recursion already holds.
 */
static Result BuildChildren(LayoutBuildContext *ctx,
                                    const HtmlNodeHandle *element,
                                    LayoutNodeId parentBox,
                                    const CssComputedStyle *parentStyle,
                                    uint32_t depth,
                                    LayoutDiag *diag)
{
    HtmlNodeView parentView;
    HtmlNodeHandle child;

    if (depth > RoboHtmlMaxNodesPerDocument) {
        return RoboLayoutDiag(diag, RoboLayoutDomainBuilder, RoboError,
                                "the document tree nests deeper than its own node bound");
    }
    if (HtmlNodeGetView(element, &parentView, NULL) != RoboOk) {
        return RoboLayoutDiag(diag, RoboLayoutDomainBuilder, RoboNotFound,
                                "an element handle did not resolve while building");
    }

    child = parentView.firstChild;
    while (HtmlNodeHandleIsWellformed(&child)) {
        HtmlNodeView childView;
        LayoutObjectInfo info;
        LayoutNodeId childBox = RoboLayoutNodeIdNone;
        const CssComputedStyle *style = NULL;
        Result added;

        if (HtmlNodeGetView(&child, &childView, NULL) != RoboOk) {
            return RoboLayoutDiag(diag, RoboLayoutDomainBuilder, RoboNotFound,
                                    "a child handle did not resolve while building");
        }

        /*
         * Text, comments and the document node are not *boxes* -- see the header.
         * A text node becomes a **text object** instead (10.5), which is a layout
         * object but not a box, and the distinction is the reason this branch is
         * not the same as the comment case beside it.
         */
        if (childView.kind == RoboHtmlNodeKindText) {
            const char *text = childView.text ? childView.text : "";
            size_t length = childView.text ? childView.textLength : 0u;

            /*
             * The one text rule: content that is entirely collapsible whitespace
             * generates no object (RoboLayoutBuilder.h). Whitespace *inside* a run is
             * kept -- collapsing it is a line-box algorithm -- and a run whose content
             * is only spaces is an empty run on every line it could ever be on.
             */
            if (LayoutTextIsCollapsibleWhitespace(text, length)) {
                ctx->report.textsSkippedWhitespace++;
                child = childView.NextSibling;
                continue;
            }

            LayoutObjectInfoInit(&info);
            info.kind = RoboLayoutObjectText;
            info.parent = parentBox;
            info.domNode = child;
            /* The *parent element's* style, borrowed -- see the header. */
            info.style = parentStyle;
            added = LayoutTreeAddObject(ctx->tree, &info, &childBox, diag);
            if (added != RoboOk) {
                return added;
            }
            ctx->report.textsCreated++;
            if (depth > ctx->report.depthMax) {
                ctx->report.depthMax = depth;
            }
            /*
             * Not descended into, and not counted in `elementsSeen`: a text node has
             * no children and is not an element, so the two counters above would be
             * describing something that is not there.
             */
            child = childView.NextSibling;
            continue;
        }

        if (childView.kind != RoboHtmlNodeKindElement) {
            child = childView.NextSibling;
            continue;
        }

        ctx->report.elementsSeen++;
        style = LayoutStyleSourceStyle(ctx->styles, &child);
        {
            CssDisplay display = LayoutStyleDisplay(style);

            if (!CssDisplayGeneratesBox(display)) {
                /* `display: none`: no box, and none for anything inside it. */
                ctx->report.skippedDisplayNone++;
                child = childView.NextSibling;
                continue;
            }

            LayoutObjectInfoInit(&info);
            info.parent = parentBox;
            info.domNode = child;
            info.style = style;
            info.display = display;
            added = LayoutTreeAddObject(ctx->tree, &info, &childBox, diag);
            if (added != RoboOk) {
                return added;
            }
            if (!style) {
                ctx->report.boxesWithoutStyle++;
            }
            if (depth > ctx->report.depthMax) {
                ctx->report.depthMax = depth;
            }
        }

        ctx->report.boxesCreated++;
        /* Descend. Depth is the box depth, so a child of the root is 1. */
        if (BuildChildren(ctx, &child, childBox, style, depth + 1u, diag) != RoboOk) {
            return diag ? diag->code : RoboError;
        }
        child = childView.NextSibling;
    }
    return RoboOk;
}

void LayoutBuildOptionsInit(LayoutBuildOptions *options)
{
    if (!options) {
        return;
    }
    memset(options, 0, sizeof(*options));
}

Result LayoutTreeBuildFromDocument(LayoutTree *tree,
                                                   HtmlDocumentId document,
                                                   const LayoutBuildOptions *options,
                                                   LayoutBuildReport *outReport,
                                                   LayoutDiag *diag)
{
    LayoutBuildContext ctx;
    HtmlNodeHandle documentRoot;
    HtmlNodeView documentView;
    HtmlNodeHandle rootElement;
    int foundRoot = 0;

    if (outReport) {
        memset(outReport, 0, sizeof(*outReport));
    }
    if (!tree) {
        return RoboLayoutDiag(diag, RoboLayoutDomainBuilder, RoboInvalidArgument,
                                "tree is NULL");
    }
    if (document == RoboHtmlDocumentIdNone) {
        return RoboLayoutDiag(diag, RoboLayoutDomainBuilder, RoboInvalidArgument,
                                "document is NONE");
    }
    if (!HtmlDocumentIsLive(document)) {
        return RoboLayoutDiag(diag, RoboLayoutDomainBuilder, RoboNotFound,
                                "document is not live");
    }

    /* A rebuild replaces: the tree is derived state, and a rebuild that merged
     * would keep the boxes of a tree the DOM no longer has. */
    LayoutTreeClear(tree);

    memset(&ctx, 0, sizeof(ctx));
    ctx.tree = tree;
    ctx.styles = options ? options->styles : NULL;

    documentRoot = HtmlDocumentRoot(document);
    if (!HtmlNodeHandleIsWellformed(&documentRoot)) {
        /* A document with no tree at all is not an error: it builds no boxes. */
        if (outReport) {
            *outReport = ctx.report;
        }
        return RoboOk;
    }
    if (HtmlNodeGetView(&documentRoot, &documentView, NULL) != RoboOk) {
        return RoboLayoutDiag(diag, RoboLayoutDomainBuilder, RoboNotFound,
                                "the document root did not resolve");
    }

    /* The layout root is the first *element* child of the document node. */
    rootElement = documentView.firstChild;
    while (HtmlNodeHandleIsWellformed(&rootElement)) {
        HtmlNodeView elementView;

        if (HtmlNodeGetView(&rootElement, &elementView, NULL) != RoboOk) {
            return RoboLayoutDiag(diag, RoboLayoutDomainBuilder, RoboNotFound,
                                    "a root candidate did not resolve");
        }
        if (elementView.kind == RoboHtmlNodeKindElement) {
            foundRoot = 1;
            break;
        }
        rootElement = elementView.NextSibling;
    }
    if (!foundRoot) {
        if (outReport) {
            *outReport = ctx.report;
        }
        return RoboOk;
    }

    {
        const CssComputedStyle *style =
            LayoutStyleSourceStyle(ctx.styles, &rootElement);
        CssDisplay display = LayoutStyleDisplay(style);
        LayoutObjectInfo info;
        LayoutNodeId rootBox = RoboLayoutNodeIdNone;
        Result added;

        ctx.report.elementsSeen++;
        if (!CssDisplayGeneratesBox(display)) {
            /* The whole document generates nothing. An empty tree is the honest
             * answer, and the report says why. */
            ctx.report.skippedDisplayNone++;
            if (outReport) {
                *outReport = ctx.report;
            }
            return RoboOk;
        }

        LayoutObjectInfoInit(&info);
        info.parent = RoboLayoutNodeIdNone;
        info.domNode = rootElement;
        info.style = style;
        info.display = display;
        added = LayoutTreeAddObject(tree, &info, &rootBox, diag);
        if (added != RoboOk) {
            return added;
        }
        if (!style) {
            ctx.report.boxesWithoutStyle++;
        }
        ctx.report.boxesCreated++;

        if (BuildChildren(&ctx, &rootElement, rootBox, style, 1u, diag) != RoboOk) {
            return diag ? diag->code : RoboError;
        }
    }

    if (outReport) {
        *outReport = ctx.report;
    }
    return RoboOk;
}
