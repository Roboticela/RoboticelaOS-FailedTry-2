#include "RoboWindow/RoboWindowKeyboard.h"
#include "RoboWindow/RoboWindow.h"
#include "RoboWindow/RoboWindowEvent.h"
#include "RoboWindowInternal.h"

#include "RoboBase/RoboLog.h"

#include <string.h>

static uint8_t gPressed[RoboPhysicalKeyCount];
static uint8_t gLogicalPressed[RoboLogicalKeyCount];
static KeyModifierFlags gModifiers;

void KeyboardEventInit(KeyboardEvent *ev)
{
    if (!ev) {
        return;
    }
    memset(ev, 0, sizeof(*ev));
    ev->action = RoboKeyActionNone;
    ev->physicalKey = RoboPhysicalKeyUnknown;
    ev->logicalKey = RoboLogicalKeyUnknown;
    ev->windowId = RoboWindowIdNone;
}

void KeyboardResetPressedState(void)
{
    memset(gPressed, 0, sizeof(gPressed));
    memset(gLogicalPressed, 0, sizeof(gLogicalPressed));
    /* Keep lock bits; clear momentary modifiers. */
    gModifiers &= (RoboKeyModCapsLock | RoboKeyModNumLock |
                    RoboKeyModScrollLock);
    LogDebug("robo_keyboard: pressed state reset");
}

void KeyboardReset(void)
{
    memset(gPressed, 0, sizeof(gPressed));
    memset(gLogicalPressed, 0, sizeof(gLogicalPressed));
    gModifiers = RoboKeyModNone;
    LogDebug("robo_keyboard: full reset");
}

int KeyboardIsPhysicalPressed(PhysicalKey physical)
{
    if (!PhysicalKeyIsKnown(physical)) {
        return 0;
    }
    return gPressed[physical] != 0;
}

int KeyboardIsLogicalPressed(LogicalKey logical)
{
    if (!LogicalKeyIsKnown(logical)) {
        return 0;
    }
    return gLogicalPressed[logical] != 0;
}

KeyModifierFlags KeyboardModifiers(void)
{
    return gModifiers;
}

int KeyboardModifierActive(KeyModifierFlags flag)
{
    return (gModifiers & flag) != 0;
}

static void ApplyLockFromModifiers(KeyModifierFlags mods)
{
    gModifiers &= ~(RoboKeyModCapsLock | RoboKeyModNumLock |
                     RoboKeyModScrollLock);
    gModifiers |=
        (mods & (RoboKeyModCapsLock | RoboKeyModNumLock |
                 RoboKeyModScrollLock));
}

static void RefreshMomentaryModifiers(void)
{
    KeyModifierFlags locks =
        gModifiers & (RoboKeyModCapsLock | RoboKeyModNumLock |
                       RoboKeyModScrollLock);
    KeyModifierFlags m = locks;

    if (gPressed[RoboPhysicalKeyShiftLeft] ||
        gPressed[RoboPhysicalKeyShiftRight]) {
        m |= RoboKeyModShift;
    }
    if (gPressed[RoboPhysicalKeyShiftLeft]) {
        m |= RoboKeyModShiftLeft;
    }
    if (gPressed[RoboPhysicalKeyShiftRight]) {
        m |= RoboKeyModShiftRight;
    }
    if (gPressed[RoboPhysicalKeyControlLeft] ||
        gPressed[RoboPhysicalKeyControlRight]) {
        m |= RoboKeyModCtrl;
    }
    if (gPressed[RoboPhysicalKeyControlLeft]) {
        m |= RoboKeyModCtrlLeft;
    }
    if (gPressed[RoboPhysicalKeyControlRight]) {
        m |= RoboKeyModCtrlRight;
    }
    if (gPressed[RoboPhysicalKeyAltLeft] ||
        gPressed[RoboPhysicalKeyAltRight]) {
        m |= RoboKeyModAlt;
    }
    if (gPressed[RoboPhysicalKeyAltLeft]) {
        m |= RoboKeyModAltLeft;
    }
    if (gPressed[RoboPhysicalKeyAltRight]) {
        m |= RoboKeyModAltRight;
    }
    if (gPressed[RoboPhysicalKeyMetaLeft] ||
        gPressed[RoboPhysicalKeyMetaRight]) {
        m |= RoboKeyModMeta;
    }
    if (gPressed[RoboPhysicalKeyMetaLeft]) {
        m |= RoboKeyModMetaLeft;
    }
    if (gPressed[RoboPhysicalKeyMetaRight]) {
        m |= RoboKeyModMetaRight;
    }
    gModifiers = m;
}

static void UpdateStateFromEvent(const KeyboardEvent *ev)
{
    ApplyLockFromModifiers(ev->modifiers);

    if (PhysicalKeyIsKnown(ev->physicalKey)) {
        if (ev->action == RoboKeyActionDown) {
            gPressed[ev->physicalKey] = 1;
        } else if (ev->action == RoboKeyActionUp) {
            gPressed[ev->physicalKey] = 0;
        }
    }
    if (LogicalKeyIsKnown(ev->logicalKey)) {
        if (ev->action == RoboKeyActionDown) {
            gLogicalPressed[ev->logicalKey] = 1;
        } else if (ev->action == RoboKeyActionUp) {
            gLogicalPressed[ev->logicalKey] = 0;
        }
    }

    /* Prefer explicit modifier snapshot from backend when provided. */
    if (ev->modifiers != RoboKeyModNone ||
        (ev->modifiers & (RoboKeyModCapsLock | RoboKeyModNumLock |
                          RoboKeyModScrollLock)) != 0 ||
        ev->action == RoboKeyActionUp ||
        ev->action == RoboKeyActionDown) {
        /* Merge: locks from event; momentary from event if any set, else recompute. */
        if ((ev->modifiers &
             (RoboKeyModShift | RoboKeyModCtrl | RoboKeyModAlt |
              RoboKeyModMeta | RoboKeyModShiftLeft |
              RoboKeyModShiftRight | RoboKeyModCtrlLeft |
              RoboKeyModCtrlRight | RoboKeyModAltLeft |
              RoboKeyModAltRight | RoboKeyModMetaLeft |
              RoboKeyModMetaRight)) != 0) {
            gModifiers = ev->modifiers;
        } else {
            RefreshMomentaryModifiers();
            ApplyLockFromModifiers(ev->modifiers);
        }
    }
}

Result WindowObserveKey(WindowId windowId,
                                      const KeyboardEvent *ev)
{
    Result r;
    WindowId target;
    WindowEvent we;
    KeyboardEvent local;

    if (!ev) {
        return RoboInvalidArgument;
    }
    if (ev->action != RoboKeyActionDown && ev->action != RoboKeyActionUp) {
        return RoboInvalidArgument;
    }
    if (!WindowIsInitialized()) {
        return RoboInitError;
    }
    r = WindowThreadRequireOwner();
    if (r != RoboOk) {
        return r;
    }

    local = *ev;
    target = windowId;
    if (target == RoboWindowIdNone) {
        target = WindowFindFocusedId();
    }

    UpdateStateFromEvent(&local);

    if (!PhysicalKeyIsKnown(local.physicalKey) &&
        !LogicalKeyIsKnown(local.logicalKey)) {
        LogDebug("robo_keyboard: unmapped key dropped action=%s",
                       KeyActionName(local.action));
        /* Still OK — do not crash; state already absorbed locks if any. */
        return RoboOk;
    }

    if (target == RoboWindowIdNone || !WindowIdIsLive(target)) {
        LogDebug("robo_keyboard: no live target for action=%s phys=%s",
                       KeyActionName(local.action),
                       PhysicalKeyName(local.physicalKey));
        return RoboNotFound;
    }

    WindowEventInit(&we);
    we.type = (local.action == RoboKeyActionDown)
                  ? RoboWindowEventKeyDown
                  : RoboWindowEventKeyUp;
    we.windowId = target;
    we.key = local;
    we.key.windowId = target;

    r = WindowEventPush(&we);
    if (r != RoboOk) {
        LogWarn("robo_keyboard: queue push failed (%d)", (int)r);
        return r;
    }
    LogDebug(
        "robo_keyboard: %s id=%llu phys=%s log=%s mods=0x%x repeat=%d",
        KeyActionName(local.action), (unsigned long long)target,
        PhysicalKeyName(local.physicalKey),
        LogicalKeyName(local.logicalKey), (unsigned)gModifiers,
        local.isRepeat);
    return RoboOk;
}
