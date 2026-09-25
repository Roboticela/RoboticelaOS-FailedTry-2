/*
 * RoboDesktop shell windows (Phase 11.13) — see RoboDesktopWindow.h.
 *
 * ## The list is an array, and the z-order is its order
 *
 * Eight slots, `windows[0]` furthest back, `windows[count - 1]` in front. A *list* rather than a
 * z-order field per window, and the reason is that the two have to agree: with an order field, "paint in
 * z-order" is a sort whose tie-breaking is a second decision, and "raise" is a renumbering that has to
 * update every window it moved past. With the array, raising is `memmove` one element to the end and the
 * painter is a loop. The cost is that `at(index)` is an index and not an id -- which is exactly what the
 * door says it is.
 *
 * Removing a window shifts the ones above it down, so `focusedIndex` is repaired on every removal rather
 * than being allowed to dangle: a shell whose focus pointed at a destroyed window would paint no accent
 * border and route a press to nothing, and neither would be a visible failure until someone looked.
 *
 * ## The minimum-size rule, and what it is not
 *
 * `DesktopWindowAdd` **grows** a window below the decoration minimum rather than refusing it. That
 * is the opposite of what this tree's `WindowSetSize` does one module down (which rejects an
 * out-of-constraint size rather than clamping), and the difference is what the minimum *is*: RoboWindow's
 * constraints are the caller's policy, so silently overriding them would be overriding an intention; the
 * desktop's minimum is a property of decorations the caller does not control, so a request below it has
 * no expressible meaning and the smallest drawable window is the closest thing to what was asked for.
 * Both rules are right for their own subject, and this comment exists so the divergence reads as one.
 *
 * ## Closing is not destroying
 *
 * `request_close` marks a window CLOSING, hides it, and leaves it in the list for the owner to destroy --
 * see `RoboDesktopTypes.h`. The close button produces this and nothing else, which is why a desktop with
 * a close button is still a desktop whose windows' lifetimes belong to whoever made them.
 */

#include "RoboDesktopInternal.h"

#include "RoboBase/RoboLog.h"

/* ------------------------------------------------------------------ */
/* Descriptor vocabulary                                               */
/* ------------------------------------------------------------------ */

void DesktopWindowDescInit(DesktopWindowDesc *desc)
{
    if (!desc) {
        return;
    }
    desc->title = NULL;
    /* Negative means "place me" -- the only state a caller that does not know the work area can ask for
     * (see the header). */
    desc->x = -1;
    desc->y = -1;
    desc->width = 560;
    desc->height = 360;
    desc->content = NULL;
    desc->contentFace = NULL;
    desc->visible = 1;
}

Result DesktopWindowDescValidate(const DesktopWindowDesc *desc)
{
    if (!desc) {
        return RoboInvalidArgument;
    }
    if (desc->width < 0 || desc->height < 0) {
        return RoboInvalidArgument;
    }
    if (desc->width > (int32_t)RoboDesktopMaxDimension ||
        desc->height > (int32_t)RoboDesktopMaxDimension) {
        return RoboInvalidArgument;
    }
    if (desc->title && DesktopStringLength(desc->title) + 1u > RoboDesktopTitleMax) {
        return RoboInvalidArgument;
    }
    return RoboOk;
}

int DesktopWindowIdIsNone(DesktopWindowId id)
{
    return id == RoboDesktopWindowIdNone;
}

int DesktopWindowIdEqual(DesktopWindowId a, DesktopWindowId b)
{
    return a == b;
}

/* ------------------------------------------------------------------ */
/* The list                                                            */
/* ------------------------------------------------------------------ */

const DesktopWindow *DesktopWindowFindConst(const Desktop *desktop,
                                                      DesktopWindowId id)
{
    size_t index;

    if (!desktop || DesktopWindowIdIsNone(id)) {
        return NULL;
    }
    for (index = 0u; index < desktop->windowCount; index++) {
        if (desktop->windows[index].id == id) {
            return &desktop->windows[index];
        }
    }
    return NULL;
}

DesktopWindow *DesktopWindowFind(Desktop *desktop, DesktopWindowId id)
{
    return (DesktopWindow *)DesktopWindowFindConst(desktop, id);
}

size_t DesktopWindowIndexOf(const Desktop *desktop, DesktopWindowId id)
{
    size_t index;

    for (index = 0u; index < desktop->windowCount; index++) {
        if (desktop->windows[index].id == id) {
            return index;
        }
    }
    return RoboDesktopNoIndex;
}

/** Focus falls to the front window, or to none. Called after every removal. */
void DesktopWindowRepairFocus(Desktop *desktop)
{
    if (desktop->focusedIndex != RoboDesktopNoIndex &&
        desktop->focusedIndex < desktop->windowCount) {
        return;
    }
    desktop->focusedIndex =
        (desktop->windowCount == 0u) ? RoboDesktopNoIndex : (desktop->windowCount - 1u);
}

void DesktopWindowReleaseTarget(DesktopWindow *window)
{
    if (window && window->target) {
        RenderTargetDestroy(window->target);
        window->target = NULL;
    }
}

/* ------------------------------------------------------------------ */
/* Public doors                                                        */
/* ------------------------------------------------------------------ */

size_t DesktopWindowCount(const Desktop *desktop)
{
    return desktop ? desktop->windowCount : 0u;
}

Result DesktopWindowAt(const Desktop *desktop,
                                     size_t index,
                                     DesktopWindowId *outId)
{
    if (!desktop || !outId) {
        return RoboInvalidArgument;
    }
    if (index >= desktop->windowCount) {
        return RoboNotFound;
    }
    *outId = desktop->windows[index].id;
    return RoboOk;
}

int DesktopWindowExists(const Desktop *desktop, DesktopWindowId id)
{
    return DesktopWindowFindConst(desktop, id) != NULL;
}

Result DesktopWindowAdd(Desktop *desktop,
                                     const DesktopWindowDesc *desc,
                                     DesktopWindowId *outId,
                                     DesktopDiag *diag)
{
    DesktopWindow *window;
    GfxPoint placed;
    GfxRect frame;
    Result status;

    if (!desktop || !desc || !outId) {
        return RoboDesktopDiag(diag, RoboDesktopDomainWindow, RoboInvalidArgument,
                                 "a desktop, a description and an out id are required");
    }
    status = DesktopWindowDescValidate(desc);
    if (status != RoboOk) {
        return RoboDesktopDiag(diag, RoboDesktopDomainWindow, status,
                                 "the window description is not one this build can create");
    }
    if (desktop->windowCount >= RoboDesktopMaxWindows) {
        return RoboDesktopDiag(diag, RoboDesktopDomainWindow, RoboAlreadyExists,
                                 "the desktop already holds the maximum number of windows");
    }
    /*
     * "Is there a work area to place a window in?" is a question about *room*, not about emptiness: a
     * work area one pixel tall is a rectangle, so `GfxRectIsEmpty` would answer no and the shell
     * would place a window it cannot draw. `RoboDesktopMinDimension` is the smallest work area this
     * build treats as usable, and it is tested on both axes -- a 400x4 work area is as unplaceable as a
     * 4x400 one. The geometry itself is total and stays total (a thin work area is a rectangle and a
     * caller can read it); this is the door where "drawable" is decided. See `RoboDesktopTypes.h`.
     */
    if (desktop->geometry.workArea.width < RoboDesktopMinDimension ||
        desktop->geometry.workArea.height < RoboDesktopMinDimension) {
        return RoboDesktopDiag(diag, RoboDesktopDomainShell, RoboInvalidArgument,
                                 "there is no work area to place a window in");
    }

    /* The decorations come first: a window smaller than them cannot be drawn at all. */
    frame.width = (desc->width < desktop->windowMetrics.minWidth)
                      ? desktop->windowMetrics.minWidth
                      : desc->width;
    frame.height = (desc->height < desktop->windowMetrics.minHeight)
                       ? desktop->windowMetrics.minHeight
                       : desc->height;
    if (desc->x < 0 || desc->y < 0) {
        /* "Place me": the deterministic centred-nudged-up placement, then the work-area clamp, so a
         * default-sized window on a small screen is still fully visible. */
        status = DesktopGeometryPlace(MakeGfxSize(frame.width, frame.height),
                                             &desktop->geometry.workArea, &placed);
        if (status != RoboOk) {
            return RoboDesktopDiag(diag, RoboDesktopDomainWindow, status,
                                     "the window could not be placed in the work area");
        }
        frame.x = placed.x;
        frame.y = placed.y;
    } else {
        frame.x = desc->x;
        frame.y = desc->y;
    }
    if (DesktopGeometryContain(frame, &desktop->geometry.workArea, &frame) != RoboOk) {
        return RoboDesktopDiag(diag, RoboDesktopDomainWindow, RoboError,
                                 "the window's bounds could not be clamped into the work area");
    }

    window = &desktop->windows[desktop->windowCount];
    /* Zeroed rather than field-assigned where a field has a meaningful "nothing": the array is reused
     * across additions, and a stale `target` or `content` from a destroyed window would be exactly the
     * cross-window leak this module can least afford. */
    {
        size_t bytes = sizeof(*window);
        size_t index;

        for (index = 0u; index < bytes; index++) {
            ((char *)window)[index] = 0;
        }
    }
    window->id = ++desktop->nextId;
    if (window->id == RoboDesktopWindowIdNone) {
        /* A wrap would hand out the "none" token, which is not a window: skip it. */
        window->id = ++desktop->nextId;
    }
    window->bounds = frame;
    window->state = RoboDesktopWindowOpen;
    window->visible = desc->visible ? 1 : 0;
    window->content = desc->content;
    window->contentFace = desc->contentFace;
    window->contentDirty = 1;
    window->frameDirty = 1;
    window->target = NULL;
    if (desc->title &&
        DesktopStringCopy(window->title, sizeof(window->title), desc->title) != RoboOk) {
        return RoboDesktopDiag(diag, RoboDesktopDomainWindow, RoboInvalidArgument,
                                 "the window's title does not fit");
    }

    desktop->windowCount++;
    desktop->stats.windowsAdded++;
    DesktopComposeInvalidateWindowOrder(desktop);
    /* Mark prior focused chrome dirty so its inactive title bar redraws. */
    if (desktop->focusedIndex != RoboDesktopNoIndex &&
        desktop->focusedIndex < desktop->windowCount - 1u) {
        desktop->windows[desktop->focusedIndex].frameDirty = 1;
    }
    /* A new window is focused: it is what the caller just made, and on a desktop with no window manager
     * "the newest window is the one being used" is the only defensible default. */
    desktop->focusedIndex = desktop->windowCount - 1u;
    *outId = window->id;
    /* The *bounds* rather than the request: a grown or clamped window is the fact a reader needs, and
     * the placement decision is the one thing this door and `RoboDesktop.h` most often disagree about. */
    LogDebug("robo_desktop: window added id=%llu bounds=%d,%d %dx%d (count=%llu)",
                   (unsigned long long)window->id, (int)frame.x, (int)frame.y, (int)frame.width,
                   (int)frame.height, (unsigned long long)desktop->windowCount);
    return RoboOk;
}

Result DesktopWindowSetBounds(Desktop *desktop,
                                            DesktopWindowId id,
                                            GfxRect bounds,
                                            DesktopDiag *diag)
{
    DesktopWindow *window;
    GfxRect clamped;

    if (!desktop) {
        return RoboDesktopDiag(diag, RoboDesktopDomainWindow, RoboInvalidArgument,
                                 "a desktop is required");
    }
    window = DesktopWindowFind(desktop, id);
    if (!window) {
        return RoboDesktopDiag(diag, RoboDesktopDomainWindow, RoboNotFound,
                                 "no such window");
    }
    if (!GfxRectIsValid(bounds)) {
        return RoboDesktopDiag(diag, RoboDesktopDomainWindow, RoboInvalidArgument,
                                 "the window's bounds are not a rectangle");
    }
    if (bounds.width < desktop->windowMetrics.minWidth) {
        bounds.width = desktop->windowMetrics.minWidth;
    }
    if (bounds.height < desktop->windowMetrics.minHeight) {
        bounds.height = desktop->windowMetrics.minHeight;
    }
    if (DesktopGeometryContain(bounds, &desktop->geometry.workArea, &clamped) != RoboOk) {
        return RoboDesktopDiag(diag, RoboDesktopDomainWindow, RoboError,
                                 "the window's bounds could not be clamped into the work area");
    }
    if (GfxRectEqual(window->bounds, clamped)) {
        /* An unchanged size is not a no-op if the *client* area changed -- it cannot have, since the
         * decoration metrics are fixed -- so nothing has to be invalidated either way. */
        return RoboOk;
    }
    window->bounds = clamped;
    window->contentDirty = 1;
    window->frameDirty = 1;
    /* The content target is deliberately **not** recreated here: `robo_desktop_window_content_target`
     * rebuilds it when a frame finds it stale, so resizing eight windows allocates nothing until they are
     * painted (see `RoboDesktopInternal.h`). */
    return RoboOk;
}

Result DesktopWindowSetTitle(Desktop *desktop,
                                            DesktopWindowId id,
                                            const char *utf8Title,
                                            DesktopDiag *diag)
{
    DesktopWindow *window;

    if (!desktop) {
        return RoboDesktopDiag(diag, RoboDesktopDomainWindow, RoboInvalidArgument,
                                 "a desktop is required");
    }
    window = DesktopWindowFind(desktop, id);
    if (!window) {
        return RoboDesktopDiag(diag, RoboDesktopDomainWindow, RoboNotFound,
                                 "no such window");
    }
    if (DesktopStringCopy(window->title, sizeof(window->title), utf8Title) != RoboOk) {
        return RoboDesktopDiag(diag, RoboDesktopDomainWindow, RoboInvalidArgument,
                                 "the title does not fit");
    }
    return RoboOk;
}

const char *DesktopWindowTitle(const Desktop *desktop, DesktopWindowId id)
{
    const DesktopWindow *window = DesktopWindowFindConst(desktop, id);

    return window ? window->title : "";
}

Result DesktopWindowSetContent(Desktop *desktop,
                                              DesktopWindowId id,
                                              const PaintDisplayList *content,
                                              FontFace *contentFace,
                                              DesktopDiag *diag)
{
    DesktopWindow *window;

    if (!desktop) {
        return RoboDesktopDiag(diag, RoboDesktopDomainWindow, RoboInvalidArgument,
                                 "a desktop is required");
    }
    window = DesktopWindowFind(desktop, id);
    if (!window) {
        return RoboDesktopDiag(diag, RoboDesktopDomainWindow, RoboNotFound,
                                 "no such window");
    }
    /* Borrowed, not copied: the list belongs to whoever built it, and a copy would be a snapshot the
     * caller could not invalidate without telling the shell. */
    window->content = content;
    window->contentFace = contentFace;
    window->contentDirty = 1;
    return RoboOk;
}

const PaintDisplayList *DesktopWindowContent(const Desktop *desktop,
                                                            DesktopWindowId id)
{
    const DesktopWindow *window = DesktopWindowFindConst(desktop, id);

    return window ? window->content : NULL;
}

Result DesktopWindowSetVisible(Desktop *desktop,
                                              DesktopWindowId id,
                                              int visible,
                                              DesktopDiag *diag)
{
    DesktopWindow *window;

    if (!desktop) {
        return RoboDesktopDiag(diag, RoboDesktopDomainWindow, RoboInvalidArgument,
                                 "a desktop is required");
    }
    window = DesktopWindowFind(desktop, id);
    if (!window) {
        return RoboDesktopDiag(diag, RoboDesktopDomainWindow, RoboNotFound,
                                 "no such window");
    }
    window->visible = visible ? 1 : 0;
    window->frameDirty = 1;
    return RoboOk;
}

int DesktopWindowIsVisible(const Desktop *desktop, DesktopWindowId id)
{
    const DesktopWindow *window = DesktopWindowFindConst(desktop, id);

    return (window && window->visible) ? 1 : 0;
}

Result DesktopWindowRaise(Desktop *desktop, DesktopWindowId id)
{
    size_t index;
    DesktopWindow moved;
    size_t cursor;

    if (!desktop) {
        return RoboInvalidArgument;
    }
    index = DesktopWindowIndexOf(desktop, id);
    if (index == RoboDesktopNoIndex) {
        return RoboNotFound;
    }
    if (index + 1u != desktop->windowCount) {
        size_t oldFocused = desktop->focusedIndex;

        moved = desktop->windows[index];
        for (cursor = index; cursor + 1u < desktop->windowCount; cursor++) {
            desktop->windows[cursor] = desktop->windows[cursor + 1u];
        }
        desktop->windows[desktop->windowCount - 1u] = moved;
        DesktopComposeInvalidateWindowOrder(desktop);
        if (oldFocused != RoboDesktopNoIndex && oldFocused < desktop->windowCount) {
            /* After the shift, the previous front (or prior focus) needs an inactive chrome redraw. */
            desktop->windows[oldFocused < index ? oldFocused : oldFocused - 1u].frameDirty = 1;
        }
        desktop->windows[desktop->windowCount - 1u].frameDirty = 1;
    }
    /* Focus follows the raise; see the header of `RoboDesktop.h`'s raise door. */
    desktop->focusedIndex = desktop->windowCount - 1u;
    return RoboOk;
}

Result DesktopWindowFocus(Desktop *desktop, DesktopWindowId id)
{
    size_t index;
    size_t oldFocused;

    if (!desktop) {
        return RoboInvalidArgument;
    }
    index = DesktopWindowIndexOf(desktop, id);
    if (index == RoboDesktopNoIndex) {
        return RoboNotFound;
    }
    oldFocused = desktop->focusedIndex;
    if (oldFocused != index && oldFocused != RoboDesktopNoIndex &&
        oldFocused < desktop->windowCount) {
        desktop->windows[oldFocused].frameDirty = 1;
    }
    desktop->windows[index].frameDirty = 1;
    desktop->focusedIndex = index;
    return RoboOk;
}

DesktopWindowId DesktopWindowFocused(const Desktop *desktop)
{
    if (!desktop || desktop->focusedIndex == RoboDesktopNoIndex ||
        desktop->focusedIndex >= desktop->windowCount) {
        return RoboDesktopWindowIdNone;
    }
    return desktop->windows[desktop->focusedIndex].id;
}

Result DesktopWindowRequestClose(Desktop *desktop, DesktopWindowId id)
{
    DesktopWindow *window;

    if (!desktop) {
        return RoboInvalidArgument;
    }
    window = DesktopWindowFind(desktop, id);
    if (!window) {
        return RoboNotFound;
    }
    if (window->state != RoboDesktopWindowClosing) {
        window->state = RoboDesktopWindowClosing;
        /* Hidden as well as closing: a window the user asked to close must stop being painted at once,
         * and a state that only the owner reads would leave it on screen until the owner noticed. */
        window->visible = 0;
        desktop->stats.windowsClosed++;
        LogDebug("robo_desktop: window close requested id=%llu",
                      (unsigned long long)window->id);
    }
    return RoboOk;
}

Result GetDesktopWindowState(const Desktop *desktop,
                                        DesktopWindowId id,
                                        DesktopWindowState *out)
{
    const DesktopWindow *window;

    if (!desktop || !out) {
        return RoboInvalidArgument;
    }
    window = DesktopWindowFindConst(desktop, id);
    if (!window) {
        return RoboNotFound;
    }
    *out = window->state;
    return RoboOk;
}

Result DesktopWindowDestroy(Desktop *desktop, DesktopWindowId id)
{
    size_t index;
    size_t cursor;

    if (!desktop) {
        return RoboInvalidArgument;
    }
    index = DesktopWindowIndexOf(desktop, id);
    if (index == RoboDesktopNoIndex) {
        return RoboNotFound;
    }
    DesktopWindowReleaseTarget(&desktop->windows[index]);
    for (cursor = index; cursor + 1u < desktop->windowCount; cursor++) {
        desktop->windows[cursor] = desktop->windows[cursor + 1u];
    }
    desktop->windowCount--;
    DesktopComposeInvalidateWindowOrder(desktop);
    /* The vacated slot is cleared so a later `add` cannot inherit a target that has been released. */
    {
        DesktopWindow *last = &desktop->windows[desktop->windowCount];
        size_t bytes = sizeof(*last);
        size_t byte;

        for (byte = 0u; byte < bytes; byte++) {
            ((char *)last)[byte] = 0;
        }
    }
    if (desktop->focusedIndex == index) {
        desktop->focusedIndex = RoboDesktopNoIndex;
    } else if (desktop->focusedIndex != RoboDesktopNoIndex && desktop->focusedIndex > index) {
        desktop->focusedIndex--;
    }
    DesktopWindowRepairFocus(desktop);
    desktop->stats.windowsDestroyed++;
    LogDebug("robo_desktop: window destroyed id=%llu (count=%llu, focused=%llu)",
                   (unsigned long long)id, (unsigned long long)desktop->windowCount,
                   (unsigned long long)DesktopWindowFocused(desktop));
    return RoboOk;
}

Result DesktopWindowBounds(const Desktop *desktop,
                                         DesktopWindowId id,
                                         GfxRect *out)
{
    const DesktopWindow *window;

    if (!desktop || !out) {
        return RoboInvalidArgument;
    }
    window = DesktopWindowFindConst(desktop, id);
    if (!window) {
        return RoboNotFound;
    }
    *out = window->bounds;
    return RoboOk;
}

Result DesktopWindowClientBounds(const Desktop *desktop,
                                                DesktopWindowId id,
                                                GfxRect *out)
{
    const DesktopWindow *window;

    if (!desktop || !out) {
        return RoboInvalidArgument;
    }
    window = DesktopWindowFindConst(desktop, id);
    if (!window) {
        return RoboNotFound;
    }
    *out = DesktopChromeClientBounds(desktop, window);
    return RoboOk;
}
