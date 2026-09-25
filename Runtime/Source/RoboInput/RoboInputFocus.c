/*
 * Focus (Phase 13.25--13.28) — see `RoboInputFocus.h`.
 *
 * Four pieces: the reason vocabulary, the transition record (and the event list it produces), the manager
 * that keeps one focus per window, and the navigation chain. The single rule that holds them together is
 * that **a transition is computed before anything is stored**: every door builds the both-ends record from
 * the state it is about to replace, then installs the new state. A door that stored first and reported
 * afterwards could not name what it had defocused, and the caller would have no `FOCUS_OUT` to dispatch.
 */

#include "RoboInput/RoboInputFocus.h"

#include "RoboInputInternal.h"

#include <string.h>

/* ============================================================================================== *
 * The reason vocabulary
 * ============================================================================================== */

static const char *const RiFocusReasonNames[RoboInputFocusReasonCount] = {
    "none", "keyboard", "pointer", "programmatic", "navigation", "window-activation", "accessibility"
};

int InputFocusReasonIsKnown(InputFocusReason reason)
{
    return (int)reason >= 0 && (size_t)reason < (size_t)RoboInputFocusReasonCount;
}

const char *InputFocusReasonName(InputFocusReason reason)
{
    if (!InputFocusReasonIsKnown(reason)) {
        return "unknown";
    }
    return RiFocusReasonNames[(size_t)reason];
}

int InputFocusDirectionIsSupported(InputFocusDirection direction)
{
    return direction == RoboInputFocusDirectionNext || direction == RoboInputFocusDirectionPrevious ||
           direction == RoboInputFocusDirectionFirst || direction == RoboInputFocusDirectionLast;
}

/* ============================================================================================== *
 * The transition
 * ============================================================================================== */

void InputFocusTransitionInit(InputFocusTransition *transition)
{
    if (!transition) {
        return;
    }
    memset(transition, 0, sizeof(*transition));
    InputTargetInit(&transition->previous);
    InputTargetInit(&transition->focused);
    transition->reason = RoboInputFocusReasonNone;
}

int InputFocusTransitionHasFocus(const InputFocusTransition *transition)
{
    if (!transition) {
        return 0;
    }
    return transition->hasPrevious || transition->hasFocused;
}

uint32_t InputFocusTransitionEventCount(const InputFocusTransition *transition)
{
    uint32_t count = 0u;

    if (!transition) {
        return 0u;
    }
    /* Deactivate before activate, out before in: the order the shell's own listeners need. */
    if (transition->hasDeactivated) {
        count++;
    }
    if (transition->hasActivated) {
        count++;
    }
    if (transition->hasPrevious) {
        count++;
    }
    if (transition->hasFocused) {
        count++;
    }
    return count;
}

uint32_t InputFocusTransitionEvents(const InputFocusTransition *transition,
                                    InputEvent *out,
                                    uint32_t capacity)
{
    uint32_t written = 0u;
    InputDiag diag;

    if (!transition || !out || capacity == 0u) {
        return 0u;
    }
    InputDiagReset(&diag);

    if (transition->hasDeactivated && written < capacity) {
        InputEvent *event = &out[written];

        InputEventInit(event);
        if (InputEventSetFocusOrWindow(event, RoboInputEventWindowDeactivate, RoboInputSourceKeyboard,
                                       &diag) == RoboOk) {
            InputEventSetWindow(event, transition->deactivated);
            written++;
        }
    }
    if (transition->hasActivated && written < capacity) {
        InputEvent *event = &out[written];

        InputEventInit(event);
        if (InputEventSetFocusOrWindow(event, RoboInputEventWindowActivate, RoboInputSourceKeyboard,
                                       &diag) == RoboOk) {
            InputEventSetWindow(event, transition->activated);
            written++;
        }
    }
    if (transition->hasPrevious && written < capacity) {
        InputEvent *event = &out[written];

        InputEventInit(event);
        if (InputEventSetFocusOrWindow(event, RoboInputEventFocusOut, RoboInputSourceKeyboard,
                                       &diag) == RoboOk) {
            /*
             * The target is the *document tip* when there is one, which is what a listener wants to be
             * told: a `FOCUS_OUT` delivered to a window would tell the window about itself.
             */
            InputEventSetTarget(event, &transition->previous, &diag);
            InputEventSetWindow(event, transition->previous.window);
            written++;
        }
    }
    if (transition->hasFocused && written < capacity) {
        InputEvent *event = &out[written];

        InputEventInit(event);
        if (InputEventSetFocusOrWindow(event, RoboInputEventFocusIn, RoboInputSourceKeyboard,
                                       &diag) == RoboOk) {
            InputEventSetTarget(event, &transition->focused, &diag);
            InputEventSetWindow(event, transition->focused.window);
            written++;
        }
    }
    return written;
}

/* ============================================================================================== *
 * The manager
 * ============================================================================================== */

void InputFocusManagerInit(InputFocusManager *manager)
{
    if (!manager) {
        return;
    }
    memset(manager, 0, sizeof(*manager));
}

static InputFocusScope *RiFocusScopeFind(InputFocusManager *manager, WindowId window)
{
    uint32_t index;

    if (!manager) {
        return NULL;
    }
    for (index = 0u; index < RoboInputMaxFocusScopes; index++) {
        if (manager->scopes[index].active && manager->scopes[index].window == window) {
            return &manager->scopes[index];
        }
    }
    return NULL;
}

static InputFocusScope *RiFocusScopeEnsure(InputFocusManager *manager, WindowId window)
{
    InputFocusScope *scope = RiFocusScopeFind(manager, window);
    uint32_t index;

    if (scope) {
        return scope;
    }
    for (index = 0u; index < RoboInputMaxFocusScopes; index++) {
        if (!manager->scopes[index].active) {
            memset(&manager->scopes[index], 0, sizeof(manager->scopes[index]));
            manager->scopes[index].active = 1;
            manager->scopes[index].window = window;
            InputTargetInit(&manager->scopes[index].target);
            manager->count++;
            return &manager->scopes[index];
        }
    }
    return NULL;
}

/** Publish the active scope's answer into `focused` / `hasFocus`. The table stays the only storage. */
static void RiFocusPublish(InputFocusManager *manager)
{
    InputFocusScope *scope = manager->activeWindow != 0u
                                 ? RiFocusScopeFind(manager, manager->activeWindow)
                                 : NULL;

    if (scope && InputTargetIsValid(&scope->target)) {
        manager->focused = scope->target;
        manager->hasFocus = 1;
        return;
    }
    InputTargetInit(&manager->focused);
    manager->hasFocus = 0;
}

WindowId InputFocusManagerActiveWindow(const InputFocusManager *manager)
{
    return manager ? manager->activeWindow : 0u;
}

Result InputFocusManagerSetActiveWindow(InputFocusManager *manager,
                                        WindowId window,
                                        InputApplicationId application,
                                        InputFocusTransition *out,
                                        InputDiag *diag)
{
    InputFocusScope *scope;

    if (out) {
        InputFocusTransitionInit(out);
    }
    if (!manager) {
        return RoboInputDiag(diag, RoboInputDomainFocus, RoboInvalidArgument,
                             "a focus manager is required");
    }
    if (window == 0u) {
        manager->refused++;
        return RoboInputDiag(diag, RoboInputDomainFocus, RoboInvalidArgument,
                             "a zero window id names no scope");
    }
    if (manager->activeWindow == window) {
        /* The same window is already active: nothing changed, and saying so again would produce a
         * `WindowActivate` per frame. */
        if (diag) {
            InputDiagReset(diag);
        }
        return RoboOk;
    }

    scope = RiFocusScopeEnsure(manager, window);
    if (!scope) {
        manager->refused++;
        return RoboInputDiag(diag, RoboInputDomainFocus, RoboOutOfMemory,
                             "the scope table is full");
    }

    if (out) {
        /*
         * The both-ends record, built *before* anything is stored. Only the focus half is known here: the
         * activation half names a window, not a target, and is filled in below.
         */
        if (manager->hasFocus) {
            out->hasPrevious = 1;
            out->previous = manager->focused;
        }
        out->hasDeactivated = manager->activeWindow != 0u ? 1 : 0;
        out->deactivated = manager->activeWindow;
        out->hasActivated = 1;
        out->activated = window;
        out->reason = RoboInputFocusReasonWindowActivation;
    }

    manager->activeWindow = window;
    manager->activeApplication = application;
    RiFocusPublish(manager);

    if (out && manager->hasFocus) {
        /* The new scope's remembered focus comes back -- unless it is the same target that just lost
         * focus, which happens when a caller re-announces the window it was already in. */
        if (!out->hasPrevious || !InputTargetEqual(&out->previous, &manager->focused)) {
            out->hasFocused = 1;
            out->focused = manager->focused;
        } else {
            out->hasPrevious = 0;
            InputTargetInit(&out->previous);
        }
    }
    manager->changes++;
    if (diag) {
        InputDiagReset(diag);
    }
    return RoboOk;
}

Result InputFocusRequest(InputFocusManager *manager,
                         const InputTarget *target,
                         InputFocusReason reason,
                         InputFocusTransition *out,
                         InputDiag *diag)
{
    InputFocusScope *scope;

    if (out) {
        InputFocusTransitionInit(out);
    }
    if (!manager) {
        return RoboInputDiag(diag, RoboInputDomainFocus, RoboInvalidArgument,
                             "a focus manager is required");
    }
    if (!InputTargetIsValid(target)) {
        manager->refused++;
        return RoboInputDiag(diag, RoboInputDomainFocus, RoboInvalidArgument,
                             "only a valid target may take focus");
    }
    if (!InputFocusReasonIsKnown(reason) || reason == RoboInputFocusReasonNone) {
        manager->refused++;
        return RoboInputDiag(diag, RoboInputDomainFocus, RoboInvalidArgument,
                             "a focus change names a reason");
    }
    if (manager->activeWindow == 0u) {
        manager->refused++;
        return RoboInputDiag(diag, RoboInputDomainFocus, RoboNotFound,
                             "no window is active, so there is no scope to focus in");
    }
    /*
     * A focus that belonged to a window the shell is not showing could never receive a key, so it is refused
     * rather than stored where nothing would look. This is the check that keeps a background window's
     * control from stealing focus by asking.
     */
    if (!InputTargetIsWindow(target, manager->activeWindow)) {
        manager->refused++;
        return RoboInputDiag(diag, RoboInputDomainFocus, RoboInvalidArgument,
                             "the target does not belong to the active window");
    }
    /* An application target is a window-scope answer, not a focusable node: a key routed to "the app" has
     * nowhere to go. The document tip or the window itself is what a caller focuses instead. */
    if (target->kind == RoboInputTargetApplication) {
        manager->refused++;
        return RoboInputDiag(diag, RoboInputDomainFocus, RoboInvalidArgument,
                             "an application is not a focusable target");
    }

    scope = RiFocusScopeEnsure(manager, manager->activeWindow);
    if (!scope) {
        manager->refused++;
        return RoboInputDiag(diag, RoboInputDomainFocus, RoboOutOfMemory,
                             "the scope table is full");
    }

    if (scope->active && InputTargetIsValid(&scope->target) &&
        InputTargetEqual(&scope->target, target)) {
        /* Already focused. The empty transition: the idempotence a press on a slider needs. */
        if (diag) {
            InputDiagReset(diag);
        }
        return RoboOk;
    }

    if (out) {
        if (InputTargetIsValid(&scope->target)) {
            out->hasPrevious = 1;
            out->previous = scope->target;
        }
        out->hasFocused = 1;
        out->focused = *target;
        out->reason = reason;
    }

    scope->target = *target;
    manager->changes++;
    RiFocusPublish(manager);
    if (diag) {
        InputDiagReset(diag);
    }
    return RoboOk;
}

Result InputFocusManagerClear(InputFocusManager *manager,
                              InputFocusReason reason,
                              InputFocusTransition *out,
                              InputDiag *diag)
{
    InputFocusScope *scope;

    if (out) {
        InputFocusTransitionInit(out);
    }
    if (!manager) {
        return RoboInputDiag(diag, RoboInputDomainFocus, RoboInvalidArgument,
                             "a focus manager is required");
    }
    scope = manager->activeWindow != 0u ? RiFocusScopeFind(manager, manager->activeWindow) : NULL;
    if (!scope || !InputTargetIsValid(&scope->target)) {
        /* Nothing focused, so nothing to clear -- and nothing to report. */
        manager->hasFocus = 0;
        InputTargetInit(&manager->focused);
        if (diag) {
            InputDiagReset(diag);
        }
        return RoboOk;
    }

    if (out) {
        out->hasPrevious = 1;
        out->previous = scope->target;
        out->reason = reason;
    }
    InputTargetInit(&scope->target);
    manager->changes++;
    RiFocusPublish(manager);
    if (diag) {
        InputDiagReset(diag);
    }
    return RoboOk;
}

const InputTarget *InputFocusManagerTarget(const InputFocusManager *manager)
{
    if (!manager || !manager->hasFocus) {
        return NULL;
    }
    return &manager->focused;
}

const InputTarget *InputFocusManagerScope(const InputFocusManager *manager, WindowId window)
{
    uint32_t index;

    if (!manager || window == 0u) {
        return NULL;
    }
    for (index = 0u; index < RoboInputMaxFocusScopes; index++) {
        if (manager->scopes[index].active && manager->scopes[index].window == window &&
            InputTargetIsValid(&manager->scopes[index].target)) {
            return &manager->scopes[index].target;
        }
    }
    return NULL;
}

int InputFocusManagerIsFocused(const InputFocusManager *manager, const InputTarget *target)
{
    return manager && manager->hasFocus && InputTargetEqual(&manager->focused, target) ? 1 : 0;
}

uint32_t InputFocusManagerReleaseTarget(InputFocusManager *manager, const InputTarget *target)
{
    uint32_t index;
    uint32_t removed = 0u;

    if (!manager || !target) {
        return 0u;
    }
    for (index = 0u; index < RoboInputMaxFocusScopes; index++) {
        if (manager->scopes[index].active &&
            InputTargetEqual(&manager->scopes[index].target, target)) {
            InputTargetInit(&manager->scopes[index].target);
            removed++;
        }
    }
    if (removed > 0u) {
        RiFocusPublish(manager);
        manager->changes++;
    }
    return removed;
}

int InputFocusManagerReleaseWindow(InputFocusManager *manager, WindowId window)
{
    InputFocusScope *scope;

    if (!manager || window == 0u) {
        return 0;
    }
    scope = RiFocusScopeFind(manager, window);
    if (!scope) {
        return 0;
    }
    memset(scope, 0, sizeof(*scope));
    if (manager->count > 0u) {
        manager->count--;
    }
    if (manager->activeWindow == window) {
        manager->activeWindow = 0u;
        manager->activeApplication = RoboInputApplicationIdNone;
    }
    RiFocusPublish(manager);
    manager->changes++;
    return 1;
}

uint32_t InputFocusManagerClearAll(InputFocusManager *manager)
{
    uint32_t removed;

    if (!manager) {
        return 0u;
    }
    removed = manager->count;
    memset(manager->scopes, 0, sizeof(manager->scopes));
    manager->count = 0u;
    manager->activeWindow = 0u;
    manager->activeApplication = RoboInputApplicationIdNone;
    InputTargetInit(&manager->focused);
    manager->hasFocus = 0;
    return removed;
}

/* ============================================================================================== *
 * Navigation
 * ============================================================================================== */

void InputFocusChainInit(InputFocusChain *chain)
{
    if (!chain) {
        return;
    }
    memset(chain, 0, sizeof(*chain));
}

int InputFocusChainIndexOf(const InputFocusChain *chain, const InputTarget *target)
{
    uint32_t index;

    if (!chain || !target) {
        return -1;
    }
    for (index = 0u; index < chain->count; index++) {
        if (InputTargetEqual(&chain->targets[index], target)) {
            return (int)index;
        }
    }
    return -1;
}

Result InputFocusChainAdd(InputFocusChain *chain, const InputTarget *target, InputDiag *diag)
{
    if (!chain) {
        return RoboInputDiag(diag, RoboInputDomainFocus, RoboInvalidArgument,
                             "a focus chain is required");
    }
    if (!InputTargetIsValid(target)) {
        return RoboInputDiag(diag, RoboInputDomainFocus, RoboInvalidArgument,
                             "only a valid target is focusable");
    }
    if (InputFocusChainIndexOf(chain, target) >= 0) {
        /* A caller registering the same target twice means "this is focusable", not "visit it twice". */
        chain->duplicates++;
        if (diag) {
            InputDiagReset(diag);
        }
        return RoboOk;
    }
    if (chain->count >= RoboInputMaxFocusChain) {
        return RoboInputDiag(diag, RoboInputDomainFocus, RoboOutOfMemory,
                             "the focus chain is full");
    }
    chain->targets[chain->count] = *target;
    chain->count++;
    if (diag) {
        InputDiagReset(diag);
    }
    return RoboOk;
}

int InputFocusChainRemove(InputFocusChain *chain, const InputTarget *target)
{
    int index;
    uint32_t move;

    if (!chain || !target) {
        return -1;
    }
    index = InputFocusChainIndexOf(chain, target);
    if (index < 0) {
        return -1;
    }
    for (move = (uint32_t)index; move + 1u < chain->count; move++) {
        chain->targets[move] = chain->targets[move + 1u];
    }
    chain->count--;
    InputTargetInit(&chain->targets[chain->count]);
    return index;
}

const InputTarget *InputFocusChainPeek(const InputFocusChain *chain,
                                       const InputTarget *current,
                                       InputFocusDirection direction)
{
    int index;

    if (!chain || chain->count == 0u) {
        return NULL;
    }
    index = current ? InputFocusChainIndexOf(chain, current) : -1;

    if (direction == RoboInputFocusDirectionFirst) {
        return &chain->targets[0];
    }
    if (direction == RoboInputFocusDirectionLast) {
        return &chain->targets[chain->count - 1u];
    }
    if (direction == RoboInputFocusDirectionNext) {
        /*
         * From nowhere, `Tab` enters at the first entry; from the last, it *stays* -- a chain that wrapped
         * would move focus out of the caller's window, and "what comes after the last control" is a question
         * about the tree rather than about this list.
         */
        if (index < 0) {
            return &chain->targets[0];
        }
        if ((uint32_t)index + 1u < chain->count) {
            return &chain->targets[(uint32_t)index + 1u];
        }
        return &chain->targets[chain->count - 1u];
    }
    if (direction == RoboInputFocusDirectionPrevious) {
        if (index < 0) {
            return &chain->targets[chain->count - 1u];
        }
        if (index == 0) {
            return &chain->targets[0];
        }
        return &chain->targets[(uint32_t)index - 1u];
    }
    return NULL;
}

Result InputFocusChainFocus(InputFocusManager *manager,
                            const InputFocusChain *chain,
                            InputFocusDirection direction,
                            InputFocusReason reason,
                            InputFocusTransition *out,
                            InputDiag *diag)
{
    const InputTarget *next;

    if (out) {
        InputFocusTransitionInit(out);
    }
    if (!manager || !chain) {
        return RoboInputDiag(diag, RoboInputDomainFocus, RoboInvalidArgument,
                             "a manager and a chain are required");
    }
    if (!InputFocusDirectionIsSupported(direction)) {
        return RoboInputDiag(diag, RoboInputDomainFocus, RoboNotSupported,
                             "this build walks a chain forward and backward, not geometrically");
    }
    if (chain->count == 0u) {
        return RoboInputDiag(diag, RoboInputDomainFocus, RoboNotFound,
                             "the chain has no focusable target");
    }

    next = InputFocusChainPeek(chain, InputFocusManagerTarget(manager), direction);
    if (!next) {
        return RoboInputDiag(diag, RoboInputDomainFocus, RoboNotFound,
                             "the direction names no entry");
    }

    /*
     * The target that came *from* the chain belongs to the active window; one that does not is refused by
     * the request door below, which is the honest answer -- a chain spanning windows is the shell's, not
     * this list's.
     */
    return InputFocusRequest(manager, next, reason, out, diag);
}

/* ============================================================================================== *
 * Diagnostics
 * ============================================================================================== */

Result InputFocusTransitionWrite(const InputFocusTransition *transition,
                                 char *buffer,
                                 size_t capacity,
                                 size_t *outLength,
                                 InputDiag *diag)
{
    InputWriter text;

    if (!RiWriterArgumentsAreValid(buffer, capacity, outLength)) {
        return RoboInputDiag(diag, RoboInputDomainDump, RoboInvalidArgument,
                             "a dump needs a buffer, a capacity and somewhere to put the length");
    }
    RiWriterInit(&text, buffer, capacity);
    if (!transition) {
        RiWriterText(&text, "focus-transition <none>\n");
        return RiWriterFinish(&text, outLength, diag);
    }

    RiWriterText(&text, "focus-transition reason=");
    RiWriterText(&text, InputFocusReasonName(transition->reason));
    RiWriterFieldU64(&text, " previous", (uint64_t)(transition->hasPrevious ? 1u : 0u));
    RiWriterFieldU64(&text, " focused", (uint64_t)(transition->hasFocused ? 1u : 0u));
    RiWriterFieldU64(&text, " deactivated", (uint64_t)transition->deactivated);
    RiWriterFieldU64(&text, " activated", (uint64_t)transition->activated);
    RiWriterFieldU64(&text, " events", (uint64_t)InputFocusTransitionEventCount(transition));
    RiWriterChar(&text, '\n');
    return RiWriterFinish(&text, outLength, diag);
}

Result InputFocusManagerWrite(const InputFocusManager *manager,
                              char *buffer,
                              size_t capacity,
                              size_t *outLength,
                              InputDiag *diag)
{
    InputWriter text;
    uint32_t index;

    if (!RiWriterArgumentsAreValid(buffer, capacity, outLength)) {
        return RoboInputDiag(diag, RoboInputDomainDump, RoboInvalidArgument,
                             "a dump needs a buffer, a capacity and somewhere to put the length");
    }
    RiWriterInit(&text, buffer, capacity);
    if (!manager) {
        RiWriterText(&text, "focus <none>\n");
        return RiWriterFinish(&text, outLength, diag);
    }

    RiWriterText(&text, "focus scopes=");
    RiWriterU64(&text, (uint64_t)manager->count);
    RiWriterFieldU64(&text, " active", (uint64_t)manager->activeWindow);
    RiWriterFieldText(&text, " focused",
                      manager->hasFocus ? InputTargetKindName(manager->focused.kind) : "none");
    RiWriterFieldU64(&text, " changes", (uint64_t)manager->changes);
    RiWriterFieldU64(&text, " refused", (uint64_t)manager->refused);
    RiWriterChar(&text, '\n');

    for (index = 0u; index < RoboInputMaxFocusScopes; index++) {
        if (!manager->scopes[index].active) {
            continue;
        }
        RiWriterText(&text, "  scope window=");
        RiWriterU64(&text, (uint64_t)manager->scopes[index].window);
        RiWriterFieldText(&text, " focus",
                          InputTargetIsValid(&manager->scopes[index].target)
                              ? InputTargetKindName(manager->scopes[index].target.kind)
                              : "none");
        RiWriterChar(&text, '\n');
    }
    return RiWriterFinish(&text, outLength, diag);
}

Result InputFocusChainWrite(const InputFocusChain *chain,
                            char *buffer,
                            size_t capacity,
                            size_t *outLength,
                            InputDiag *diag)
{
    InputWriter text;
    uint32_t index;

    if (!RiWriterArgumentsAreValid(buffer, capacity, outLength)) {
        return RoboInputDiag(diag, RoboInputDomainDump, RoboInvalidArgument,
                             "a dump needs a buffer, a capacity and somewhere to put the length");
    }
    RiWriterInit(&text, buffer, capacity);
    if (!chain) {
        RiWriterText(&text, "focus-chain <none>\n");
        return RiWriterFinish(&text, outLength, diag);
    }

    RiWriterText(&text, "focus-chain count=");
    RiWriterU64(&text, (uint64_t)chain->count);
    RiWriterFieldU64(&text, " duplicates", (uint64_t)chain->duplicates);
    RiWriterChar(&text, '\n');
    for (index = 0u; index < chain->count; index++) {
        RiWriterText(&text, "  entry #");
        RiWriterU64(&text, (uint64_t)index);
        RiWriterFieldText(&text, " kind", InputTargetKindName(chain->targets[index].kind));
        RiWriterFieldU64(&text, " window", (uint64_t)chain->targets[index].window);
        RiWriterFieldU64(&text, " node", (uint64_t)chain->targets[index].layoutNode);
        RiWriterChar(&text, '\n');
    }
    return RiWriterFinish(&text, outLength, diag);
}
