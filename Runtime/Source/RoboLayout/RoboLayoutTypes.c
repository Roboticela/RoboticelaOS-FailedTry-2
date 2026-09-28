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

int LayoutObjectKindIsKnown(LayoutObjectKind kind)
{
    return kind >= RoboLayoutObjectNone &&
           kind < (LayoutObjectKind)RoboLayoutObjectKindCount;
}

const char *LayoutObjectKindName(LayoutObjectKind kind)
{
    switch (kind) {
    case RoboLayoutObjectNone:
        return "none";
    case RoboLayoutObjectBox:
        return "box";
    case RoboLayoutObjectText:
        return "text";
    default:
        return "unknown";
    }
}

/* ------------------------------------------------------------------ */
/* Diagnostic domain                                                   */
/* ------------------------------------------------------------------ */

int LayoutDiagnosticDomainIsKnown(LayoutDiagnosticDomain domain)
{
    return domain >= RoboLayoutDomainNone &&
           domain < (LayoutDiagnosticDomain)RoboLayoutDomainCount;
}

const char *LayoutDiagnosticDomainName(LayoutDiagnosticDomain domain)
{
    switch (domain) {
    case RoboLayoutDomainNone:
        return "none";
    case RoboLayoutDomainModule:
        return "module";
    case RoboLayoutDomainTree:
        return "tree";
    case RoboLayoutDomainObject:
        return "object";
    case RoboLayoutDomainConstraints:
        return "constraints";
    case RoboLayoutDomainResult:
        return "result";
    case RoboLayoutDomainContext:
        return "context";
    case RoboLayoutDomainBuilder:
        return "builder";
    case RoboLayoutDomainStyle:
        return "style";
    case RoboLayoutDomainDom:
        return "dom";
    case RoboLayoutDomainGeometry:
        return "geometry";
    case RoboLayoutDomainFormattingContext:
        return "formatting-context";
    case RoboLayoutDomainBlockLayout:
        return "block-layout";
    case RoboLayoutDomainInlineLayout:
        return "inline-layout";
    case RoboLayoutDomainTextLayout:
        return "text-layout";
    case RoboLayoutDomainSizing:
        return "sizing";
    case RoboLayoutDomainMargins:
        return "margins";
    case RoboLayoutDomainPadding:
        return "padding";
    case RoboLayoutDomainBorder:
        return "border";
    case RoboLayoutDomainPosition:
        return "position";
    case RoboLayoutDomainOverflow:
        return "overflow";
    case RoboLayoutDomainInvalidation:
        return "invalidation";
    case RoboLayoutDomainAbsolute:
        return "absolute";
    case RoboLayoutDomainIntrinsic:
        return "intrinsic";
    case RoboLayoutDomainFlex:
        return "flex";
    case RoboLayoutDomainGrid:
        return "grid";
    case RoboLayoutDomainTable:
        return "table";
    case RoboLayoutDomainFragment:
        return "fragment";
    default:
        return "unknown";
    }
}
