/*
 * RoboHTML element/attribute boundary (Phase 8.1) — architecture stub.
 *
 * Fixes where the future element model lives and how names/attributes will
 * flow from tokenizer (8.2) through the tree builder (8.3) into the DOM
 * phase. No element instances, no attribute storage, no behavior here.
 *
 * Naming policy (Roboticela): element and attribute names are stored as
 * lowercase ASCII after boundary normalization; the normalization helper
 * below is the single sanctioned place to enforce that so tokenizer output
 * and application queries compare consistently.
 */

#ifndef ROBO_HTML_ELEMENT_H
#define ROBO_HTML_ELEMENT_H

#include "RoboBase/RoboError.h"
#include "RoboHTML/RoboHTMLDiagnostics.h"
#include "RoboHTML/RoboHTMLNode.h"
#include "RoboHTML/RoboHTMLTypes.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Maximum copied element/attribute name length (excluding NUL). */
#define ROBO_HTML_NAME_MAX 64u

/** Maximum copied attribute value length (excluding NUL) in the stub. */
#define ROBO_HTML_ATTR_VALUE_MAX 256u

/**
 * Future element registry reservation. Phase 8.1 defines only the shape
 * the element subsystem will register against; it is intentionally opaque.
 */
typedef struct robo_html_element_boundary {
    /** Reserved; must be 0. Guards against accidental use before the DOM phase. */
    uint32_t reserved;
} robo_html_element_boundary_t;

/** Reset the boundary reservation. Safe on NULL. */
void robo_html_element_boundary_init(robo_html_element_boundary_t *b);

/**
 * Validate an element/attribute name at the boundary:
 *   - non-empty, ASCII letters/digits/'-'/'_' only,
 *   - length <= ROBO_HTML_NAME_MAX.
 * This is a policy check for later phases, exposed so the contract is
 * testable now. No storage, no registry.
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT (NULL/empty); ROBO_PARSE_ERROR
 *         (bad character); ROBO_NOT_SUPPORTED (too long).
 */
robo_result_t robo_html_element_name_validate(const char *name,
                                              robo_html_diag_t *diag);

/**
 * ASCII-lowercase copy of @p name into @p out (capacity
 * ROBO_HTML_NAME_MAX). Future tokenizer/tree-builder naming policy.
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT / ROBO_PARSE_ERROR per validate;
 *         ROBO_NOT_SUPPORTED when the copy would overflow.
 */
robo_result_t robo_html_element_name_normalize(const char *name,
                                               char *out,
                                               robo_html_diag_t *diag);

/* ------------------------------------------------------------------ */
/* Element/attribute storage read side (Phase 8.3)                     */
/* ------------------------------------------------------------------ */

/**
 * Borrowed attribute view (see the node payload contract in
 * RoboHTMLNode.h: valid until the arena grows or the document dies).
 */
typedef struct robo_html_attr_view {
    const char *name;  /* borrowed, lowercase ASCII, NUL-terminated */
    const char *value; /* borrowed, character references already decoded */
} robo_html_attr_view_t;

/**
 * Element name and size summary.
 * @c name is borrowed and lowercase (the boundary normalization is applied
 * once, when the tree builder stores the name).
 */
typedef struct robo_html_element_view {
    const char *name;
    size_t name_length;
    size_t attribute_count;
    /** Non-zero when the source wrote "/>" for this element. */
    int self_closing;
} robo_html_element_view_t;

/**
 * Read an element's name/attribute summary.
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT (NULL args / ill-formed handle);
 *         ROBO_NOT_FOUND (unknown id); ROBO_NOT_SUPPORTED when the node is
 *         not an element or has no stored payload.
 */
robo_result_t robo_html_element_view_get(
    const robo_html_node_handle_t *element,
    robo_html_element_view_t *out,
    robo_html_diag_t *diag);

/**
 * Read attribute @p index in source order (duplicates preserved exactly as
 * the tokenizer emitted them — no last-wins folding in this phase).
 * Out of range → ROBO_NOT_FOUND.
 */
robo_result_t robo_html_element_attribute_at(
    const robo_html_node_handle_t *element,
    size_t index,
    robo_html_attr_view_t *out,
    robo_html_diag_t *diag);

/**
 * First attribute named @p name (ASCII case-insensitive, since stored names
 * are lowercase); ROBO_NOT_FOUND when absent.
 */
robo_result_t robo_html_element_find_attribute(
    const robo_html_node_handle_t *element,
    const char *name,
    robo_html_attr_view_t *out,
    robo_html_diag_t *diag);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_HTML_ELEMENT_H */
