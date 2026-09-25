/*
 * Dispatch (Phase 13.29--13.36) — see `RoboInputDispatch.h`.
 *
 * Four pieces: the hit test (one provider call, with a window-only fallback), the path (one provider call,
 * with a one-entry fallback), the propagation context (the only thing a listener may write), and the
 * dispatcher that walks the path three times. The default-action table is last because it is a *reading* of
 * an event rather than a step of the walk.
 */

#include "RoboInput/RoboInputDispatch.h"

#include "RoboInputInternal.h"

#include <string.h>

/* ============================================================================================== *
 * Hit testing
 * ============================================================================================== */

static const char *const RiPhaseNames[RoboInputPhaseCount] = { "capture", "target", "bubble" };

int InputPhaseIsKnown(InputPhase phase)
{
    return (int)phase >= 0 && (size_t)phase < (size_t)RoboInputPhaseCount;
}

const char *InputPhaseName(InputPhase phase)
{
    if (!InputPhaseIsKnown(phase)) {
        return "unknown";
    }
    return RiPhaseNames[(size_t)phase];
}

Result InputHitTestPoint(const InputHitTestProvider *provider,
                         const InputRoute *route,
                         GfxPoint point,
                         InputSpace space,
                         InputHitTestResult *out,
                         InputDiag *diag)
{
    InputTarget windowTarget;
    Result status;

    if (!out) {
        return RoboInputDiag(diag, RoboInputDomainDispatch, RoboInvalidArgument,
                             "a hit test needs somewhere to put its answer");
    }
    memset(out, 0, sizeof(*out));
    InputTargetInit(&out->target);
    if (!route) {
        return RoboInputDiag(diag, RoboInputDomainDispatch, RoboInvalidArgument,
                             "a hit test needs the geometry to convert against");
    }
    /*
     * A point whose relationship to the screen nobody knows cannot be tested against geometry that is in
     * screen space. `APPLICATION` is convertible in the type system only because an application routes in its
     * own plane (12.1); here it is refused, because a window's tree cannot answer for it.
     */
    if (!InputSpaceIsConvertible(space) || space == RoboInputSpaceApplication) {
        return RoboInputDiag(diag, RoboInputDomainDispatch, RoboInvalidArgument,
                             "the point is not in a plane a tree can be hit-tested in");
    }
    out->space = space;

    if (provider && provider->hitTest) {
        status = provider->hitTest(provider->userdata, route, point, space, &out->target, diag);
        if (status == RoboOk) {
            if (!InputTargetIsValid(&out->target)) {
                return RoboInputDiag(diag, RoboInputDomainDispatch, RoboInvalidArgument,
                                     "the hit-test provider answered with an invalid target");
            }
            out->hit = 1;
            return RoboOk;
        }
        if (status != RoboNotFound) {
            /* A provider *failure* propagates: reading it as "no hit" is how a broken tree becomes a silent
             * misfeature. */
            return status;
        }
        InputTargetInit(&out->target);
    }

    /*
     * Nothing claimed the point -- or nothing *can*, because there is no provider. A window-only system has
     * exactly one target under every point, and saying so is more useful than refusing; `fellThrough` is what
     * keeps the two cases apart for a caller that cares.
     */
    if (route->window == 0u) {
        return RoboInputDiag(diag, RoboInputDomainDispatch, RoboNotFound,
                             "the route names no window to fall back to");
    }
    InputTargetInit(&windowTarget);
    status = InputTargetSetWindow(&windowTarget, route->window, RoboInputSpaceWindow, diag);
    if (status != RoboOk) {
        return status;
    }
    out->target = windowTarget;
    out->hit = 1;
    out->fellThrough = 1;
    if (diag) {
        InputDiagReset(diag);
    }
    return RoboOk;
}

/* ============================================================================================== *
 * The event path
 * ============================================================================================== */

void InputEventPathInit(InputEventPath *path)
{
    if (!path) {
        return;
    }
    memset(path, 0, sizeof(*path));
}

Result InputEventPathBuild(const InputPathProvider *provider,
                           const InputTarget *target,
                           InputEventPath *out,
                           InputDiag *diag)
{
    Result status;

    if (!out) {
        return RoboInputDiag(diag, RoboInputDomainDispatch, RoboInvalidArgument,
                             "a path needs somewhere to go");
    }
    InputEventPathInit(out);
    if (!InputTargetIsValid(target)) {
        return RoboInputDiag(diag, RoboInputDomainDispatch, RoboInvalidArgument,
                             "a path is built for a valid target");
    }

    if (provider && provider->buildPath) {
        status = provider->buildPath(provider->userdata, target, out, diag);
        if (status != RoboOk) {
            return status;
        }
        if (out->count == 0u || out->count > RoboInputMaxEventPath) {
            InputEventPathInit(out);
            return RoboInputDiag(diag, RoboInputDomainDispatch, RoboInvalidArgument,
                                 "the path provider answered with an impossible length");
        }
        if (!InputTargetEqual(&out->targets[out->count - 1u], target)) {
            /* An event that never reaches the target it was addressed to is a routing bug this door catches
             * for free, and it is not something a caller should have to notice at the listener. */
            InputEventPathInit(out);
            return RoboInputDiag(diag, RoboInputDomainDispatch, RoboInvalidArgument,
                                 "the path does not end at the target it was built for");
        }
        out->depth = out->count - 1u;
        if (diag) {
            InputDiagReset(diag);
        }
        return RoboOk;
    }

    /* No tree to ask: the target alone. */
    out->targets[0] = *target;
    out->count = 1u;
    out->depth = 0u;
    if (diag) {
        InputDiagReset(diag);
    }
    return RoboOk;
}

const InputTarget *InputEventPathAt(const InputEventPath *path, uint32_t index)
{
    if (!path || index >= path->count) {
        return NULL;
    }
    return &path->targets[index];
}

const InputTarget *InputEventPathTarget(const InputEventPath *path)
{
    if (!path || path->count == 0u) {
        return NULL;
    }
    return &path->targets[path->count - 1u];
}

/* ============================================================================================== *
 * Propagation
 * ============================================================================================== */

void InputEventContextInit(InputEventContext *context)
{
    if (!context) {
        return;
    }
    memset(context, 0, sizeof(*context));
    InputTargetInit(&context->currentTarget);
    context->phase = RoboInputPhaseCapture;
}

Result InputEventContextStopPropagation(InputEventContext *context)
{
    if (!context) {
        return RoboInputDiag(NULL, RoboInputDomainDispatch, RoboInvalidArgument,
                             "a propagation context is required");
    }
    context->stopped = 1;
    return RoboOk;
}

Result InputEventContextStopImmediatePropagation(InputEventContext *context)
{
    if (!context) {
        return RoboInputDiag(NULL, RoboInputDomainDispatch, RoboInvalidArgument,
                             "a propagation context is required");
    }
    context->stopped = 1;
    context->stoppedImmediate = 1;
    return RoboOk;
}

Result InputEventContextPreventDefault(InputEventContext *context)
{
    if (!context) {
        return RoboInputDiag(NULL, RoboInputDomainDispatch, RoboInvalidArgument,
                             "a propagation context is required");
    }
    if (context->passive) {
        /* A passive listener promised it would not cancel. The call is a contract violation rather than a
         * request, so it is refused -- and counted, so a caller can find the listener that did it. */
        context->refusedDefault++;
        return RoboInputDiag(NULL, RoboInputDomainDispatch, RoboInvalidArgument,
                             "a passive listener may not cancel the default action");
    }
    context->defaultPrevented = 1;
    return RoboOk;
}

int InputEventContextIsDefaultPrevented(const InputEventContext *context)
{
    return context && context->defaultPrevented ? 1 : 0;
}

/* ============================================================================================== *
 * The dispatcher
 * ============================================================================================== */

void InputDispatcherInit(InputDispatcher *dispatcher, const InputListenerProvider *listeners)
{
    if (!dispatcher) {
        return;
    }
    memset(dispatcher, 0, sizeof(*dispatcher));
    dispatcher->listeners = listeners;
    dispatcher->lastFailure = RoboOk;
}

/** Call one target's listeners for @p phase. Returns the first callback failure, or RoboOk. */
static Result RiDispatchAtTarget(InputDispatcher *dispatcher,
                                 const InputTarget *target,
                                 const InputEvent *event,
                                 InputPhase phase,
                                 InputEventContext *context)
{
    InputListener batch[RoboInputMaxListeners];
    uint32_t available;
    uint32_t index;

    context->phase = phase;
    context->currentTarget = *target;
    context->passive = 0;

    if (!dispatcher->listeners || !dispatcher->listeners->listenersFor) {
        return RoboOk;
    }
    memset(batch, 0, sizeof(batch));
    available = dispatcher->listeners->listenersFor(dispatcher->listeners->userdata, target, event,
                                                    batch, RoboInputMaxListeners);
    if (available > RoboInputMaxListeners) {
        /* Offered more than the batch holds: *reported*, because a listener silently never called is the one
         * failure a dispatcher cannot afford to hide. */
        dispatcher->dropped += available - RoboInputMaxListeners;
        available = RoboInputMaxListeners;
    }
    available = available < RoboInputMaxListeners ? available : RoboInputMaxListeners;

    for (index = 0u; index < available; index++) {
        Result status;

        if (batch[index].removed || !batch[index].callback) {
            continue;
        }
        if (batch[index].phase != phase) {
            continue;
        }
        context->passive = batch[index].passive;
        context->calls++;
        dispatcher->calls++;
        status = batch[index].callback(batch[index].userdata, event, context);
        if (status != RoboOk) {
            dispatcher->failures++;
            dispatcher->lastFailure = status;
            return status;
        }
        if (context->stoppedImmediate) {
            break;
        }
    }
    context->passive = 0;
    return RoboOk;
}

Result InputDispatch(InputDispatcher *dispatcher,
                     const InputEvent *event,
                     const InputEventPath *path,
                     InputEventContext *out,
                     InputDiag *diag)
{
    uint32_t index;
    Result status;

    if (!out) {
        return RoboInputDiag(diag, RoboInputDomainDispatch, RoboInvalidArgument,
                             "a dispatch needs somewhere to put its state");
    }
    InputEventContextInit(out);
    if (!dispatcher) {
        return RoboInputDiag(diag, RoboInputDomainDispatch, RoboInvalidArgument,
                             "a dispatcher is required");
    }
    if (!event) {
        return RoboInputDiag(diag, RoboInputDomainDispatch, RoboInvalidArgument,
                             "an event is required");
    }
    if (!path || path->count == 0u || path->count > RoboInputMaxEventPath) {
        return RoboInputDiag(diag, RoboInputDomainDispatch, RoboInvalidArgument,
                             "a dispatch needs a built path");
    }

    dispatcher->dispatches++;

    /* Capture: root down to the target's parent. */
    for (index = 0u; index + 1u < path->count && !out->stopped; index++) {
        status = RiDispatchAtTarget(dispatcher, &path->targets[index], event, RoboInputPhaseCapture, out);
        if (status != RoboOk) {
            return status;
        }
    }
    /* Target: always reached unless an ancestor in capture stopped propagation *and* nothing was
     * immediate-stopped -- `stopPropagation` in a capture listener does prevent the target phase, which is
     * the platform rule, so `stopped` is the only test here. */
    if (!out->stopped) {
        out->stoppedImmediate = 0;
        status = RiDispatchAtTarget(dispatcher, &path->targets[path->count - 1u], event,
                                    RoboInputPhaseTarget, out);
        if (status != RoboOk) {
            return status;
        }
    }
    /* Bubble: the parent up to the root. `stoppedImmediate` was decided at the target and stops the walk
     * here too, which is what "immediately" means. */
    out->stoppedImmediate = 0;
    for (index = path->count - 1u; index > 0u && !out->stopped; index--) {
        status = RiDispatchAtTarget(dispatcher, &path->targets[index - 1u], event, RoboInputPhaseBubble,
                                    out);
        if (status != RoboOk) {
            return status;
        }
    }
    if (diag) {
        InputDiagReset(diag);
    }
    return RoboOk;
}

/* ============================================================================================== *
 * The default action
 * ============================================================================================== */

static const char *const RiDefaultActionNames[RoboInputDefaultActionKindCount] = {
    "none", "focus", "scroll", "activate", "navigate", "command"
};

int InputDefaultActionKindIsKnown(InputDefaultActionKind kind)
{
    return (int)kind >= 0 && (size_t)kind < (size_t)RoboInputDefaultActionKindCount;
}

const char *InputDefaultActionKindName(InputDefaultActionKind kind)
{
    if (!InputDefaultActionKindIsKnown(kind)) {
        return "unknown";
    }
    return RiDefaultActionNames[(size_t)kind];
}

InputDefaultAction InputDefaultActionFor(const InputEvent *event,
                                         const InputDefaultAction *request,
                                         int32_t lineHeightPx)
{
    InputDefaultAction action;

    memset(&action, 0, sizeof(action));
    InputTargetInit(&action.target);
    action.kind = RoboInputDefaultActionNone;
    if (!event) {
        return action;
    }
    action.event = event->type;
    action.button = RoboMouseButtonNone;
    action.direction = RoboInputFocusDirectionNone;

    if (event->hasTarget) {
        action.target = event->target;
    } else if (event->window != 0u) {
        /* A window-scope answer is still an answer: a focus or a scroll with no finer target acts on the
         * window, which is what a click on empty space in a shell means. */
        InputDiag diag;

        InputDiagReset(&diag);
        (void)InputTargetSetWindow(&action.target, event->window, event->space, &diag);
    }

    switch (event->type) {
    case RoboInputEventWheel: {
        InputWheelData wheel = event->payload.pointer.wheel;
        InputWheelPixels pixels;

        if (request && (request->deltaX != 0 || request->deltaY != 0)) {
            wheel.deltaX = request->deltaX;
            wheel.deltaY = request->deltaY;
            wheel.unit = RoboMouseScrollUnitPixel;
        }
        pixels = InputWheelToPixels(&wheel, lineHeightPx, 0);
        action.kind = RoboInputDefaultActionScroll;
        action.deltaX = pixels.deltaX;
        action.deltaY = pixels.deltaY;
        return action;
    }

    case RoboInputEventPointerDown:
        /*
         * A *secondary* press does not move focus: a context menu raised over a text field must not take the
         * caret away from it. Left and middle do, which is the rule every platform agrees on.
         */
        if (event->payload.pointer.button == RoboMouseButtonRight) {
            return action;
        }
        action.kind = RoboInputDefaultActionFocus;
        action.button = event->payload.pointer.button;
        return action;

    case RoboInputEventKeyDown:
    case RoboInputEventKeyRepeat: {
        PhysicalKey physical = event->payload.keyboard.physical;
        InputKeyboardData keyboard = event->payload.keyboard;

        action.hasText = keyboard.text[0] != '\0' ? 1 : 0;
        if (physical == RoboPhysicalKeyTab) {
            action.kind = RoboInputDefaultActionNavigate;
            action.direction = (event->modifiers & (InputModifiers)RoboKeyModShift) != 0
                                   ? RoboInputFocusDirectionPrevious
                                   : RoboInputFocusDirectionNext;
            return action;
        }
        if (physical == RoboPhysicalKeyEnter || physical == RoboPhysicalKeyNumpadEnter) {
            action.kind = RoboInputDefaultActionActivate;
            return action;
        }
        if (physical == RoboPhysicalKeySpace) {
            /*
             * Space activates **only when it produced no text**. In a text field it is a space character, and
             * a build that activated on it would press the button under the caret's paragraph every time a
             * user typed one.
             */
            if (action.hasText) {
                action.kind = RoboInputDefaultActionCommand;
                return action;
            }
            action.kind = RoboInputDefaultActionActivate;
            return action;
        }
        if (action.hasText) {
            action.kind = RoboInputDefaultActionCommand;
            return action;
        }
        /* A bare modifier, a function key, a dead accent still waiting: nothing follows from it here, and
         * the shortcut layer above is where a chord becomes a command. */
        return action;
    }

    default:
        return action;
    }
}

/* ============================================================================================== *
 * Diagnostics
 * ============================================================================================== */

Result InputEventPathWrite(const InputEventPath *path,
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
    if (!path) {
        RiWriterText(&text, "path <none>\n");
        return RiWriterFinish(&text, outLength, diag);
    }

    RiWriterText(&text, "path count=");
    RiWriterU64(&text, (uint64_t)path->count);
    RiWriterFieldU64(&text, " depth", (uint64_t)path->depth);
    RiWriterFieldU64(&text, " truncated", (uint64_t)path->truncated);
    RiWriterChar(&text, '\n');
    for (index = 0u; index < path->count; index++) {
        RiWriterText(&text, "  ");
        RiWriterU64(&text, (uint64_t)index);
        RiWriterFieldText(&text, " kind", InputTargetKindName(path->targets[index].kind));
        RiWriterFieldU64(&text, " window", (uint64_t)path->targets[index].window);
        RiWriterFieldU64(&text, " node", (uint64_t)path->targets[index].layoutNode);
        if (index == path->depth) {
            RiWriterText(&text, " target=1");
        }
        RiWriterChar(&text, '\n');
    }
    return RiWriterFinish(&text, outLength, diag);
}

Result InputEventContextWrite(const InputEventContext *context,
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
    if (!context) {
        RiWriterText(&text, "propagation <none>\n");
        return RiWriterFinish(&text, outLength, diag);
    }

    RiWriterText(&text, "propagation phase=");
    RiWriterText(&text, InputPhaseName(context->phase));
    RiWriterFieldText(&text, " current", InputTargetKindName(context->currentTarget.kind));
    RiWriterFieldU64(&text, " calls", (uint64_t)context->calls);
    RiWriterFieldU64(&text, " stopped", (uint64_t)(context->stopped ? 1u : 0u));
    RiWriterFieldU64(&text, " immediate", (uint64_t)(context->stoppedImmediate ? 1u : 0u));
    RiWriterFieldU64(&text, " prevented", (uint64_t)(context->defaultPrevented ? 1u : 0u));
    RiWriterFieldU64(&text, " refused", (uint64_t)context->refusedDefault);
    RiWriterChar(&text, '\n');
    return RiWriterFinish(&text, outLength, diag);
}

Result InputDefaultActionWrite(const InputDefaultAction *action,
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
    if (!action) {
        RiWriterText(&text, "default-action <none>\n");
        return RiWriterFinish(&text, outLength, diag);
    }

    RiWriterText(&text, "default-action ");
    RiWriterText(&text, InputDefaultActionKindName(action->kind));
    RiWriterText(&text, " on=");
    RiWriterText(&text, InputEventTypeName(action->event));
    RiWriterFieldText(&text, " target", InputTargetKindName(action->target.kind));
    RiWriterFieldI32(&text, " dx", action->deltaX);
    RiWriterFieldI32(&text, " dy", action->deltaY);
    RiWriterFieldU64(&text, " button", (uint64_t)action->button);
    RiWriterFieldText(&text, " direction", InputFocusDirectionName(action->direction));
    RiWriterFieldU64(&text, " text", (uint64_t)(action->hasText ? 1u : 0u));
    RiWriterChar(&text, '\n');
    return RiWriterFinish(&text, outLength, diag);
}
