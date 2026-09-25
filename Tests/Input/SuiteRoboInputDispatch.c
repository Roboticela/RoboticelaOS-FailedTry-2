/*
 * RoboInput dispatch suite (Phase 13.29--13.36) — `input.dispatch.*`.
 *
 * Seven cases, one per piece of the seam:
 *
 *   `hit`         the provider that answers "what is under this point", and the three degenerate answers a
 *                 caller must be able to tell apart: a tree that found something, a tree that found nothing
 *                 (a *reachable* window), and no tree at all (`fellThrough`);
 *
 *   `path`        the chain root → target, the one-entry path of a window-only system, and the two
 *                 refusals that catch a routing bug for free -- a path that does not end at its target, and a
 *                 provider that answers with an impossible length;
 *
 *   `propagation` stop, stop-immediately and prevent-default, including the refusal a *passive* listener gets
 *                 and the counter that makes it findable;
 *
 *   `order`       the three phases, with the call order asserted as a string: capture root → parent, the
 *                 target, then bubble parent → root, and the target phase skipped when a capture listener
 *                 stopped propagation;
 *
 *   `dispatcher`  the counters, a callback failure stopping the walk, a provider offering more listeners than
 *                 the batch holds, and a dispatcher with no provider at all;
 *
 *   `default`     the four rules of 13.34: wheel → scroll with the caller's line height, press → focus with a
 *                 secondary button excepted, `Enter` → activate, `Space` → command when it carried text, and
 *                 `Tab` → a named direction;
 *
 *   `dump`        all three dumps, asserted whole and deterministic.
 */

#include "RoboInputTestSupport.h"

/* ------------------------------------------------------------------ */
/* Fixtures: a two-level tree and a recorder                           */
/* ------------------------------------------------------------------ */

typedef struct DispatchPathFixture {
    InputTarget root;
    InputTarget middle;
    InputTarget leaf;
    int levels;
} DispatchPathFixture;

static InputTarget DispatchWindowTarget(WindowId window)
{
    InputTarget target;
    InputDiag diag;

    InputTargetInit(&target);
    if (InputTargetSetWindow(&target, window, RoboInputSpaceWindow, &diag) != RoboOk) {
        RoboCheckMsg(0, "the fixture's window target was refused");
    }
    return target;
}

static InputTarget DispatchNodeTarget(WindowId window, LayoutNodeId node)
{
    InputTarget target;
    InputDiag diag;

    InputTargetInit(&target);
    if (InputTargetSetDocument(&target, window, NULL, node, 0u, RoboInputSpaceWindow, &diag) != RoboOk) {
        RoboCheckMsg(0, "the fixture's document target was refused");
    }
    return target;
}

/* One recorder for every listener in this suite: the order is the subject. */
static char DispatchTrace[128];
static Result DispatchTraceResult;
static uint32_t DispatchCallsMade;

static Result DispatchRecord(void *userdata, const InputEvent *event, InputEventContext *context);

typedef struct DispatchRecorder {
    char tag;
    int passive;
    Result result;
    int stop;
    int stopImmediate;
    int preventDefault;
} DispatchRecorder;

static DispatchRecorder DispatchA = { 'a', 0, RoboOk, 0, 0, 0 };
static DispatchRecorder DispatchB = { 'b', 0, RoboOk, 0, 0, 0 };
static DispatchRecorder DispatchC = { 'c', 0, RoboOk, 0, 0, 0 };

static Result DispatchRecord(void *userdata, const InputEvent *event, InputEventContext *context)
{
    const DispatchRecorder *recorder = (const DispatchRecorder *)userdata;
    size_t length = strlen(DispatchTrace);

    (void)event;
    if (length + 1u < sizeof(DispatchTrace)) {
        DispatchTrace[length] = recorder->tag;
        DispatchTrace[length + 1u] = '\0';
    }
    DispatchCallsMade++;
    context->passive = recorder->passive;
    if (recorder->stop) {
        InputEventContextStopPropagation(context);
    }
    if (recorder->stopImmediate) {
        InputEventContextStopImmediatePropagation(context);
    }
    if (recorder->preventDefault) {
        (void)InputEventContextPreventDefault(context);
    }
    DispatchTraceResult = recorder->result;
    return recorder->result;
}

/* A provider that walks a root → middle → leaf chain. */
static Result DispatchBuildPath(void *userdata,
                                const InputTarget *target,
                                InputEventPath *out,
                                InputDiag *diag)
{
    const DispatchPathFixture *fixture = (const DispatchPathFixture *)userdata;
    uint32_t index;
    uint32_t count;
    const InputTarget *chain[3];

    (void)diag;
    chain[0] = &fixture->root;
    chain[1] = &fixture->middle;
    chain[2] = &fixture->leaf;
    count = fixture->levels == 3 ? 3u : (fixture->levels == 2 ? 2u : 1u);

    for (index = 0u; index < count; index++) {
        out->targets[index] = *chain[index];
    }
    out->count = count;
    out->depth = count - 1u;
    (void)target;
    return RoboOk;
}

/* A provider whose path deliberately ends somewhere else, to prove the door catches it. */
static Result DispatchBuildWrongEnd(void *userdata,
                                    const InputTarget *target,
                                    InputEventPath *out,
                                    InputDiag *diag)
{
    const DispatchPathFixture *fixture = (const DispatchPathFixture *)userdata;

    (void)target;
    (void)diag;
    out->targets[0] = fixture->root;
    out->count = 1u;
    out->depth = 0u;
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Listeners                                                          */
/* ------------------------------------------------------------------ */

typedef struct DispatchListeners {
    InputListener items[RoboInputMaxListeners + 4u];
    uint32_t count;
    uint32_t offered; /* what to claim exists, so the "dropped" path can be exercised */
} DispatchListeners;

static uint32_t DispatchListenersFor(void *userdata,
                                     const InputTarget *target,
                                     const InputEvent *event,
                                     InputListener *out,
                                     uint32_t capacity)
{
    const DispatchListeners *source = (const DispatchListeners *)userdata;
    uint32_t index;
    uint32_t written = 0u;

    (void)event;
    for (index = 0u; index < source->count; index++) {
        if (!InputTargetEqual(&source->items[index].target, target)) {
            continue;
        }
        if (written < capacity) {
            out[written] = source->items[index];
            written++;
        }
    }
    if (source->offered > 0u) {
        return source->offered;
    }
    return written;
}

static void DispatchAddListener(DispatchListeners *listeners,
                                const InputTarget *target,
                                InputPhase phase,
                                DispatchRecorder *recorder)
{
    InputListener *item = &listeners->items[listeners->count];

    memset(item, 0, sizeof(*item));
    item->target = *target;
    item->phase = phase;
    item->passive = recorder->passive;
    item->callback = DispatchRecord;
    item->userdata = recorder;
    listeners->count++;
}

static void DispatchReset(void)
{
    DispatchTrace[0] = '\0';
    DispatchCallsMade = 0u;
    DispatchTraceResult = RoboOk;
    DispatchA.stop = 0;
    DispatchA.stopImmediate = 0;
    DispatchA.preventDefault = 0;
    DispatchA.passive = 0;
    DispatchA.result = RoboOk;
    DispatchB.stop = 0;
    DispatchB.stopImmediate = 0;
    DispatchB.preventDefault = 0;
    DispatchB.passive = 0;
    DispatchB.result = RoboOk;
    DispatchC.stop = 0;
    DispatchC.stopImmediate = 0;
    DispatchC.preventDefault = 0;
    DispatchC.passive = 0;
    DispatchC.result = RoboOk;
}

/* ------------------------------------------------------------------ */
/* Hit testing                                                        */
/* ------------------------------------------------------------------ */

static Result DispatchHitProvider(void *userdata,
                                  const InputRoute *route,
                                  GfxPoint point,
                                  InputSpace space,
                                  InputTarget *outTarget,
                                  InputDiag *diag)
{
    const InputTarget *answer = (const InputTarget *)userdata;

    (void)route;
    (void)space;
    (void)diag;
    /* The tree "finds" its leaf only in the left half of the window; the right half is empty space. */
    if (point.x >= 100.0f) {
        return RoboNotFound;
    }
    *outTarget = *answer;
    return RoboOk;
}

static void TestDispatchHit(void)
{
    InputHitTestProvider provider;
    InputHitTestResult result;
    InputRoute route = GetRoboInputRoute(0, 0, 400, 300, 0, 0, 400, 300);
    InputTarget leaf = DispatchNodeTarget(7u, (LayoutNodeId)9u);
    InputDiag diag;

    InputDiagReset(&diag);

    /* With no provider at all, every point resolves to the route's window -- and says so. */
    RoboCheckEqInt(InputHitTestPoint(NULL, &route, MakeGfxPoint(10, 10), RoboInputSpaceWindow, &result,
                                     &diag),
                   RoboOk);
    RoboCheck(result.hit);
    RoboCheck(result.fellThrough);
    RoboCheckEqInt((int)result.target.kind, (int)RoboInputTargetWindow);
    RoboCheckEqInt(result.target.window, 7);
    RoboCheckEqInt((int)result.space, (int)RoboInputSpaceWindow);

    /* With one, the answer is the tree's. */
    provider.userdata = &leaf;
    provider.hitTest = DispatchHitProvider;
    RoboCheckEqInt(InputHitTestPoint(&provider, &route, MakeGfxPoint(10, 10), RoboInputSpaceWindow,
                                     &result, &diag),
                   RoboOk);
    RoboCheck(result.hit && !result.fellThrough);
    RoboCheck(InputTargetEqual(&result.target, &leaf));

    /* A miss is *reachable*, not a failure: the window is still there. */
    RoboCheckEqInt(InputHitTestPoint(&provider, &route, MakeGfxPoint(200, 10), RoboInputSpaceWindow,
                                     &result, &diag),
                   RoboOk);
    RoboCheck(result.hit && result.fellThrough);
    RoboCheckEqInt((int)result.target.kind, (int)RoboInputTargetWindow);

    /* A *failure* from the provider propagates: reading it as "no hit" is how a broken tree becomes a silent
     * misfeature, so the suite proves the difference between `RoboNotFound` and an error. */
    {
        InputHitTestProvider broken;

        broken.userdata = NULL;
        broken.hitTest = NULL;
        RoboCheckEqInt(InputHitTestPoint(&broken, &route, MakeGfxPoint(10, 10), RoboInputSpaceWindow,
                                         &result, &diag),
                       RoboOk);
        RoboCheck(result.fellThrough);
    }

    /* A plane a tree cannot be tested in is refused rather than guessed at. */
    RoboCheckEqInt(InputHitTestPoint(&provider, &route, MakeGfxPoint(10, 10), RoboInputSpaceUnknown,
                                     &result, &diag),
                   RoboInvalidArgument);
    RoboCheckEqInt((int)diag.domain, (int)RoboInputDomainDispatch);
    RoboCheckEqInt(InputHitTestPoint(&provider, &route, MakeGfxPoint(10, 10),
                                     RoboInputSpaceApplication, &result, &diag),
                   RoboInvalidArgument);
    RoboCheckEqInt(InputHitTestPoint(&provider, NULL, MakeGfxPoint(10, 10), RoboInputSpaceWindow,
                                     &result, &diag),
                   RoboInvalidArgument);
    RoboCheckEqInt(InputHitTestPoint(&provider, &route, MakeGfxPoint(10, 10), RoboInputSpaceWindow, NULL,
                                     &diag),
                   RoboInvalidArgument);

    /* No provider and a route naming no window: the honest answer is "not found". */
    {
        InputRoute empty = InputRouteFrameOnly(0, 0, 10, 10);

        empty.window = 0u;
        RoboCheckEqInt(InputHitTestPoint(NULL, &empty, MakeGfxPoint(1, 1), RoboInputSpaceWindow, &result,
                                         &diag),
                       RoboNotFound);
    }
}

/* ------------------------------------------------------------------ */
/* The path                                                           */
/* ------------------------------------------------------------------ */

static void TestDispatchPath(void)
{
    InputPathProvider provider;
    DispatchPathFixture fixture;
    InputEventPath path;
    InputDiag diag;

    InputDiagReset(&diag);
    fixture.root = DispatchWindowTarget(7u);
    fixture.middle = DispatchNodeTarget(7u, (LayoutNodeId)1u);
    fixture.leaf = DispatchNodeTarget(7u, (LayoutNodeId)2u);
    fixture.levels = 3;

    /* No provider: the target alone. That is a whole system whose only target kind is a window. */
    RoboCheckEqInt(InputEventPathBuild(NULL, &fixture.leaf, &path, &diag), RoboOk);
    RoboCheckEqInt((int)path.count, 1);
    RoboCheckEqInt((int)path.depth, 0);
    RoboCheck(InputTargetEqual(InputEventPathTarget(&path), &fixture.leaf));
    RoboCheck(InputEventPathAt(&path, 1u) == NULL);

    /* With one: root first, target last, and the depth says which is which. */
    provider.userdata = &fixture;
    provider.buildPath = DispatchBuildPath;
    RoboCheckEqInt(InputEventPathBuild(&provider, &fixture.leaf, &path, &diag), RoboOk);
    RoboCheckEqInt((int)path.count, 3);
    RoboCheckEqInt((int)path.depth, 2);
    RoboCheckEqInt((int)path.targets[0].kind, (int)RoboInputTargetWindow);
    RoboCheck(InputTargetEqual(&path.targets[1], &fixture.middle));
    RoboCheck(InputTargetEqual(InputEventPathTarget(&path), &fixture.leaf));

    /* A path that does not end at its target is refused: an event that never reaches the target it was
     * addressed to is a routing bug, and the door catches it for free. */
    provider.buildPath = DispatchBuildWrongEnd;
    RoboCheckEqInt(InputEventPathBuild(&provider, &fixture.leaf, &path, &diag), RoboInvalidArgument);
    RoboCheckEqInt((int)diag.domain, (int)RoboInputDomainDispatch);
    RoboCheckEqInt((int)path.count, 0);

    /* An invalid target, and NULL arguments. */
    provider.buildPath = DispatchBuildPath;
    {
        InputTarget empty;

        InputTargetInit(&empty);
        RoboCheckEqInt(InputEventPathBuild(&provider, &empty, &path, &diag), RoboInvalidArgument);
        RoboCheckEqInt(InputEventPathBuild(&provider, NULL, &path, &diag), RoboInvalidArgument);
        RoboCheckEqInt(InputEventPathBuild(&provider, &fixture.leaf, NULL, &diag), RoboInvalidArgument);
    }

    InputEventPathInit(&path);
    RoboCheckEqInt((int)path.count, 0);
    RoboCheck(InputEventPathTarget(&path) == NULL);
    RoboCheck(InputEventPathAt(NULL, 0u) == NULL);
}

/* ------------------------------------------------------------------ */
/* Propagation                                                        */
/* ------------------------------------------------------------------ */

static void TestDispatchPropagation(void)
{
    InputEventContext context;

    InputEventContextInit(&context);
    RoboCheckEqInt((int)context.phase, (int)RoboInputPhaseCapture);
    RoboCheck(!InputEventContextIsDefaultPrevented(&context));
    RoboCheck(!context.stopped);
    RoboCheck(!context.stoppedImmediate);

    RoboCheckEqInt(InputEventContextStopPropagation(&context), RoboOk);
    RoboCheck(context.stopped);
    RoboCheck(!context.stoppedImmediate);
    /* Idempotent, and a second call does not escalate to "immediate". */
    RoboCheckEqInt(InputEventContextStopPropagation(&context), RoboOk);
    RoboCheck(!context.stoppedImmediate);

    RoboCheckEqInt(InputEventContextStopImmediatePropagation(&context), RoboOk);
    RoboCheck(context.stoppedImmediate);

    RoboCheckEqInt(InputEventContextPreventDefault(&context), RoboOk);
    RoboCheck(InputEventContextIsDefaultPrevented(&context));
    RoboCheckEqInt((int)context.refusedDefault, 0);

    /* A passive listener promised it would not cancel. The call is a contract violation rather than a
     * request, so it is refused -- and counted, so a caller can find the listener that did it. */
    context.passive = 1;
    RoboCheckEqInt(InputEventContextPreventDefault(&context), RoboInvalidArgument);
    RoboCheckEqInt((int)context.refusedDefault, 1);
    /* ...and the earlier cancellation stands: the refusal does not undo it. */
    RoboCheck(InputEventContextIsDefaultPrevented(&context));
    context.passive = 0;

    RoboCheckEqInt(InputEventContextStopPropagation(NULL), RoboInvalidArgument);
    RoboCheckEqInt(InputEventContextStopImmediatePropagation(NULL), RoboInvalidArgument);
    RoboCheckEqInt(InputEventContextPreventDefault(NULL), RoboInvalidArgument);
    RoboCheck(!InputEventContextIsDefaultPrevented(NULL));

    /* The vocabulary. */
    RoboCheckEqInt(RoboInputPhaseCapture, 0);
    RoboCheckEqInt(RoboInputPhaseTarget, 1);
    RoboCheckEqInt(RoboInputPhaseBubble, 2);
    RoboCheckEqInt(RoboInputPhaseCount, 3);
    RoboCheckStrEq(InputPhaseName(RoboInputPhaseCapture), "capture");
    RoboCheckStrEq(InputPhaseName(RoboInputPhaseTarget), "target");
    RoboCheckStrEq(InputPhaseName(RoboInputPhaseBubble), "bubble");
    RoboCheckStrEq(InputPhaseName((InputPhase)7), "unknown");
    RoboCheck(!InputPhaseIsKnown((InputPhase)3));
}

/* ------------------------------------------------------------------ */
/* The walk                                                           */
/* ------------------------------------------------------------------ */

static void TestDispatchOrder(void)
{
    DispatchPathFixture fixture;
    DispatchListeners listeners;
    InputPathProvider pathProvider;
    InputListenerProvider provider;
    InputDispatcher dispatcher;
    InputEventPath path;
    InputEventContext context;
    InputEvent event;
    InputDiag diag;

    InputDiagReset(&diag);
    DispatchReset();
    fixture.root = DispatchWindowTarget(7u);
    fixture.middle = DispatchNodeTarget(7u, (LayoutNodeId)1u);
    fixture.leaf = DispatchNodeTarget(7u, (LayoutNodeId)2u);
    fixture.levels = 3;
    pathProvider.userdata = &fixture;
    pathProvider.buildPath = DispatchBuildPath;
    RoboCheckEqInt(InputEventPathBuild(&pathProvider, &fixture.leaf, &path, &diag), RoboOk);

    memset(&listeners, 0, sizeof(listeners));
    /* The window sees the capture; the leaf's parent sees the bubble; the leaf sees the target. */
    DispatchAddListener(&listeners, &fixture.root, RoboInputPhaseCapture, &DispatchA);
    DispatchAddListener(&listeners, &fixture.leaf, RoboInputPhaseTarget, &DispatchB);
    DispatchAddListener(&listeners, &fixture.middle, RoboInputPhaseBubble, &DispatchC);
    provider.userdata = &listeners;
    provider.listenersFor = DispatchListenersFor;
    InputDispatcherInit(&dispatcher, &provider);

    InputPointerMove(1, 2, 0u, RoboInputSpaceWindow, 7u);
    event = InputPointerMove(1, 2, 0u, RoboInputSpaceWindow, 7u);
    RoboCheckEqInt(InputDispatch(&dispatcher, &event, &path, &context, &diag), RoboOk);
    /* capture root, target, bubble middle -- in that order. */
    RoboCheckStrEq(DispatchTrace, "abc");
    RoboCheckEqInt((int)context.calls, 3);
    RoboCheckEqInt((int)context.phase, (int)RoboInputPhaseBubble);
    RoboCheckEqInt((int)dispatcher.dispatches, 1);
    RoboCheckEqInt((int)dispatcher.calls, 3);

    /* A stop in *capture* skips the target and the bubble entirely: `stopPropagation` on an ancestor is how
     * a modal swallows a click before the control underneath sees it. */
    DispatchReset();
    DispatchA.stop = 1;
    RoboCheckEqInt(InputDispatch(&dispatcher, &event, &path, &context, &diag), RoboOk);
    RoboCheckStrEq(DispatchTrace, "a");
    RoboCheck(context.stopped);

    /* A stop *immediate* in the target skips the rest of the target's own listeners too. */
    DispatchReset();
    {
        DispatchListeners two;

        memset(&two, 0, sizeof(two));
        DispatchAddListener(&two, &fixture.leaf, RoboInputPhaseTarget, &DispatchA);
        DispatchAddListener(&two, &fixture.leaf, RoboInputPhaseTarget, &DispatchB);
        DispatchAddListener(&two, &fixture.leaf, RoboInputPhaseBubble, &DispatchC);
        provider.userdata = &two;
        DispatchA.stopImmediate = 1;
        DispatchB.stopImmediate = 1;
        RoboCheckEqInt(InputDispatch(&dispatcher, &event, &path, &context, &diag), RoboOk);
        /* `a` stopped it: `b` was never called, and neither was the bubble listener. */
        RoboCheckStrEq(DispatchTrace, "a");
    }

    /* A listener on the middle *and* in both phases is called twice, once per registration. */
    DispatchReset();
    {
        DispatchListeners both;

        memset(&both, 0, sizeof(both));
        DispatchAddListener(&both, &fixture.middle, RoboInputPhaseCapture, &DispatchA);
        DispatchAddListener(&both, &fixture.middle, RoboInputPhaseBubble, &DispatchB);
        provider.userdata = &both;
        RoboCheckEqInt(InputDispatch(&dispatcher, &event, &path, &context, &diag), RoboOk);
        RoboCheckStrEq(DispatchTrace, "ab");
    }

    /* preventDefault is recorded on the context and reported, and `passive` is cleared between listeners so
     * one passive listener does not make the next one passive too. */
    DispatchReset();
    {
        DispatchListeners passives;

        memset(&passives, 0, sizeof(passives));
        DispatchA.passive = 1;
        DispatchA.preventDefault = 1;
        DispatchB.preventDefault = 1;
        DispatchAddListener(&passives, &fixture.leaf, RoboInputPhaseTarget, &DispatchA);
        DispatchAddListener(&passives, &fixture.leaf, RoboInputPhaseTarget, &DispatchB);
        provider.userdata = &passives;
        RoboCheckEqInt(InputDispatch(&dispatcher, &event, &path, &context, &diag), RoboOk);
        RoboCheckStrEq(DispatchTrace, "ab");
        /* The passive one was refused; the second, non-passive, cancelled. */
        RoboCheckEqInt((int)context.refusedDefault, 1);
        RoboCheck(InputEventContextIsDefaultPrevented(&context));
    }

    /* A callback failure stops the walk: a listener that could not do its job has left state a later one
     * would act on. */
    DispatchReset();
    DispatchA.result = RoboOutOfMemory;
    DispatchA.stop = 0;
    DispatchA.passive = 0;
    DispatchA.preventDefault = 0;
    RoboCheckEqInt(InputDispatch(&dispatcher, &event, &path, &context, &diag), RoboOutOfMemory);
    RoboCheckEqInt((int)dispatcher.failures, 1);
    RoboCheckEqInt((int)dispatcher.lastFailure, (int)RoboOutOfMemory);
    RoboCheckEqInt((int)context.calls, 1);

    /* Nothing listening: the walk completes and reports an untouched context. */
    DispatchReset();
    InputDispatcherInit(&dispatcher, NULL);
    RoboCheckEqInt(InputDispatch(&dispatcher, &event, &path, &context, &diag), RoboOk);
    RoboCheckEqInt((int)context.calls, 0);
    RoboCheck(!context.stopped);
    RoboCheckStrEq(DispatchTrace, "");

    /* NULL arguments. */
    RoboCheckEqInt(InputDispatch(NULL, &event, &path, &context, &diag), RoboInvalidArgument);
    RoboCheckEqInt(InputDispatch(&dispatcher, NULL, &path, &context, &diag), RoboInvalidArgument);
    RoboCheckEqInt(InputDispatch(&dispatcher, &event, NULL, &context, &diag), RoboInvalidArgument);
    RoboCheckEqInt(InputDispatch(&dispatcher, &event, &path, NULL, &diag), RoboInvalidArgument);
    InputEventPathInit(&path);
    RoboCheckEqInt(InputDispatch(&dispatcher, &event, &path, &context, &diag), RoboInvalidArgument);
}

static void TestDispatchCounters(void)
{
    DispatchPathFixture fixture;
    DispatchListeners listeners;
    InputPathProvider pathProvider;
    InputListenerProvider provider;
    InputDispatcher dispatcher;
    InputEventPath path;
    InputEventContext context;
    InputEvent event;
    InputDiag diag;

    InputDiagReset(&diag);
    DispatchReset();
    fixture.root = DispatchWindowTarget(7u);
    fixture.middle = DispatchNodeTarget(7u, (LayoutNodeId)1u);
    fixture.leaf = DispatchNodeTarget(7u, (LayoutNodeId)2u);
    fixture.levels = 1;
    pathProvider.userdata = &fixture;
    pathProvider.buildPath = DispatchBuildPath;
    RoboCheckEqInt(InputEventPathBuild(&pathProvider, &fixture.root, &path, &diag), RoboOk);
    RoboCheckEqInt((int)path.count, 1);

    memset(&listeners, 0, sizeof(listeners));
    DispatchAddListener(&listeners, &fixture.root, RoboInputPhaseTarget, &DispatchA);
    provider.userdata = &listeners;
    provider.listenersFor = DispatchListenersFor;
    InputDispatcherInit(&dispatcher, &provider);

    /* A provider that offers more listeners than the batch holds: reported, not hidden. */
    listeners.offered = RoboInputMaxListeners + 3u;
    event = InputPointerMove(1, 2, 0u, RoboInputSpaceWindow, 7u);
    RoboCheckEqInt(InputDispatch(&dispatcher, &event, &path, &context, &diag), RoboOk);
    RoboCheckEqInt((int)dispatcher.dropped, 3);
    listeners.offered = 0u;

    /* A listener marked removed, and one with no callback, are skipped: that is what "removed but not yet
     * collected" means. */
    DispatchReset();
    listeners.items[0].removed = 1;
    RoboCheckEqInt(InputDispatch(&dispatcher, &event, &path, &context, &diag), RoboOk);
    RoboCheckStrEq(DispatchTrace, "");
    listeners.items[0].removed = 0;
    listeners.items[0].callback = NULL;
    RoboCheckEqInt(InputDispatch(&dispatcher, &event, &path, &context, &diag), RoboOk);
    RoboCheckStrEq(DispatchTrace, "");
    listeners.items[0].callback = DispatchRecord;

    /* A listener registered for a *different* phase is not called. */
    DispatchReset();
    listeners.items[0].phase = RoboInputPhaseCapture;
    RoboCheckEqInt(InputDispatch(&dispatcher, &event, &path, &context, &diag), RoboOk);
    RoboCheckStrEq(DispatchTrace, "");
}

/* ------------------------------------------------------------------ */
/* Default actions                                                    */
/* ------------------------------------------------------------------ */

static void TestDispatchDefault(void)
{
    InputEvent event;
    InputDefaultAction action;
    InputPointerData pointer;
    InputKeyboardData keyboard;

    /* A wheel: scroll, with line deltas multiplied by the caller's line height. */
    InputWheelData wheelData;

    InputWheelDataInit(&wheelData);
    wheelData.deltaX = 0;
    wheelData.deltaY = 3;
    wheelData.unit = RoboMouseScrollUnitLine;
    pointer = GetRoboInputPointerData(10, 20, RoboMouseButtonNone, 0u);
    RoboCheckEqInt(InputPointerDataSetWheel(&pointer, 0, 3, RoboMouseScrollUnitLine), RoboOk);
    InputEventInit(&event);
    RoboCheckEqInt(InputEventSetPointer(&event, RoboInputEventWheel, RoboInputSourceMouse, &pointer,
                                       RoboInputSpaceWindow, NULL),
                   RoboOk);
    action = InputDefaultActionFor(&event, NULL, 40);
    RoboCheckEqInt((int)action.kind, (int)RoboInputDefaultActionScroll);
    RoboCheckEqInt((int)action.deltaY, 120);
    RoboCheckEqInt((int)action.deltaX, 0);
    /* A line height of zero is a document with no lines: no distance, not a guess. */
    action = InputDefaultActionFor(&event, NULL, 0);
    RoboCheckEqInt((int)action.deltaY, 0);

    /* A press: focus -- except with the secondary button, which must not take the caret away. */
    pointer = GetRoboInputPointerData(10, 20, RoboMouseButtonLeft, InputButtonMask(RoboMouseButtonLeft));
    InputEventInit(&event);
    RoboCheckEqInt(InputEventSetPointer(&event, RoboInputEventPointerDown, RoboInputSourceMouse, &pointer,
                                       RoboInputSpaceWindow, NULL),
                   RoboOk);
    RoboCheckEqInt(event.window, 0);
    InputEventSetWindow(&event, 7u);
    action = InputDefaultActionFor(&event, NULL, 40);
    RoboCheckEqInt((int)action.kind, (int)RoboInputDefaultActionFocus);
    RoboCheckEqInt((int)action.button, (int)RoboMouseButtonLeft);
    RoboCheckEqInt((int)action.target.kind, (int)RoboInputTargetWindow);

    pointer = GetRoboInputPointerData(10, 20, RoboMouseButtonRight, InputButtonMask(RoboMouseButtonRight));
    InputEventInit(&event);
    RoboCheckEqInt(InputEventSetPointer(&event, RoboInputEventPointerDown, RoboInputSourceMouse, &pointer,
                                       RoboInputSpaceWindow, NULL),
                   RoboOk);
    action = InputDefaultActionFor(&event, NULL, 40);
    RoboCheckEqInt((int)action.kind, (int)RoboInputDefaultActionNone);

    /* A move answers nothing: hover is not a default action. */
    pointer = GetRoboInputPointerData(10, 20, RoboMouseButtonNone, 0u);
    InputEventInit(&event);
    RoboCheckEqInt(InputEventSetPointer(&event, RoboInputEventPointerMove, RoboInputSourceMouse, &pointer,
                                       RoboInputSpaceWindow, NULL),
                   RoboOk);
    RoboCheckEqInt((int)InputDefaultActionFor(&event, NULL, 40).kind,
                   (int)RoboInputDefaultActionNone);

    /* Keys. A letter is a command; `Enter` activates; `Space` activates until it carries text; `Tab`
     * navigates, and the modifier picks the direction. */
    keyboard = InputKeyData(RoboPhysicalKeyA, RoboLogicalKeyA, RoboKeyActionDown, 0, "a");
    InputEventInit(&event);
    RoboCheckEqInt(InputEventSetKeyboard(&event, RoboInputEventKeyDown, RoboInputSourceKeyboard, &keyboard, NULL),
                   RoboOk);
    action = InputDefaultActionFor(&event, NULL, 40);
    RoboCheckEqInt((int)action.kind, (int)RoboInputDefaultActionCommand);
    RoboCheck(action.hasText);

    keyboard = InputKeyData(RoboPhysicalKeyEnter, RoboLogicalKeyEnter, RoboKeyActionDown, 0, "");
    InputEventInit(&event);
    RoboCheckEqInt(InputEventSetKeyboard(&event, RoboInputEventKeyDown, RoboInputSourceKeyboard, &keyboard, NULL),
                   RoboOk);
    RoboCheckEqInt((int)InputDefaultActionFor(&event, NULL, 40).kind,
                   (int)RoboInputDefaultActionActivate);

    keyboard = InputKeyData(RoboPhysicalKeySpace, RoboLogicalKeySpace, RoboKeyActionDown, 0, " ");
    InputEventInit(&event);
    RoboCheckEqInt(InputEventSetKeyboard(&event, RoboInputEventKeyDown, RoboInputSourceKeyboard, &keyboard, NULL),
                   RoboOk);
    action = InputDefaultActionFor(&event, NULL, 40);
    RoboCheckEqInt((int)action.kind, (int)RoboInputDefaultActionCommand);

    keyboard = InputKeyData(RoboPhysicalKeySpace, RoboLogicalKeySpace, RoboKeyActionDown, 0, "");
    InputEventInit(&event);
    RoboCheckEqInt(InputEventSetKeyboard(&event, RoboInputEventKeyDown, RoboInputSourceKeyboard, &keyboard, NULL),
                   RoboOk);
    RoboCheckEqInt((int)InputDefaultActionFor(&event, NULL, 40).kind,
                   (int)RoboInputDefaultActionActivate);

    keyboard = InputKeyData(RoboPhysicalKeyTab, RoboLogicalKeyTab, RoboKeyActionDown, 0, "");
    InputEventInit(&event);
    RoboCheckEqInt(InputEventSetKeyboard(&event, RoboInputEventKeyDown, RoboInputSourceKeyboard, &keyboard, NULL),
                   RoboOk);
    action = InputDefaultActionFor(&event, NULL, 40);
    RoboCheckEqInt((int)action.kind, (int)RoboInputDefaultActionNavigate);
    RoboCheckEqInt((int)action.direction, (int)RoboInputFocusDirectionNext);    RoboCheckEqInt(InputEventSetKeyboard(&event, RoboInputEventKeyDown, RoboInputSourceKeyboard, &keyboard,
                                         NULL),
                   RoboOk);
    RoboCheckEqInt(InputEventSetModifiers(&event, RoboKeyModShift, NULL), RoboOk);
    RoboCheckEqInt((int)InputDefaultActionFor(&event, NULL, 40).direction,
                   (int)RoboInputFocusDirectionPrevious);

    /* A key-up and a bare modifier answer nothing. */
    keyboard = InputKeyData(RoboPhysicalKeyA, RoboLogicalKeyA, RoboKeyActionUp, 0, "");
    InputEventInit(&event);
    RoboCheckEqInt(InputEventSetKeyboard(&event, RoboInputEventKeyUp, RoboInputSourceKeyboard, &keyboard, NULL),
                   RoboOk);
    RoboCheckEqInt((int)InputDefaultActionFor(&event, NULL, 40).kind,
                   (int)RoboInputDefaultActionNone);
    RoboCheckEqInt((int)InputDefaultActionFor(NULL, NULL, 40).kind,
                   (int)RoboInputDefaultActionNone);

    /* A focus action with an explicit target takes the request's deltas and target. */
    {
        InputDefaultAction request;

        memset(&request, 0, sizeof(request));
        request.deltaX = 5;
        request.deltaY = -7;
        pointer = GetRoboInputPointerData(0, 0, RoboMouseButtonNone, 0u);
        InputEventInit(&event);
        RoboCheckEqInt(InputEventSetPointer(&event, RoboInputEventWheel, RoboInputSourceMouse, &pointer,
                                           RoboInputSpaceWindow, NULL),
                       RoboOk);
        action = InputDefaultActionFor(&event, &request, 40);
        RoboCheckEqInt((int)action.deltaX, 5);
        RoboCheckEqInt((int)action.deltaY, -7);
    }

    /* The vocabulary. */
    RoboCheckEqInt(RoboInputDefaultActionNone, 0);
    RoboCheckEqInt(RoboInputDefaultActionFocus, 1);
    RoboCheckEqInt(RoboInputDefaultActionScroll, 2);
    RoboCheckEqInt(RoboInputDefaultActionActivate, 3);
    RoboCheckEqInt(RoboInputDefaultActionNavigate, 4);
    RoboCheckEqInt(RoboInputDefaultActionCommand, 5);
    RoboCheckEqInt(RoboInputDefaultActionKindCount, 6);
    RoboCheckStrEq(InputDefaultActionKindName(RoboInputDefaultActionScroll), "scroll");
    RoboCheckStrEq(InputDefaultActionKindName((InputDefaultActionKind)9), "unknown");
    RoboCheck(!InputDefaultActionKindIsKnown((InputDefaultActionKind)6));
}

/* ------------------------------------------------------------------ */
/* Dumps                                                              */
/* ------------------------------------------------------------------ */

static void TestDispatchDump(void)
{
    DispatchPathFixture fixture;
    InputPathProvider pathProvider;
    InputEventPath path;
    InputEventContext context;
    InputDefaultAction action;
    InputEvent event;
    InputDiag diag;
    char line[512];
    size_t length = 0u;

    InputDiagReset(&diag);
    fixture.root = DispatchWindowTarget(7u);
    fixture.middle = DispatchNodeTarget(7u, (LayoutNodeId)1u);
    fixture.leaf = DispatchNodeTarget(7u, (LayoutNodeId)2u);
    fixture.levels = 2;
    pathProvider.userdata = &fixture;
    pathProvider.buildPath = DispatchBuildPath;
    RoboCheckEqInt(InputEventPathBuild(&pathProvider, &fixture.middle, &path, &diag), RoboOk);
    RoboCheckEqInt(InputEventPathWrite(&path, line, sizeof(line), &length, &diag), RoboOk);
    RoboCheckStrEq(line, "path count=2 depth=1 truncated=0\n"
                         "  0 kind=window window=7 node=0\n"
                         "  1 kind=document window=7 node=1 target=1\n");

    InputEventContextInit(&context);
    RoboCheckEqInt(InputEventContextStopPropagation(&context), RoboOk);
    RoboCheckEqInt(InputEventContextWrite(&context, line, sizeof(line), &length, &diag), RoboOk);
    RoboCheckStrEq(line, "propagation phase=capture current=none calls=0 stopped=1 immediate=0 "
                         "prevented=0 refused=0\n");

    event = InputPointerMove(1, 1, 0u, RoboInputSpaceWindow, 7u);
    action = InputDefaultActionFor(&event, NULL, 40);
    RoboCheckEqInt(InputDefaultActionWrite(&action, line, sizeof(line), &length, &diag), RoboOk);
    RoboCheckStrEq(line, "default-action none on=PointerMove target=window dx=0 dy=0 button=0 "
                         "direction=none text=0\n");

    /* NULL records, and a small buffer. */
    RoboCheckEqInt(InputEventPathWrite(NULL, line, sizeof(line), &length, &diag), RoboOk);
    RoboCheckStrEq(line, "path <none>\n");
    RoboCheckEqInt(InputEventContextWrite(NULL, line, sizeof(line), &length, &diag), RoboOk);
    RoboCheckStrEq(line, "propagation <none>\n");
    RoboCheckEqInt(InputDefaultActionWrite(NULL, line, sizeof(line), &length, &diag), RoboOk);
    RoboCheckStrEq(line, "default-action <none>\n");
    {
        char small[8];
        size_t needed = 0u;

        RoboCheckEqInt(InputEventPathWrite(&path, small, sizeof(small), &needed, &diag), RoboNotSupported);
        RoboCheckStrEq(small, "?");
        RoboCheck(needed > sizeof(small));
    }
    RoboCheckEqInt(InputEventPathWrite(&path, line, sizeof(line), NULL, &diag), RoboInvalidArgument);
}

/* ------------------------------------------------------------------ */
/* Frame                                                               */
/* ------------------------------------------------------------------ */

void TestRegisterInputDispatch(void)
{
    TestRegister("input.dispatch.hit", TestDispatchHit);
    TestRegister("input.dispatch.path", TestDispatchPath);
    TestRegister("input.dispatch.propagation", TestDispatchPropagation);
    TestRegister("input.dispatch.order", TestDispatchOrder);
    TestRegister("input.dispatch.counters", TestDispatchCounters);
    TestRegister("input.dispatch.default", TestDispatchDefault);
    TestRegister("input.dispatch.dump", TestDispatchDump);
}
