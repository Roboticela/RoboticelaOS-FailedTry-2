#include "RoboHTML/RoboHTMLLifecycle.h"

int HtmlDocumentLifecycleIsKnown(HtmlDocumentLifecycle life)
{
    switch (life) {
    case RoboHtmlDocumentLifecycleUninitialized:
    case RoboHtmlDocumentLifecycleCreated:
    case RoboHtmlDocumentLifecycleLoading:
    case RoboHtmlDocumentLifecycleInteractive:
    case RoboHtmlDocumentLifecycleComplete:
    case RoboHtmlDocumentLifecycleFailed:
    case RoboHtmlDocumentLifecycleDestroying:
    case RoboHtmlDocumentLifecycleDestroyed:
        return 1;
    default:
        return 0;
    }
}

int HtmlDocumentLifecycleCanTransition(
    HtmlDocumentLifecycle from,
    HtmlDocumentLifecycle to)
{
    if (!HtmlDocumentLifecycleIsKnown(from) ||
        !HtmlDocumentLifecycleIsKnown(to)) {
        return 0;
    }
    if (from == RoboHtmlDocumentLifecycleUninitialized) {
        return to == RoboHtmlDocumentLifecycleCreated;
    }
    if (from == RoboHtmlDocumentLifecycleCreated) {
        return to == RoboHtmlDocumentLifecycleLoading ||
               to == RoboHtmlDocumentLifecycleFailed ||
               to == RoboHtmlDocumentLifecycleDestroying;
    }
    if (from == RoboHtmlDocumentLifecycleLoading) {
        return to == RoboHtmlDocumentLifecycleInteractive ||
               to == RoboHtmlDocumentLifecycleFailed ||
               to == RoboHtmlDocumentLifecycleDestroying;
    }
    if (from == RoboHtmlDocumentLifecycleInteractive) {
        return to == RoboHtmlDocumentLifecycleComplete ||
               to == RoboHtmlDocumentLifecycleLoading ||
               to == RoboHtmlDocumentLifecycleFailed ||
               to == RoboHtmlDocumentLifecycleDestroying;
    }
    if (from == RoboHtmlDocumentLifecycleComplete) {
        return to == RoboHtmlDocumentLifecycleLoading ||
               to == RoboHtmlDocumentLifecycleFailed ||
               to == RoboHtmlDocumentLifecycleDestroying;
    }
    if (from == RoboHtmlDocumentLifecycleFailed) {
        return to == RoboHtmlDocumentLifecycleDestroying;
    }
    if (from == RoboHtmlDocumentLifecycleDestroying) {
        return to == RoboHtmlDocumentLifecycleDestroyed;
    }
    /* DESTROYED → DESTROYED is the idempotent stay. */
    return to == RoboHtmlDocumentLifecycleDestroyed;
}

Result HtmlDocumentLifecycleCheckTransition(
    HtmlDocumentLifecycle from,
    HtmlDocumentLifecycle to)
{
    return HtmlDocumentLifecycleCanTransition(from, to)
               ? RoboOk
               : RoboInvalidArgument;
}

const char *HtmlDocumentLifecycleName(
    HtmlDocumentLifecycle life)
{
    switch (life) {
    case RoboHtmlDocumentLifecycleUninitialized:
        return "uninitialized";
    case RoboHtmlDocumentLifecycleCreated:
        return "created";
    case RoboHtmlDocumentLifecycleLoading:
        return "loading";
    case RoboHtmlDocumentLifecycleInteractive:
        return "interactive";
    case RoboHtmlDocumentLifecycleComplete:
        return "complete";
    case RoboHtmlDocumentLifecycleFailed:
        return "failed";
    case RoboHtmlDocumentLifecycleDestroying:
        return "destroying";
    case RoboHtmlDocumentLifecycleDestroyed:
        return "destroyed";
    default:
        return "unknown";
    }
}

int HtmlDocumentLifecycleAllowsContent(
    HtmlDocumentLifecycle life)
{
    return life == RoboHtmlDocumentLifecycleCreated ||
           life == RoboHtmlDocumentLifecycleLoading ||
           life == RoboHtmlDocumentLifecycleInteractive ||
           life == RoboHtmlDocumentLifecycleComplete;
}

int HtmlDocumentLifecycleAllowsSourceAttach(
    HtmlDocumentLifecycle life)
{
    return life == RoboHtmlDocumentLifecycleCreated ||
           life == RoboHtmlDocumentLifecycleInteractive ||
           life == RoboHtmlDocumentLifecycleComplete;
}

int HtmlDocumentLifecycleIsSettled(HtmlDocumentLifecycle life)
{
    return life == RoboHtmlDocumentLifecycleInteractive ||
           life == RoboHtmlDocumentLifecycleComplete;
}

int HtmlDocumentLifecycleIsTerminal(HtmlDocumentLifecycle life)
{
    return life == RoboHtmlDocumentLifecycleFailed ||
           life == RoboHtmlDocumentLifecycleDestroying ||
           life == RoboHtmlDocumentLifecycleDestroyed;
}

int HtmlDocumentLifecycleIsDestroyed(
    HtmlDocumentLifecycle life)
{
    return life == RoboHtmlDocumentLifecycleDestroyed;
}
