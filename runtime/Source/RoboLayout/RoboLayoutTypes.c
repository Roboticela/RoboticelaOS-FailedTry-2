/*
 * RoboLayout vocabulary (Phase 10.1) — see RoboLayoutTypes.h.
 *
 * Two name tables and their predicates, and nothing else. They exist for the
 * reason RoboCSS's domain names do: a diagnostic that prints an enumerator's
 * *number* is a message nobody can act on, and a test that asserts a name is
 * asserting the spelling a reader will see.
 *
 * The tables are walked rather than guessed, so a suite can iterate
 * 0..COUNT-1 and require that every enumerator answers a name that is not
 * "unknown" — which is what makes "somebody added a value and forgot the table"
 * a failure instead of a mystery.
 */

#include "RoboLayout/RoboLayoutTypes.h"

/* ------------------------------------------------------------------ */
/* Object kind                                                         */
/* ------------------------------------------------------------------ */

int robo_layout_object_kind_is_known(robo_layout_object_kind_t kind)
{
    return kind >= ROBO_LAYOUT_OBJECT_NONE &&
           kind < (robo_layout_object_kind_t)ROBO_LAYOUT_OBJECT_KIND_COUNT;
}

const char *robo_layout_object_kind_name(robo_layout_object_kind_t kind)
{
    switch (kind) {
    case ROBO_LAYOUT_OBJECT_NONE:
        return "none";
    case ROBO_LAYOUT_OBJECT_BOX:
        return "box";
    default:
        return "unknown";
    }
}

/* ------------------------------------------------------------------ */
/* Diagnostic domain                                                   */
/* ------------------------------------------------------------------ */

int robo_layout_diagnostic_domain_is_known(robo_layout_diagnostic_domain_t domain)
{
    return domain >= ROBO_LAYOUT_DOMAIN_NONE &&
           domain < (robo_layout_diagnostic_domain_t)ROBO_LAYOUT_DOMAIN_COUNT;
}

const char *robo_layout_diagnostic_domain_name(robo_layout_diagnostic_domain_t domain)
{
    switch (domain) {
    case ROBO_LAYOUT_DOMAIN_NONE:
        return "none";
    case ROBO_LAYOUT_DOMAIN_MODULE:
        return "module";
    case ROBO_LAYOUT_DOMAIN_TREE:
        return "tree";
    case ROBO_LAYOUT_DOMAIN_OBJECT:
        return "object";
    case ROBO_LAYOUT_DOMAIN_CONSTRAINTS:
        return "constraints";
    case ROBO_LAYOUT_DOMAIN_RESULT:
        return "result";
    case ROBO_LAYOUT_DOMAIN_CONTEXT:
        return "context";
    case ROBO_LAYOUT_DOMAIN_BUILDER:
        return "builder";
    case ROBO_LAYOUT_DOMAIN_STYLE:
        return "style";
    case ROBO_LAYOUT_DOMAIN_DOM:
        return "dom";
    case ROBO_LAYOUT_DOMAIN_GEOMETRY:
        return "geometry";
    case ROBO_LAYOUT_DOMAIN_FORMATTING_CONTEXT:
        return "formatting-context";
    default:
        return "unknown";
    }
}
