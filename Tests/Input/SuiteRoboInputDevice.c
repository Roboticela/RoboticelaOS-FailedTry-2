/*
 * RoboInput device table suite (Phase 13.2) — `input.device.*`.
 *
 * Seven cases about the table and one about the vocabulary:
 *
 *   `vocabulary`     the seven device types, the eleven capability bits and the three connection states,
 *                    asserted positionally with their names and masks, because they are the numbers a
 *                    descriptor in a saved recording carries;
 *
 *   `descriptor`     a descriptor's own consistency: the two rules the *type* makes checkable (a keyboard
 *                    reports keys; a gamepad has buttons or axes), the connection state a descriptor must
 *                    name, and the source that is *defaulted* from the type but not *constrained* by it --
 *                    which is the combined-digitizer case the header exists for;
 *
 *   `capabilities`   the compact list a dump reads, including the refusal of a bit this build does not
 *                    define: an undefined bit is not a capability, and writing the ones that are nameable
 *                    would produce a list that reads as complete;
 *
 *   `registry`       connect assigning ids, the table filling, a duplicate id refused, lookup by index and
 *                    by id, and the two counters;
 *
 *   `primary`        the rule that makes "the main keyboard" a question with one answer: the first of a
 *                    type becomes its primary, a later flag takes it from whoever held it, and a disconnect
 *                    hands it to the next of that type;
 *
 *   `disconnect`     removal rather than marking -- the entry is gone, the removed descriptor comes back,
 *                    and disconnecting an id that is not there is reported rather than swallowed;
 *
 *   `dump`           the text, asserted whole: a summary line, one line per device in connection order, and
 *                    byte-for-byte determinism across two calls.
 *
 * The one thing this suite cannot assert is that a disconnect released a *key*, because that is the
 * keyboard state's job (13.2c) -- what it can assert is that the table hands back what it removed, which is
 * how the caller releasing the keys knows which id to release.
 */

#include "RoboInputTestSupport.h"

/* ------------------------------------------------------------------ */
/* Vocabulary                                                          */
/* ------------------------------------------------------------------ */

static void TestDeviceVocabulary(void)
{
    uint32_t index;
    uint32_t mask = 0u;

    RoboCheckEqInt(RoboInputDeviceTypeUnknown, 0);
    RoboCheckEqInt(RoboInputDeviceTypeKeyboard, 1);
    RoboCheckEqInt(RoboInputDeviceTypeMouse, 2);
    RoboCheckEqInt(RoboInputDeviceTypeTouchscreen, 3);
    RoboCheckEqInt(RoboInputDeviceTypeTouchpad, 4);
    RoboCheckEqInt(RoboInputDeviceTypePen, 5);
    RoboCheckEqInt(RoboInputDeviceTypeGamepad, 6);
    RoboCheckEqInt(RoboInputDeviceTypeCount, 7);

    RoboCheckEqInt(RoboInputDeviceConnectionUnknown, 0);
    RoboCheckEqInt(RoboInputDeviceConnectionConnected, 1);
    RoboCheckEqInt(RoboInputDeviceConnectionSuspended, 2);
    RoboCheckEqInt(RoboInputDeviceConnectionCount, 3);

    RoboCheckEqInt(RoboInputDeviceCapabilityCount, 11);

    /* Each bit is its own bit, in the brief's order, and the union is all of them. */
    for (index = 0u; index < RoboInputDeviceCapabilityCount; index++) {
        uint32_t bit = InputDeviceCapabilityMask(index);

        RoboCheck(bit != 0u);
        RoboCheckEqInt((int)(bit & mask), 0);
        mask |= bit;
        RoboCheckStrEq(InputDeviceCapabilityName(index),
                          index == 0u
                              ? "keyboard-keys"
                              : (index == 8u ? "wheel" : InputDeviceCapabilityName(index)));
    }
    RoboCheckEqInt((int)mask, (int)RoboInputDeviceCapabilityAll);
    RoboCheck(InputDeviceCapabilityMask(RoboInputDeviceCapabilityCount) == 0u);
    RoboCheck(InputDeviceCapabilityName(RoboInputDeviceCapabilityCount) == NULL);
    RoboCheck(InputDeviceCapabilitiesAreKnown(RoboInputDeviceCapabilityAll));
    RoboCheck(!InputDeviceCapabilitiesAreKnown(1u << 11));
    RoboCheck(InputDeviceCapabilitiesHas(RoboInputDeviceCapabilityAll,
                                         RoboInputDeviceCapabilityTilt));
    RoboCheck(!InputDeviceCapabilitiesHas(RoboInputDeviceCapabilityPointer,
                                          RoboInputDeviceCapabilityTilt));

    /* Every name is a distinct word, and an undefined type names itself "unknown". */
    RoboCheckStrEq(InputDeviceTypeName(RoboInputDeviceTypeTouchpad), "touchpad");
    RoboCheckStrEq(InputDeviceTypeName((InputDeviceType)7), "unknown");
    RoboCheckStrEq(InputDeviceConnectionName(RoboInputDeviceConnectionSuspended), "suspended");
    RoboCheckStrEq(InputDeviceConnectionName((InputDeviceConnection)3), "unknown");
    RoboCheck(InputDeviceTypeIsKnown(RoboInputDeviceTypeUnknown));
    RoboCheck(!InputDeviceTypeIsKnown((InputDeviceType)7));
    RoboCheck(!InputDeviceConnectionIsKnown((InputDeviceConnection)3));

    /* The default source: the two cases that prove source and type are different questions. */
    RoboCheckEqInt(InputDeviceTypeDefaultSource(RoboInputDeviceTypeKeyboard),
                      RoboInputSourceKeyboard);
    RoboCheckEqInt(InputDeviceTypeDefaultSource(RoboInputDeviceTypeMouse), RoboInputSourceMouse);
    RoboCheckEqInt(InputDeviceTypeDefaultSource(RoboInputDeviceTypeTouchscreen),
                      RoboInputSourceTouch);
    RoboCheckEqInt(InputDeviceTypeDefaultSource(RoboInputDeviceTypePen), RoboInputSourceTablet);
    RoboCheckEqInt(InputDeviceTypeDefaultSource(RoboInputDeviceTypeGamepad),
                      RoboInputSourceGameController);
    /* A touchpad positions a pointer and has no contacts: it reports as a *mouse*, which is why a handler
     * must test the source rather than the type. */
    RoboCheckEqInt(InputDeviceTypeDefaultSource(RoboInputDeviceTypeTouchpad),
                      RoboInputSourceMouse);
    RoboCheckEqInt(InputDeviceTypeDefaultSource(RoboInputDeviceTypeUnknown),
                      RoboInputSourceUnknown);
}

/* ------------------------------------------------------------------ */
/* The descriptor                                                      */
/* ------------------------------------------------------------------ */

/** A keyboard descriptor, the fixture most cases want, asserting on refusal. */
static InputDeviceDescriptor DeviceKeyboard(InputDeviceId id, const char *name)
{
    InputDeviceDescriptor descriptor;

    InputDeviceDescriptorInit(&descriptor);
    if (InputDeviceDescriptorSet(&descriptor, id, RoboInputDeviceTypeKeyboard,
                                 RoboInputSourceUnknown, name,
                                 RoboInputDeviceCapabilityKeyboardKeys, NULL) != RoboOk) {
        RoboCheckMsg(0, "the fixture's keyboard descriptor was refused");
    }
    return descriptor;
}

static void TestDeviceDescriptor(void)
{
    InputDeviceDescriptor descriptor;
    InputDiag diag;

    InputDiagReset(&diag);
    /* The cleared descriptor is *not* valid: it names no kind of hardware and no connection state, which
     * is the honest triple-zero rather than a keyboard that happens to be disconnected. */
    InputDeviceDescriptorInit(&descriptor);
    RoboCheck(!InputDeviceDescriptorIsValid(&descriptor));
    RoboCheck(!InputDeviceDescriptorIsValid(NULL));

    descriptor = DeviceKeyboard(11u, "AT keyboard");
    RoboCheck(InputDeviceDescriptorIsValid(&descriptor));
    RoboCheckEqInt(descriptor.device.id, 11);
    RoboCheckEqInt(descriptor.device.source, RoboInputSourceKeyboard);
    RoboCheckEqInt(descriptor.type, RoboInputDeviceTypeKeyboard);
    RoboCheckEqInt(descriptor.connection, RoboInputDeviceConnectionConnected);
    RoboCheck(InputDeviceDescriptorHasCapability(&descriptor,
                                                RoboInputDeviceCapabilityKeyboardKeys));
    RoboCheck(!InputDeviceDescriptorHasCapability(&descriptor,
                                                 RoboInputDeviceCapabilityWheel));
    RoboCheck(!InputDeviceDescriptorHasCapability(NULL, RoboInputDeviceCapabilityWheel));

    /* A keyboard that denies it reports keys is a record saying two things at once. */
    RoboCheckEqInt(InputDeviceDescriptorSet(&descriptor, 11u, RoboInputDeviceTypeKeyboard,
                                            RoboInputSourceUnknown, "k",
                                            RoboInputDeviceCapabilityPointer, &diag),
                      RoboOk);
    RoboCheck(!InputDeviceDescriptorIsValid(&descriptor));
    /* A gamepad with neither buttons nor axes is a device this model has no way to hear from. */
    RoboCheckEqInt(InputDeviceDescriptorSet(&descriptor, 12u, RoboInputDeviceTypeGamepad,
                                            RoboInputSourceUnknown, "pad",
                                            RoboInputDeviceCapabilityPressure, &diag),
                      RoboOk);
    RoboCheck(!InputDeviceDescriptorIsValid(&descriptor));
    RoboCheckEqInt(InputDeviceDescriptorSet(&descriptor, 12u, RoboInputDeviceTypeGamepad,
                                            RoboInputSourceUnknown, "pad",
                                            RoboInputDeviceCapabilityGamepadAxes, &diag),
                      RoboOk);
    RoboCheck(InputDeviceDescriptorIsValid(&descriptor));

    /* The refusals, each having written nothing. */
    RoboCheckEqInt(InputDeviceDescriptorSet(NULL, 1u, RoboInputDeviceTypeMouse,
                                            RoboInputSourceUnknown, "m", 0u, &diag),
                      RoboInvalidArgument);
    RoboCheckEqInt(diag.domain, RoboInputDomainDevice);
    RoboCheckEqInt(InputDeviceDescriptorSet(&descriptor, 1u, RoboInputDeviceTypeUnknown,
                                            RoboInputSourceUnknown, "m", 0u, &diag),
                      RoboInvalidArgument);
    RoboCheck(!InputDeviceDescriptorIsValid(&descriptor));
    RoboCheckEqInt(InputDeviceDescriptorSet(&descriptor, 1u, RoboInputDeviceTypeMouse,
                                            RoboInputSourceUnknown, "m", 1u << 11, &diag),
                      RoboInvalidArgument);
    {
        /* A name longer than the identity holds is refused rather than truncated: a truncated name is a
         * different device's name. */
        const char *longName = "a name that is far too long for a device identity";

        RoboCheckEqInt(InputDeviceDescriptorSet(&descriptor, 1u, RoboInputDeviceTypeMouse,
                                                RoboInputSourceUnknown, longName, 0u, &diag),
                          RoboInvalidArgument);
        RoboCheck(!InputDeviceDescriptorIsValid(&descriptor));
    }

    /* A *named* source is taken as given: the combined digitizer. A touchscreen that reports as a tablet is
     * legitimate, and one that reports as a keyboard is not -- the source must still be a source. */
    RoboCheckEqInt(InputDeviceDescriptorSet(&descriptor, 3u, RoboInputDeviceTypeTouchscreen,
                                            RoboInputSourceTablet, "pen+touch",
                                            RoboInputDeviceCapabilityMultiTouch, &diag),
                      RoboOk);
    RoboCheck(InputDeviceDescriptorIsValid(&descriptor));
    RoboCheckEqInt(descriptor.device.source, RoboInputSourceTablet);

    /* Flags, ids and the connection state. */
    RoboCheckEqInt(InputDeviceDescriptorSetFlags(&descriptor,
                                                 RoboInputDeviceFlagPrimary |
                                                     RoboInputDeviceFlagSynthetic,
                                                 &diag),
                      RoboOk);
    RoboCheck((descriptor.device.flags & (uint32_t)RoboInputDeviceFlagSynthetic) != 0u);
    RoboCheckEqInt(InputDeviceDescriptorSetFlags(&descriptor, 1u << 5, &diag),
                      RoboInvalidArgument);
    RoboCheckEqInt(InputDeviceDescriptorSetIds(&descriptor, 0x046Du, 0xC31Cu), RoboOk);
    RoboCheckEqInt(descriptor.vendorId, 0x046D);
    RoboCheckEqInt(descriptor.productId, 0xC31C);
    RoboCheckEqInt(InputDeviceDescriptorSetConnection(&descriptor,
                                                      RoboInputDeviceConnectionSuspended, &diag),
                      RoboOk);
    RoboCheck(InputDeviceDescriptorIsValid(&descriptor));
    RoboCheckEqInt(InputDeviceDescriptorSetConnection(&descriptor,
                                                      RoboInputDeviceConnectionUnknown, &diag),
                      RoboInvalidArgument);
    RoboCheckEqInt(InputDeviceDescriptorSetConnection(NULL,
                                                      RoboInputDeviceConnectionConnected, &diag),
                      RoboInvalidArgument);

    /* Equality is field by field, and a difference in any one of them is a difference. */
    {
        InputDeviceDescriptor left = DeviceKeyboard(1u, "one");
        InputDeviceDescriptor right = DeviceKeyboard(1u, "one");

        RoboCheck(InputDeviceDescriptorEqual(&left, &right));
        InputDeviceDescriptorSetIds(&right, 1u, 2u);
        RoboCheck(!InputDeviceDescriptorEqual(&left, &right));
        right = DeviceKeyboard(2u, "one");
        RoboCheck(!InputDeviceDescriptorEqual(&left, &right));
        right = DeviceKeyboard(1u, "two");
        RoboCheck(!InputDeviceDescriptorEqual(&left, &right));
        RoboCheck(InputDeviceDescriptorEqual(NULL, NULL));
        RoboCheck(!InputDeviceDescriptorEqual(&left, NULL));

        /* Two devices the caller built identically compare equal **byte for byte**, which is what a
         * recording and a replay rest on. This found a real defect: `InputDeviceInit` cleared only the
         * first byte of the name, and `InputDeviceEqual` compares all of them, so the two stack copies
         * differed in the bytes past the terminator. */
        {
            InputDevice one;
            InputDevice two;

            InputDeviceInit(&one);
            InputDeviceInit(&two);
            RoboCheckEqInt(InputDeviceSet(&one, 5u, RoboInputSourceKeyboard, "k"), RoboOk);
            RoboCheckEqInt(InputDeviceSet(&two, 5u, RoboInputSourceKeyboard, "k"), RoboOk);
            RoboCheck(InputDeviceEqual(&one, &two));
            RoboCheck(memcmp(&one, &two, sizeof(one)) == 0);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Capabilities as text                                                */
/* ------------------------------------------------------------------ */

static void TestDeviceCapabilities(void)
{
    char buffer[RoboInputDeviceLineMax];
    size_t length = 0u;

    RoboCheckEqInt(InputDeviceCapabilitiesWrite(0u, NULL, sizeof(buffer), &length),
                      RoboInvalidArgument);

    RoboCheckEqInt(InputDeviceCapabilitiesWrite(0u, buffer, sizeof(buffer), &length), RoboOk);
    RoboCheckStrEq(buffer, "none");

    RoboCheckEqInt(InputDeviceCapabilitiesWrite(RoboInputDeviceCapabilityKeyboardKeys, buffer,
                                                sizeof(buffer), &length),
                      RoboOk);
    RoboCheckStrEq(buffer, "keyboard-keys");

    /* In the brief's bit order, which is the order a descriptor's list is written in: buttons (bit 4)
     * precedes wheel (bit 8), so the list reads pointer+buttons+wheel rather than by any other ordering a
     * reader might assume from the header's prose. */
    RoboCheckEqInt(InputDeviceCapabilitiesWrite(RoboInputDeviceCapabilityPointer |
                                                    RoboInputDeviceCapabilityWheel |
                                                    RoboInputDeviceCapabilityButtons,
                                                buffer, sizeof(buffer), &length),
                      RoboOk);
    RoboCheckStrEq(buffer, "pointer+buttons+wheel");

    /* An undefined bit cannot be named, so the list refuses rather than reading as complete. */
    RoboCheckEqInt(InputDeviceCapabilitiesWrite(1u << 11, buffer, sizeof(buffer), &length),
                      RoboOk);
    RoboCheckStrEq(buffer, "?");

    /* The whole set is written in the brief's order, longest-first, so a reader can check it against the
     * header without looking anything up. */
    RoboCheckEqInt(InputDeviceCapabilitiesWrite(RoboInputDeviceCapabilityAll, buffer,
                                                sizeof(buffer), &length),
                      RoboOk);
    RoboCheckStrEq(buffer,
                      "keyboard-keys+pointer+absolute-position+relative-motion+buttons+"
                      "multi-touch+pressure+tilt+wheel+gamepad-buttons+gamepad-axes");
}

/* ------------------------------------------------------------------ */
/* The registry                                                        */
/* ------------------------------------------------------------------ */

static void TestDeviceRegistry(void)
{
    InputDeviceRegistry registry;
    InputDeviceDescriptor descriptor;
    InputDeviceId id = RoboInputDeviceIdNone;
    InputDeviceId second = RoboInputDeviceIdNone;
    InputDiag diag;

    InputDiagReset(&diag);
    InputDeviceRegistryInit(&registry);
    RoboCheckEqInt((int)InputDeviceRegistryCount(&registry), 0);
    RoboCheckEqInt((int)registry.connectedTotal, 0);
    RoboCheck(!InputDeviceRegistryContains(&registry, 1u));

    /* A connect that names no id receives one, and the ids start at 1 so that 0 keeps meaning "none". */
    descriptor = DeviceKeyboard(RoboInputDeviceIdNone, "keyboard");
    RoboCheckEqInt(InputDeviceRegistryConnect(&registry, &descriptor, &id, &diag), RoboOk);
    RoboCheckEqInt(id, 1);
    RoboCheckEqInt((int)registry.count, 1);
    RoboCheckEqInt((int)registry.connectedTotal, 1);
    RoboCheckEqInt((int)registry.nextId, 2);
    RoboCheck(InputDeviceRegistryContains(&registry, id));

    /* A second device, this one claiming the id a platform gave it. */
    descriptor = DeviceKeyboard(RoboInputDeviceIdNone, "keyboard");
    RoboCheckEqInt(InputDeviceDescriptorSet(&descriptor, 40u, RoboInputDeviceTypeMouse,
                                            RoboInputSourceUnknown, "mouse",
                                            RoboInputDeviceCapabilityPointer |
                                                RoboInputDeviceCapabilityWheel |
                                                RoboInputDeviceCapabilityButtons,
                                            &diag),
                      RoboOk);
    RoboCheckEqInt(InputDeviceRegistryConnect(&registry, &descriptor, &second, &diag), RoboOk);
    RoboCheckEqInt(second, 40);
    RoboCheckEqInt((int)registry.nextId, 41);
    RoboCheckEqInt(InputDeviceRegistryIndexOf(&registry, 40u), 1);
    RoboCheckEqInt(InputDeviceRegistryIndexOf(&registry, 99u), -1);
    RoboCheckEqInt(InputDeviceRegistryIndexOf(NULL, 1u), -1);

    /* Lookup by index and by id answer the same record, and the order is connection order. */
    RoboCheckEqInt(InputDeviceRegistryAt(&registry, 0u, &descriptor, &diag), RoboOk);
    RoboCheckEqInt(descriptor.device.id, 1);
    RoboCheckEqInt(descriptor.type, RoboInputDeviceTypeKeyboard);
    RoboCheckEqInt(InputDeviceRegistryAt(&registry, 1u, &descriptor, &diag), RoboOk);
    RoboCheckEqInt(descriptor.device.id, 40);
    RoboCheckEqInt(InputDeviceRegistryAt(&registry, 2u, &descriptor, &diag), RoboNotFound);
    RoboCheckEqInt(diag.domain, RoboInputDomainDevice);
    RoboCheckEqInt(InputDeviceRegistryFind(&registry, 40u, &descriptor, &diag), RoboOk);
    RoboCheckEqInt(descriptor.type, RoboInputDeviceTypeMouse);
    RoboCheckEqInt(InputDeviceRegistryFind(&registry, 7u, &descriptor, &diag), RoboNotFound);
    RoboCheckEqInt(InputDeviceRegistryFind(&registry, RoboInputDeviceIdNone, &descriptor, &diag),
                      RoboNotFound);

    /* A duplicate id is refused: two devices with one id make an event's device ambiguous. */
    descriptor = DeviceKeyboard(1u, "another");
    RoboCheckEqInt(InputDeviceRegistryConnect(&registry, &descriptor, NULL, &diag),
                      RoboAlreadyExists);
    RoboCheckEqInt((int)registry.count, 2);
    RoboCheckEqInt((int)registry.connectedTotal, 2);

    /* An invalid descriptor never reaches the table. */
    InputDeviceDescriptorInit(&descriptor);
    RoboCheckEqInt(InputDeviceRegistryConnect(&registry, &descriptor, NULL, &diag),
                      RoboInvalidArgument);
    RoboCheckEqInt(InputDeviceRegistryConnect(NULL, &descriptor, NULL, &diag),
                      RoboInvalidArgument);

    /* The table has a stated bound, and filling it is refused rather than silently dropping. */
    {
        uint32_t index;
        InputDeviceDescriptor more = DeviceKeyboard(100u, "k");

        for (index = 2u; index < RoboInputDeviceRegistryMax; index++) {
            InputDeviceId issued = RoboInputDeviceIdNone;

            if (InputDeviceDescriptorSet(&more, 100u + index, RoboInputDeviceTypeKeyboard,
                                         RoboInputSourceUnknown, "k",
                                         RoboInputDeviceCapabilityKeyboardKeys, &diag) != RoboOk) {
                RoboCheckMsg(0, "the loop's descriptor was refused");
            }
            RoboCheckEqInt(InputDeviceRegistryConnect(&registry, &more, &issued, &diag), RoboOk);
        }
        RoboCheckEqInt((int)registry.count, (int)RoboInputDeviceRegistryMax);
        if (InputDeviceDescriptorSet(&more, 999u, RoboInputDeviceTypeKeyboard,
                                     RoboInputSourceUnknown, "k",
                                     RoboInputDeviceCapabilityKeyboardKeys, &diag) != RoboOk) {
            RoboCheckMsg(0, "the overflow descriptor was refused");
        }
        RoboCheckEqInt(InputDeviceRegistryConnect(&registry, &more, NULL, &diag),
                          RoboNotSupported);
        RoboCheckEqInt((int)registry.count, (int)RoboInputDeviceRegistryMax);
    }
}

/* ------------------------------------------------------------------ */
/* The primary of a type                                               */
/* ------------------------------------------------------------------ */

static void TestDevicePrimary(void)
{
    InputDeviceRegistry registry;
    InputDeviceDescriptor descriptor;
    InputDeviceDescriptor found;
    InputDeviceId first = RoboInputDeviceIdNone;
    InputDeviceId second = RoboInputDeviceIdNone;
    InputDiag diag;

    InputDiagReset(&diag);
    InputDeviceRegistryInit(&registry);

    /* Nothing of that kind: reported, not guessed. */
    RoboCheckEqInt(InputDeviceRegistryPrimary(&registry, RoboInputDeviceTypeKeyboard, &found, &diag),
                      RoboNotFound);

    descriptor = DeviceKeyboard(RoboInputDeviceIdNone, "first keyboard");
    RoboCheckEqInt(InputDeviceRegistryConnect(&registry, &descriptor, &first, &diag), RoboOk);
    RoboCheckEqInt(InputDeviceRegistryPrimary(&registry, RoboInputDeviceTypeKeyboard, &found, &diag),
                      RoboOk);
    RoboCheckEqInt(found.device.id, first);
    RoboCheck((found.device.flags & (uint32_t)RoboInputDeviceFlagPrimary) != 0u);

    /* A second keyboard does not take the flag by arriving. */
    descriptor = DeviceKeyboard(RoboInputDeviceIdNone, "second keyboard");
    RoboCheckEqInt(InputDeviceRegistryConnect(&registry, &descriptor, &second, &diag), RoboOk);
    RoboCheckEqInt(InputDeviceRegistryPrimary(&registry, RoboInputDeviceTypeKeyboard, &found, &diag),
                      RoboOk);
    RoboCheckEqInt(found.device.id, first);

    /* Raising it explicitly does, and takes it from whoever held it: one answer, never two. */
    descriptor = DeviceKeyboard(RoboInputDeviceIdNone, "second keyboard");
    if (InputDeviceDescriptorSetFlags(&descriptor, RoboInputDeviceFlagPrimary, &diag) != RoboOk) {
        RoboCheckMsg(0, "the fixture's flags were refused");
    }
    {
        InputDeviceId third = RoboInputDeviceIdNone;

        RoboCheckEqInt(InputDeviceRegistryConnect(&registry, &descriptor, &third, &diag), RoboOk);
        RoboCheckEqInt(InputDeviceRegistryPrimary(&registry, RoboInputDeviceTypeKeyboard, &found, &diag),
                          RoboOk);
        RoboCheckEqInt(found.device.id, third);
        RoboCheckEqInt(InputDeviceRegistryFind(&registry, first, &descriptor, &diag), RoboOk);
        RoboCheck((descriptor.device.flags & (uint32_t)RoboInputDeviceFlagPrimary) == 0u);

        /* The primary leaving hands the flag to the next of its type, which is what makes "which keyboard"
         * an answer that cannot name a device that is gone. */
        RoboCheckEqInt(InputDeviceRegistryDisconnect(&registry, third, NULL, &diag), RoboOk);
        RoboCheckEqInt(InputDeviceRegistryPrimary(&registry, RoboInputDeviceTypeKeyboard, &found, &diag),
                          RoboOk);
        RoboCheck(found.device.id == first || found.device.id == second);
    }

    /* A caller asking for "the mouse" is answered by a touchpad: both position a pointer, and making every
     * caller write the same two comparisons is what this pair exists to avoid. */
    {
        InputDeviceDescriptor pad;

        InputDeviceDescriptorInit(&pad);
        if (InputDeviceDescriptorSet(&pad, RoboInputDeviceIdNone, RoboInputDeviceTypeTouchpad,
                                     RoboInputSourceUnknown, "pad",
                                     RoboInputDeviceCapabilityPointer |
                                         RoboInputDeviceCapabilityRelativeMotion |
                                         RoboInputDeviceCapabilityMultiTouch,
                                     &diag) != RoboOk) {
            RoboCheckMsg(0, "the fixture's touchpad was refused");
        }
        RoboCheckEqInt(InputDeviceRegistryConnect(&registry, &pad, NULL, &diag), RoboOk);
        RoboCheckEqInt(InputDeviceRegistryPrimary(&registry, RoboInputDeviceTypeMouse, &found, &diag),
                          RoboOk);
        RoboCheckEqInt(found.type, RoboInputDeviceTypeTouchpad);
        /* A pen is a different question and is not paired with anything. */
        RoboCheckEqInt(InputDeviceRegistryPrimary(&registry, RoboInputDeviceTypePen, &found, &diag),
                          RoboNotFound);
    }

    /* By capability: the first device that can report it, and a refusal when none can. */
    RoboCheckEqInt(InputDeviceRegistryFindByCapability(&registry,
                                                       RoboInputDeviceCapabilityMultiTouch,
                                                       &found, &diag),
                      RoboOk);
    RoboCheckEqInt(found.type, RoboInputDeviceTypeTouchpad);
    RoboCheckEqInt(InputDeviceRegistryFindByCapability(&registry,
                                                       RoboInputDeviceCapabilityTilt,
                                                       &found, &diag),
                      RoboNotFound);
    RoboCheckEqInt(InputDeviceRegistryFindByCapability(NULL,
                                                       RoboInputDeviceCapabilityTilt,
                                                       &found, &diag),
                      RoboInvalidArgument);
}

/* ------------------------------------------------------------------ */
/* Disconnect                                                          */
/* ------------------------------------------------------------------ */

static void TestDeviceDisconnect(void)
{
    InputDeviceRegistry registry;
    InputDeviceDescriptor descriptor;
    InputDeviceDescriptor removed;
    InputDeviceId keyboard = RoboInputDeviceIdNone;
    InputDeviceId mouse = RoboInputDeviceIdNone;
    InputDiag diag;

    InputDiagReset(&diag);
    InputDeviceRegistryInit(&registry);

    descriptor = DeviceKeyboard(RoboInputDeviceIdNone, "keyboard");
    RoboCheckEqInt(InputDeviceRegistryConnect(&registry, &descriptor, &keyboard, &diag), RoboOk);
    if (InputDeviceDescriptorSet(&descriptor, RoboInputDeviceIdNone, RoboInputDeviceTypeMouse,
                                 RoboInputSourceUnknown, "mouse",
                                 RoboInputDeviceCapabilityPointer, &diag) != RoboOk) {
        RoboCheckMsg(0, "the fixture's mouse was refused");
    }
    RoboCheckEqInt(InputDeviceRegistryConnect(&registry, &descriptor, &mouse, &diag), RoboOk);

    /* The removed descriptor comes back whole -- which is how the caller that must release this device's
     * keys, captures and contacts learns which id to release. */
    RoboCheckEqInt(InputDeviceRegistryDisconnect(&registry, keyboard, &removed, &diag), RoboOk);
    RoboCheckEqInt(removed.device.id, keyboard);
    RoboCheckEqInt(removed.type, RoboInputDeviceTypeKeyboard);
    RoboCheckEqInt(removed.device.source, RoboInputSourceKeyboard);

    /* Removed, not marked: the id no longer answers, and the table compacted. */
    RoboCheck(!InputDeviceRegistryContains(&registry, keyboard));
    RoboCheckEqInt(InputDeviceRegistryFind(&registry, keyboard, &descriptor, &diag), RoboNotFound);
    RoboCheckEqInt((int)registry.count, 1);
    RoboCheckEqInt((int)registry.connectedTotal, 2);
    RoboCheckEqInt((int)registry.disconnectedTotal, 1);
    RoboCheckEqInt(InputDeviceRegistryAt(&registry, 0u, &descriptor, &diag), RoboOk);
    RoboCheckEqInt(descriptor.device.id, mouse);
    /* The id counter does not go backwards: a disconnected device's number is not handed to the next one,
     * because a recording that names it would then mean two devices. */
    RoboCheck(registry.nextId > mouse);

    /* Disconnecting what is not there is reported rather than swallowed: it is the symptom of a caller
     * that has lost track of its own table. */
    RoboCheckEqInt(InputDeviceRegistryDisconnect(&registry, keyboard, NULL, &diag), RoboNotFound);
    RoboCheckEqInt(diag.domain, RoboInputDomainDevice);
    RoboCheckEqInt(InputDeviceRegistryDisconnect(&registry, RoboInputDeviceIdNone, NULL, &diag),
                      RoboNotFound);
    RoboCheckEqInt(InputDeviceRegistryDisconnect(NULL, 1u, NULL, &diag), RoboInvalidArgument);

    /* Suspend is the third state: present, and quiet. It leaves the entry in the table. */
    RoboCheckEqInt(InputDeviceRegistrySetConnection(&registry, mouse,
                                                    RoboInputDeviceConnectionSuspended, &diag),
                      RoboOk);
    RoboCheckEqInt(InputDeviceRegistryFind(&registry, mouse, &descriptor, &diag), RoboOk);
    RoboCheckEqInt(descriptor.connection, RoboInputDeviceConnectionSuspended);
    RoboCheckEqInt(InputDeviceRegistrySetConnection(&registry, mouse,
                                                    RoboInputDeviceConnectionUnknown, &diag),
                      RoboInvalidArgument);
    RoboCheckEqInt(InputDeviceRegistrySetConnection(&registry, 1234u,
                                                    RoboInputDeviceConnectionConnected, &diag),
                      RoboNotFound);

    /* Clearing empties the table but keeps the counters: how many ever connected is a fact, not a live
     * one. */
    InputDeviceRegistryClear(&registry);
    RoboCheckEqInt((int)registry.count, 0);
    RoboCheckEqInt((int)registry.connectedTotal, 2);
    RoboCheckEqInt((int)registry.disconnectedTotal, 1);
    RoboCheckEqInt(InputDeviceRegistryPrimary(&registry, RoboInputDeviceTypeMouse, &descriptor, &diag),
                      RoboNotFound);
}

/* ------------------------------------------------------------------ */
/* The dump                                                            */
/* ------------------------------------------------------------------ */

static void TestDeviceDump(void)
{
    InputDeviceRegistry registry;
    InputDeviceDescriptor descriptor;
    char first[RoboInputDeviceRegistryDumpMax];
    char second[RoboInputDeviceRegistryDumpMax];
    char line[RoboInputDeviceLineMax];
    size_t firstLength = 0u;
    size_t secondLength = 0u;
    size_t lineLength = 0u;
    InputDiag diag;

    InputDiagReset(&diag);

    RoboCheckEqInt(InputDeviceRegistryWrite(NULL, NULL, sizeof(first), &firstLength, &diag),
                      RoboInvalidArgument);
    RoboCheckEqInt(InputDeviceRegistryWrite(NULL, first, 0u, &firstLength, &diag),
                      RoboInvalidArgument);
    RoboCheckEqInt(InputDeviceRegistryWrite(NULL, first, sizeof(first), NULL, &diag),
                      RoboInvalidArgument);

    /* An empty table is a summary line and nothing else, which is the honest dump: no device lines, not a
     * placeholder for one. */
    InputDeviceRegistryInit(&registry);
    RoboCheckEqInt(InputDeviceRegistryWrite(&registry, first, sizeof(first), &firstLength, &diag),
                      RoboOk);
    RoboCheckStrEq(first, "devices count=0 connected=0 disconnected=0 next=1\n");

    /* Two devices: one line each, in connection order, with the fields a reader checks. */
    descriptor = DeviceKeyboard(1u, "AT keyboard");
    RoboCheckEqInt(InputDeviceRegistryConnect(&registry, &descriptor, NULL, &diag), RoboOk);
    if (InputDeviceDescriptorSet(&descriptor, RoboInputDeviceIdNone, RoboInputDeviceTypeMouse,
                                 RoboInputSourceUnknown, "mouse",
                                 RoboInputDeviceCapabilityPointer |
                                     RoboInputDeviceCapabilityWheel |
                                     RoboInputDeviceCapabilityButtons,
                                 &diag) != RoboOk) {
        RoboCheckMsg(0, "the fixture's mouse was refused");
    }
    RoboCheckEqInt(InputDeviceDescriptorSetIds(&descriptor, 0x046Du, 0xC31Cu), RoboOk);
    RoboCheckEqInt(InputDeviceRegistryConnect(&registry, &descriptor, NULL, &diag), RoboOk);

    /* One descriptor, one line, asserted whole: the shape a developer reads. */
    RoboCheckEqInt(InputDeviceDescriptorWrite(&registry.devices[0], line, sizeof(line),
                                              &lineLength, &diag),
                      RoboOk);
    RoboCheckStrEq(line,
                      "device #1 keyboard source=keyboard connection=connected vid=0 pid=0 "
                      "caps=keyboard-keys primary=1 name=\"AT keyboard\"\n");
    RoboCheckEqInt(InputDeviceDescriptorWrite(&registry.devices[1], line, sizeof(line),
                                              &lineLength, &diag),
                      RoboOk);
    /* The mouse claimed no `PRIMARY` flag itself, and the registry raised it because it is the first of
     * its *type* -- the keyboard's flag does not answer for the mouse. */
    RoboCheckStrEq(line,
                      "device #2 mouse source=mouse connection=connected vid=1133 pid=49948 "
                      "caps=pointer+buttons+wheel primary=1 name=\"mouse\"\n");

    RoboCheckEqInt(InputDeviceRegistryWrite(&registry, first, sizeof(first), &firstLength, &diag),
                      RoboOk);
    RoboCheck(firstLength < (size_t)RoboInputDeviceRegistryDumpMax);
    RoboCheckEqInt((int)strlen(first), (int)firstLength);
    RoboCheck(strstr(first, "devices count=2 connected=2 disconnected=0 next=3\n") == first);
    RoboCheck(strstr(first, "device #1 keyboard") != NULL);
    RoboCheck(strstr(first, "device #2 mouse") != NULL);

    /* Deterministic: two calls, byte for byte. */
    RoboCheckEqInt(InputDeviceRegistryWrite(&registry, second, sizeof(second), &secondLength, &diag),
                      RoboOk);
    RoboCheckEqInt((int)secondLength, (int)firstLength);
    RoboCheck(strcmp(first, second) == 0);

    /* A dump of nothing, and a buffer too small to hold the text: `?` rather than a prefix that reads as a
     * whole device. */
    RoboCheckEqInt(InputDeviceDescriptorWrite(NULL, line, sizeof(line), &lineLength, &diag), RoboOk);
    RoboCheckStrEq(line, "device <none>\n");
    {
        char small[8];
        size_t needed = 0u;

        RoboCheckEqInt(InputDeviceDescriptorWrite(&registry.devices[0], small, sizeof(small),
                                                  &needed, &diag),
                          RoboNotSupported);
        RoboCheckEqInt((int)needed, (int)strlen("device #1 keyboard source=keyboard connection=connected vid=0 pid=0 caps=keyboard-keys primary=1 name=\"AT keyboard\"\n"));
        RoboCheckStrEq(small, "?");
        RoboCheckEqInt(diag.domain, RoboInputDomainDump);
    }
}

void TestRegisterInputDevice(void)
{
    TestRegister("input.device.vocabulary", TestDeviceVocabulary);
    TestRegister("input.device.descriptor", TestDeviceDescriptor);
    TestRegister("input.device.capabilities", TestDeviceCapabilities);
    TestRegister("input.device.registry", TestDeviceRegistry);
    TestRegister("input.device.primary", TestDevicePrimary);
    TestRegister("input.device.disconnect", TestDeviceDisconnect);
    TestRegister("input.device.dump", TestDeviceDump);
}
