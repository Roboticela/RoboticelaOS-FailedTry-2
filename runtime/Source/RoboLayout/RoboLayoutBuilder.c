/*
 * RoboLayout's builder (Phase 10.1) — see RoboLayoutBuilder.h.
 *
 * One rule, applied recursively: an element generates a box unless its computed
 * `display` says otherwise. The recursion is bounded by the document's own node
 * count, which RoboHTML already enforces, so a malformed tree cannot nest deeper
 * than ROBO_HTML_MAX_NODES_PER_DOCUMENT levels -- and the guard is written
 * explicitly anyway, because "it cannot happen" is the assumption that turns a
 * stack overflow into a mystery.
 */

#include "RoboLayout/RoboLayoutBuilder.h"

#include "RoboHTML/RoboHTMLNode.h"
#include "RoboHTML/RoboHTMLTraversal.h"
#include "RoboHTML/RoboHTMLTypes.h"

#include <string.h>

/** Everything one build carries down the recursion. */
typedef struct layout_build_context {
    robo_layout_tree_t *tree;
    const robo_layout_style_source_t *styles;
    robo_layout_build_report_t report;
} layout_build_context_t;

/**
 * Build @p element's box's children, and their descendants.
 *
 * @p parent_box is the box of @p element; every child *element* becomes a box
 * under it. An element that generates no box is skipped, and so are its
 * descendants: a subtree with `display: none` on its root generates nothing,
 * which is the same rule applied one level down rather than a special case.
 */
static robo_result_t build_children(layout_build_context_t *ctx,
                                    const robo_html_node_handle_t *element,
                                    robo_layout_node_id_t parent_box,
                                    uint32_t depth,
                                    robo_layout_diag_t *diag)
{
    robo_html_node_view_t parent_view;
    robo_html_node_handle_t child;

    if (depth > ROBO_HTML_MAX_NODES_PER_DOCUMENT) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_BUILDER, ROBO_ERROR,
                                "the document tree nests deeper than its own node bound");
    }
    if (robo_html_node_get_view(element, &parent_view, NULL) != ROBO_OK) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_BUILDER, ROBO_NOT_FOUND,
                                "an element handle did not resolve while building");
    }

    child = parent_view.first_child;
    while (robo_html_node_handle_is_wellformed(&child)) {
        robo_html_node_view_t child_view;
        robo_layout_object_info_t info;
        robo_layout_node_id_t child_box = ROBO_LAYOUT_NODE_ID_NONE;
        robo_result_t added;

        if (robo_html_node_get_view(&child, &child_view, NULL) != ROBO_OK) {
            return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_BUILDER, ROBO_NOT_FOUND,
                                    "a child handle did not resolve while building");
        }

        /* Text, comments and the document node are walked over rather than
         * turned into boxes -- see the header. The walk still uses the sibling
         * link, so the *order* of the boxes is the DOM's own order. */
        if (child_view.kind != ROBO_HTML_NODE_KIND_ELEMENT) {
            child = child_view.next_sibling;
            continue;
        }

        ctx->report.elements_seen++;
        {
            const robo_css_computed_style_t *style =
                robo_layout_style_source_style(ctx->styles, &child);
            robo_css_display_t display = robo_layout_style_display(style);

            if (!robo_css_display_generates_box(display)) {
                /* `display: none`: no box, and none for anything inside it. */
                ctx->report.skipped_display_none++;
                child = child_view.next_sibling;
                continue;
            }

            robo_layout_object_info_init(&info);
            info.parent = parent_box;
            info.dom_node = child;
            info.style = style;
            info.display = display;
            added = robo_layout_tree_add_object(ctx->tree, &info, &child_box, diag);
            if (added != ROBO_OK) {
                return added;
            }
            if (!style) {
                ctx->report.boxes_without_style++;
            }
            if (depth > ctx->report.depth_max) {
                ctx->report.depth_max = depth;
            }
        }

        ctx->report.boxes_created++;
        /* Descend. Depth is the box depth, so a child of the root is 1. */
        if (build_children(ctx, &child, child_box, depth + 1u, diag) != ROBO_OK) {
            return diag ? diag->code : ROBO_ERROR;
        }
        child = child_view.next_sibling;
    }
    return ROBO_OK;
}

void robo_layout_build_options_init(robo_layout_build_options_t *options)
{
    if (!options) {
        return;
    }
    memset(options, 0, sizeof(*options));
}

robo_result_t robo_layout_tree_build_from_document(robo_layout_tree_t *tree,
                                                   robo_html_document_id_t document,
                                                   const robo_layout_build_options_t *options,
                                                   robo_layout_build_report_t *out_report,
                                                   robo_layout_diag_t *diag)
{
    layout_build_context_t ctx;
    robo_html_node_handle_t document_root;
    robo_html_node_view_t document_view;
    robo_html_node_handle_t root_element;
    int found_root = 0;

    if (out_report) {
        memset(out_report, 0, sizeof(*out_report));
    }
    if (!tree) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_BUILDER, ROBO_INVALID_ARGUMENT,
                                "tree is NULL");
    }
    if (document == ROBO_HTML_DOCUMENT_ID_NONE) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_BUILDER, ROBO_INVALID_ARGUMENT,
                                "document is NONE");
    }
    if (!robo_html_document_is_live(document)) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_BUILDER, ROBO_NOT_FOUND,
                                "document is not live");
    }

    /* A rebuild replaces: the tree is derived state, and a rebuild that merged
     * would keep the boxes of a tree the DOM no longer has. */
    robo_layout_tree_clear(tree);

    memset(&ctx, 0, sizeof(ctx));
    ctx.tree = tree;
    ctx.styles = options ? options->styles : NULL;

    document_root = robo_html_document_root(document);
    if (!robo_html_node_handle_is_wellformed(&document_root)) {
        /* A document with no tree at all is not an error: it builds no boxes. */
        if (out_report) {
            *out_report = ctx.report;
        }
        return ROBO_OK;
    }
    if (robo_html_node_get_view(&document_root, &document_view, NULL) != ROBO_OK) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_BUILDER, ROBO_NOT_FOUND,
                                "the document root did not resolve");
    }

    /* The layout root is the first *element* child of the document node. */
    root_element = document_view.first_child;
    while (robo_html_node_handle_is_wellformed(&root_element)) {
        robo_html_node_view_t element_view;

        if (robo_html_node_get_view(&root_element, &element_view, NULL) != ROBO_OK) {
            return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_BUILDER, ROBO_NOT_FOUND,
                                    "a root candidate did not resolve");
        }
        if (element_view.kind == ROBO_HTML_NODE_KIND_ELEMENT) {
            found_root = 1;
            break;
        }
        root_element = element_view.next_sibling;
    }
    if (!found_root) {
        if (out_report) {
            *out_report = ctx.report;
        }
        return ROBO_OK;
    }

    {
        const robo_css_computed_style_t *style =
            robo_layout_style_source_style(ctx.styles, &root_element);
        robo_css_display_t display = robo_layout_style_display(style);
        robo_layout_object_info_t info;
        robo_layout_node_id_t root_box = ROBO_LAYOUT_NODE_ID_NONE;
        robo_result_t added;

        ctx.report.elements_seen++;
        if (!robo_css_display_generates_box(display)) {
            /* The whole document generates nothing. An empty tree is the honest
             * answer, and the report says why. */
            ctx.report.skipped_display_none++;
            if (out_report) {
                *out_report = ctx.report;
            }
            return ROBO_OK;
        }

        robo_layout_object_info_init(&info);
        info.parent = ROBO_LAYOUT_NODE_ID_NONE;
        info.dom_node = root_element;
        info.style = style;
        info.display = display;
        added = robo_layout_tree_add_object(tree, &info, &root_box, diag);
        if (added != ROBO_OK) {
            return added;
        }
        if (!style) {
            ctx.report.boxes_without_style++;
        }
        ctx.report.boxes_created++;

        if (build_children(&ctx, &root_element, root_box, 1u, diag) != ROBO_OK) {
            return diag ? diag->code : ROBO_ERROR;
        }
    }

    if (out_report) {
        *out_report = ctx.report;
    }
    return ROBO_OK;
}
