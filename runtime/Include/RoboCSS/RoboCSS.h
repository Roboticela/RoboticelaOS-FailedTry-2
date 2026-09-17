/*
 * RoboCSS module lifecycle (Phase 9.1).
 *
 * Module state machine, the same shape RoboHTML and RoboWindow use:
 *
 *   uninitialized → initialized → uninitialized
 *
 *   - robo_css_init validates its foundation (RoboBase is up; RoboMem needs no
 *     initialization of its own) and refuses double initialization with
 *     ROBO_ALREADY_EXISTS.
 *   - robo_css_shutdown destroys every live stylesheet in slot order and
 *     releases the module table. It is idempotent.
 *   - every stylesheet entry point requires the module to be initialized and
 *     answers ROBO_INIT_ERROR otherwise — deterministically, never by crash.
 *
 * The module owns one thing: the stylesheet slot table. Property tables, style
 * contexts, parsers and every model object belong to their callers, which is
 * why shutdown has nothing else to free.
 *
 * Capabilities, not marketing: robo_css_get_capabilities reports what this
 * build actually does. The model flags are 1, and so are the behaviour flags of
 * every phase that has landed — the tokenizer (9.2), the grammar and inline
 * parsing (9.3), selector parsing and matching (9.4), the cascade (9.5). The
 * flags that remain 0 are specificity, inheritance, computed values, default
 * styles and loading, and each of those names work this build does not do. A
 * caller that wants to know whether CSS will style anything should read this
 * rather than infer it from a version number.
 *
 * A flag is 1 only when the thing it names works end to end, which is why
 * `cascade` can be 1 while specificity is still ahead: collecting what applies
 * and ordering it is complete, and the one question that needs specificity — two
 * candidates of one level from different selectors — comes back *reported* as
 * undecided rather than answered by accident.
 */

#ifndef ROBO_CSS_H
#define ROBO_CSS_H

#include "RoboBase/RoboError.h"
#include "RoboCSS/RoboCSSTypes.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Null-terminated RoboCSS version string (static; do not free). */
const char *robo_css_version_string(void);

/** Numeric RoboCSS version components; NULL out → ROBO_INVALID_ARGUMENT. */
robo_result_t robo_css_get_version(robo_i32 *out_major,
                                   robo_i32 *out_minor,
                                   robo_i32 *out_patch);

/**
 * Initialize RoboCSS.
 * @return ROBO_OK; ROBO_ALREADY_EXISTS if already initialized;
 *         ROBO_INIT_ERROR when RoboBase is down.
 */
robo_result_t robo_css_init(void);

/** Shut down RoboCSS: destroy every live stylesheet. Idempotent. */
void robo_css_shutdown(void);

/** @return 1 if the module is initialized, else 0. */
int robo_css_is_initialized(void);

/** Number of live stylesheets (0 when uninitialized). */
size_t robo_css_live_stylesheet_count(void);

/**
 * What this build of RoboCSS can do.
 *
 * The model flags describe the data types of this phase; the behaviour flags
 * describe the phases that have not run. A flag is 1 only when the thing it
 * names works end to end, so no caller can be misled by a half-built feature
 * claiming to be present.
 */
typedef struct robo_css_capabilities {
    /* --- model: present in 9.1 --- */
    /** The source model: bytes, positions, ranges (9.1). */
    int source_model;
    /** The token model: the token vocabulary and a token bound to a range (9.1). */
    int token_model;
    /** The component-value model: token/function/block/list trees (9.1). */
    int value_model;
    /** Property identity: the table, registration and lookup (9.1). */
    int property_registry;
    /** The declaration model, declaration lists, and importance (9.1). */
    int declaration_model;
    /** The selector model: simple/compound/complex selectors and lists (9.1). */
    int selector_model;
    /** The rule model: style rules, at-rules and rule lists (9.1). */
    int rule_model;
    /** The stylesheet model: origin, state, source, rules (9.1). */
    int stylesheet_model;
    /** The CSS error vocabulary and the parser cursor (9.1). */
    int error_model;

    /* --- behaviour --- */
    /** The tokenizer state machine: CSS bytes in, tokens out (9.2). */
    int tokenizer;
    /**
     * The stylesheet/declaration grammar: tokens in, rules, declarations and
     * component values out (9.3).
     */
    int parser;
    /**
     * The selector grammar: a rule's prelude in, a selector list out, names
     * resolved and arguments (An+B, `:not()`) parsed (9.4).
     */
    int selector_parsing;
    /** Selector matching against an element host (9.4). */
    int selector_matching;
    /**
     * The cascade order (9.5): collecting what applies to an element in cascade
     * order and resolving a property wherever origin, importance and document
     * order decide it.
     *
     * 1 does not mean "the cascade is finished". Specificity is the other half
     * of the ordering and is 9.6, so a contest between two candidates of the
     * same level from different selectors comes back as
     * ROBO_CSS_CASCADE_VERDICT_UNDECIDED_SPECIFICITY — reported, never guessed.
     */
    int cascade;
    /** Inheritance of inherited properties (9.7). */
    int inheritance;
    /** Computed values (9.8). */
    int computed_values;
    /**
     * Parsing an element's `style` attribute — the 9.1 boundary, filled by the
     * 9.3 grammar. The DOM still owns the attribute; RoboCSS is handed its text.
     */
    int inline_styles;
    /** A runtime default stylesheet (9.1 boundary, later content). */
    int default_styles;
    /** Loading stylesheets from anywhere: `@import`, files, URLs. */
    int stylesheet_loading;
} robo_css_capabilities_t;

/** Zero every flag. Safe on NULL. */
void robo_css_capabilities_init(robo_css_capabilities_t *capabilities);

/**
 * Fill @p out with this build's capabilities.
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for a NULL output.
 * Available whether or not the module is initialized: it describes the build.
 */
robo_result_t robo_css_get_capabilities(robo_css_capabilities_t *out);

/**
 * The runtime's default (user-agent) stylesheet, as CSS text.
 *
 * This is the boundary for 9.1.11 and nothing more: the runtime default styles
 * are ordinary CSS that will be parsed through the same path as any other
 * stylesheet, which is why the interface hands out *text* rather than a
 * pre-built table of declarations.
 *
 * Phase 9.1 ships no default styles, so this returns NULL and writes 0 to
 * @p out_length. A caller must therefore treat NULL as "no runtime styles"
 * rather than as an error — the boundary exists, the styles do not yet. The
 * length is written whenever @p out_length is non-NULL, including for the NULL
 * return.
 */
const char *robo_css_default_stylesheet_text(size_t *out_length);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_CSS_H */
