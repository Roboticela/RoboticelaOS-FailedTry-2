/*
 * RoboInput device table (Phase 13.2) — see `RoboInputDevice.h`.
 *
 * The file is three pieces, in the order a caller meets them:
 *
 *   **the vocabulary**    a device type, eleven capability bits, a connection state: names, masks, the
 *                         default source a type reports as, and the compact list a dump reads.
 *   **the descriptor**    one device as a value -- the identity an event carries plus what kind of thing it
 *                         is, what it can report and where it came from -- with the two consistency rules
 *                         the type makes checkable.
 *   **the registry**      the fixed table: connect assigning an id and the primary of a type, disconnect
 *                         *removing* the entry and handing it back, and the deterministic dump.
 *
 * Nothing here reads a device or talks to a platform. A caller that has a keyboard tells the registry about
 * it; the registry's job is to remember, to answer, and to hand back what a disconnect removed so that the
 * state machines above can release it.
 */

#include "RoboInput/RoboInputDevice.h"

#include "RoboInputInternal.h"

#include <stddef.h>
#include <stdint.h>

/* ------------------------------------------------------------------ */
/* Kind of hardware                                                    */
/* ------------------------------------------------------------------ */

int InputDeviceTypeIsKnown(InputDeviceType type)
{
    return (int)type >= (int)RoboInputDeviceTypeUnknown &&
           (int)type < (int)RoboInputDeviceTypeCount;
}

const char *InputDeviceTypeName(InputDeviceType type)
{
    switch (type) {
    case RoboInputDeviceTypeUnknown: return "unknown";
    case RoboInputDeviceTypeKeyboard: return "keyboard";
    case RoboInputDeviceTypeMouse: return "mouse";
    case RoboInputDeviceTypeTouchscreen: return "touchscreen";
    case RoboInputDeviceTypeTouchpad: return "touchpad";
    case RoboInputDeviceTypePen: return "pen";
    case RoboInputDeviceTypeGamepad: return "gamepad";
    default: return "unknown";
    }
}

InputSource InputDeviceTypeDefaultSource(InputDeviceType type)
{
    switch (type) {
    /* A touchpad positions a pointer and has no absolute coordinates, so it reports as a mouse: the
     * distinction this header exists for, since testing the *type* here would be wrong. */
    case RoboInputDeviceTypeMouse:
    case RoboInputDeviceTypeTouchpad: return RoboInputSourceMouse;
    case RoboInputDeviceTypeKeyboard: return RoboInputSourceKeyboard;
    case RoboInputDeviceTypeTouchscreen: return RoboInputSourceTouch;
    case RoboInputDeviceTypePen: return RoboInputSourceTablet;
    case RoboInputDeviceTypeGamepad: return RoboInputSourceGameController;
    default: return RoboInputSourceUnknown;
    }
}

/* ------------------------------------------------------------------ */
/* Capabilities                                                        */
/* ------------------------------------------------------------------ */

uint32_t InputDeviceCapabilityMask(uint32_t index)
{
    return index < RoboInputDeviceCapabilityCount ? (uint32_t)(1u << index) : 0u;
}

const char *InputDeviceCapabilityName(uint32_t index)
{
    switch (index) {
    case 0u: return "keyboard-keys";
    case 1u: return "pointer";
    case 2u: return "absolute-position";
    case 3u: return "relative-motion";
    case 4u: return "buttons";
    case 5u: return "multi-touch";
    case 6u: return "pressure";
    case 7u: return "tilt";
    case 8u: return "wheel";
    case 9u: return "gamepad-buttons";
    case 10u: return "gamepad-axes";
    default: return NULL;
    }
}

int InputDeviceCapabilitiesAreKnown(uint32_t capabilities)
{
    return (capabilities & ~(uint32_t)RoboInputDeviceCapabilityAll) == 0u;
}

int InputDeviceCapabilitiesHas(uint32_t capabilities, uint32_t mask)
{
    return (capabilities & mask) == mask;
}

Result InputDeviceCapabilitiesWrite(uint32_t capabilities,
                                    char *buffer,
                                    size_t capacity,
                                    size_t *outLength)
{
    InputWriter text;
    uint32_t index;
    int wrote = 0;

    if (!RiWriterArgumentsAreValid(buffer, capacity, outLength)) {
        return RoboInvalidArgument;
    }
    RiWriterInit(&text, buffer, capacity);

    if (!InputDeviceCapabilitiesAreKnown(capabilities)) {
        /* An undefined bit is not a capability this vocabulary can name, and writing the ones it can name
         * would be a list that reads as complete while a bit is missing from it. */
        RiWriterText(&text, "?");
    } else if (capabilities == 0u) {
        RiWriterText(&text, "none");
    } else {
        for (index = 0u; index < RoboInputDeviceCapabilityCount; index++) {
            uint32_t mask = InputDeviceCapabilityMask(index);

            if ((capabilities & mask) == 0u) {
                continue;
            }
            if (wrote) {
                RiWriterChar(&text, '+');
            }
            RiWriterText(&text, InputDeviceCapabilityName(index));
            wrote = 1;
        }
    }
    return RiWriterFinish(&text, outLength, NULL);
}

/* ------------------------------------------------------------------ */
/* Connection state                                                    */
/* ------------------------------------------------------------------ */

int InputDeviceConnectionIsKnown(InputDeviceConnection connection)
{
    return (int)connection >= (int)RoboInputDeviceConnectionUnknown &&
           (int)connection < (int)RoboInputDeviceConnectionCount;
}

const char *InputDeviceConnectionName(InputDeviceConnection connection)
{
    switch (connection) {
    case RoboInputDeviceConnectionConnected: return "connected";
    case RoboInputDeviceConnectionSuspended: return "suspended";
    default: return "unknown";
    }
}

/* ------------------------------------------------------------------ */
/* The descriptor                                                      */
/* ------------------------------------------------------------------ */

void InputDeviceDescriptorInit(InputDeviceDescriptor *descriptor)
{
    if (!descriptor) {
        return;
    }
    InputDeviceInit(&descriptor->device);
    descriptor->type = RoboInputDeviceTypeUnknown;
    descriptor->capabilities = 0u;
    descriptor->vendorId = 0u;
    descriptor->productId = 0u;
    descriptor->connection = RoboInputDeviceConnectionUnknown;
}

Result InputDeviceDescriptorSet(InputDeviceDescriptor *descriptor,
                                InputDeviceId id,
                                InputDeviceType type,
                                InputSource source,
                                const char *name,
                                uint32_t capabilities,
                                InputDiag *diag)
{
    InputDeviceDescriptor filled;
    InputSource resolvedSource = source;

    if (!descriptor) {
        return RoboInputDiag(diag, RoboInputDomainDevice, RoboInvalidArgument,
                             "no descriptor to fill");
    }
    /* Cleared **before** anything is validated, so a refusal leaves an empty descriptor rather than a
     * mixture of this call and the previous accepted one -- the rule this door's own comment states, and
     * the reason a caller can check "did the set take?" by asking whether the result is valid. */
    InputDeviceDescriptorInit(descriptor);
    if (!InputDeviceTypeIsKnown(type) || type == RoboInputDeviceTypeUnknown) {
        return RoboInputDiag(diag, RoboInputDomainDevice, RoboInvalidArgument,
                             "a device descriptor needs a kind of hardware");
    }
    if (!InputDeviceCapabilitiesAreKnown(capabilities)) {
        return RoboInputDiag(diag, RoboInputDomainDevice, RoboInvalidArgument,
                             "capabilities carry a bit this build does not define");
    }
    /* Silence means "the ordinary source for this kind of thing" (this module's header); a named source is
     * taken as given, which is what a combined pen-and-touch digitizer needs. */
    if (resolvedSource == RoboInputSourceUnknown) {
        resolvedSource = InputDeviceTypeDefaultSource(type);
    }
    if (!InputSourceIsKnown(resolvedSource)) {
        return RoboInputDiag(diag, RoboInputDomainDevice, RoboInvalidArgument,
                             "a device descriptor needs a source this build defines");
    }

    InputDeviceDescriptorInit(&filled);
    if (InputDeviceSet(&filled.device, id, resolvedSource, name) != RoboOk) {
        /* `InputDeviceSet` refuses only a name longer than its capacity; the diagnostic is this domain's
         * because it is this call that was handed the name. */
        return RoboInputDiag(diag, RoboInputDomainDevice, RoboInvalidArgument,
                             "a device name longer than the identity can hold is refused, never truncated");
    }
    filled.type = type;
    filled.capabilities = capabilities;
    filled.connection = RoboInputDeviceConnectionConnected;

    *descriptor = filled;
    return RoboOk;
}

Result InputDeviceDescriptorSetIds(InputDeviceDescriptor *descriptor,
                                   uint16_t vendorId,
                                   uint16_t productId)
{
    if (!descriptor) {
        return RoboInvalidArgument;
    }
    descriptor->vendorId = vendorId;
    descriptor->productId = productId;
    return RoboOk;
}

Result InputDeviceDescriptorSetFlags(InputDeviceDescriptor *descriptor,
                                     uint32_t flags,
                                     InputDiag *diag)
{
    if (!descriptor) {
        return RoboInputDiag(diag, RoboInputDomainDevice, RoboInvalidArgument,
                             "no descriptor to set flags on");
    }
    if ((flags & ~(uint32_t)RoboInputDeviceFlagAll) != 0u) {
        return RoboInputDiag(diag, RoboInputDomainDevice, RoboInvalidArgument,
                             "device flags carry a bit this build does not define");
    }
    descriptor->device.flags = flags;
    return RoboOk;
}

Result InputDeviceDescriptorSetConnection(InputDeviceDescriptor *descriptor,
                                          InputDeviceConnection connection,
                                          InputDiag *diag)
{
    if (!descriptor) {
        return RoboInputDiag(diag, RoboInputDomainDevice, RoboInvalidArgument,
                             "no descriptor to set a connection state on");
    }
    if (!InputDeviceConnectionIsKnown(connection) ||
        connection == RoboInputDeviceConnectionUnknown) {
        return RoboInputDiag(diag, RoboInputDomainDevice, RoboInvalidArgument,
                             "a device needs a connection state this build defines");
    }
    descriptor->connection = connection;
    return RoboOk;
}

int InputDeviceDescriptorIsValid(const InputDeviceDescriptor *descriptor)
{
    if (!descriptor) {
        return 0;
    }
    if (!InputDeviceIsValid(&descriptor->device)) {
        return 0;
    }
    if (!InputDeviceTypeIsKnown(descriptor->type) || descriptor->type == RoboInputDeviceTypeUnknown) {
        return 0;
    }
    if (!InputDeviceConnectionIsKnown(descriptor->connection) ||
        descriptor->connection == RoboInputDeviceConnectionUnknown) {
        return 0;
    }
    if (!InputDeviceCapabilitiesAreKnown(descriptor->capabilities)) {
        return 0;
    }
    /* The identity's source must be one this build defines, and it need not match the type's default --
     * that is the combined-digitizer case the header states. */
    if (!InputSourceIsKnown(descriptor->device.source)) {
        return 0;
    }
    /* The two consistency rules: a descriptor may not name a kind of hardware while denying every
     * capability of it. A keyboard by definition reports keys; a gamepad by definition has buttons or
     * axes, and one with neither is a device this model has no way to hear from. */
    if (descriptor->type == RoboInputDeviceTypeKeyboard &&
        !InputDeviceCapabilitiesHas(descriptor->capabilities,
                                    RoboInputDeviceCapabilityKeyboardKeys)) {
        return 0;
    }
    if (descriptor->type == RoboInputDeviceTypeGamepad &&
        !InputDeviceCapabilitiesHas(descriptor->capabilities,
                                   RoboInputDeviceCapabilityGamepadButtons) &&
        !InputDeviceCapabilitiesHas(descriptor->capabilities,
                                   RoboInputDeviceCapabilityGamepadAxes)) {
        /* **Any** of the two, not both: `InputDeviceCapabilitiesHas` asks whether every bit of its mask is
         * set, so testing the pair with one call would require a pad that has buttons *and* axes -- which
         * would refuse a pad with only one of them, the reading this rule exists to allow. */
        return 0;
    }
    return 1;
}

int InputDeviceDescriptorHasCapability(const InputDeviceDescriptor *descriptor,
                                       uint32_t capability)
{
    return descriptor != NULL &&
           InputDeviceCapabilitiesHas(descriptor->capabilities, capability);
}

int InputDeviceDescriptorEqual(const InputDeviceDescriptor *a,
                               const InputDeviceDescriptor *b)
{
    if (!a || !b) {
        return a == b;
    }
    return InputDeviceEqual(&a->device, &b->device) && a->type == b->type &&
           a->capabilities == b->capabilities && a->vendorId == b->vendorId &&
           a->productId == b->productId && a->connection == b->connection;
}

Result InputDeviceDescriptorWrite(const InputDeviceDescriptor *descriptor,
                                  char *buffer,
                                  size_t capacity,
                                  size_t *outLength,
                                  InputDiag *diag)
{
    InputWriter text;
    char capabilities[RoboInputDeviceLineMax];
    size_t capabilitiesLength = 0u;

    if (!RiWriterArgumentsAreValid(buffer, capacity, outLength)) {
        return RoboInputDiag(diag, RoboInputDomainDump, RoboInvalidArgument,
                             "a dump needs a buffer, a capacity and somewhere to put the length");
    }
    RiWriterInit(&text, buffer, capacity);

    if (!descriptor) {
        RiWriterText(&text, "device <none>\n");
        return RiWriterFinish(&text, outLength, diag);
    }
    if (InputDeviceCapabilitiesWrite(descriptor->capabilities, capabilities,
                                     sizeof(capabilities), &capabilitiesLength) != RoboOk) {
        RiTextCopy(capabilities, sizeof(capabilities), "?");
    }

    RiWriterText(&text, "device #");
    RiWriterU64(&text, (uint64_t)descriptor->device.id);
    RiWriterChar(&text, ' ');
    RiWriterText(&text, InputDeviceTypeName(descriptor->type));
    RiWriterFieldText(&text, " source", InputSourceName(descriptor->device.source));
    RiWriterFieldText(&text, " connection",
                      InputDeviceConnectionName(descriptor->connection));
    RiWriterFieldU64(&text, " vid", (uint64_t)descriptor->vendorId);
    RiWriterFieldU64(&text, " pid", (uint64_t)descriptor->productId);
    RiWriterFieldText(&text, " caps", capabilities);
    if ((descriptor->device.flags & (uint32_t)RoboInputDeviceFlagPrimary) != 0u) {
        RiWriterText(&text, " primary=1");
    }
    if ((descriptor->device.flags & (uint32_t)RoboInputDeviceFlagSynthetic) != 0u) {
        RiWriterText(&text, " synthetic=1");
    }
    RiWriterText(&text, " name=\"");
    {
        char name[RoboInputDeviceNameMax];

        InputDeviceName(&descriptor->device, name, sizeof(name));
        RiWriterText(&text, name);
    }
    RiWriterText(&text, "\"\n");
    return RiWriterFinish(&text, outLength, diag);
}

/* ------------------------------------------------------------------ */
/* The registry                                                        */
/* ------------------------------------------------------------------ */

void InputDeviceRegistryInit(InputDeviceRegistry *registry)
{
    uint32_t index;

    if (!registry) {
        return;
    }
    for (index = 0u; index < RoboInputDeviceRegistryMax; index++) {
        InputDeviceDescriptorInit(&registry->devices[index]);
    }
    registry->count = 0u;
    registry->nextId = (InputDeviceId)1u;
    registry->connectedTotal = 0u;
    registry->disconnectedTotal = 0u;
}

uint32_t InputDeviceRegistryCount(const InputDeviceRegistry *registry)
{
    return registry ? registry->count : 0u;
}

int32_t InputDeviceRegistryIndexOf(const InputDeviceRegistry *registry, InputDeviceId id)
{
    uint32_t index;

    if (!registry || id == RoboInputDeviceIdNone) {
        return -1;
    }
    for (index = 0u; index < registry->count; index++) {
        if (registry->devices[index].device.id == id) {
            return (int32_t)index;
        }
    }
    return -1;
}

int InputDeviceRegistryContains(const InputDeviceRegistry *registry, InputDeviceId id)
{
    return InputDeviceRegistryIndexOf(registry, id) >= 0;
}

Result InputDeviceRegistryAt(const InputDeviceRegistry *registry,
                             uint32_t index,
                             InputDeviceDescriptor *out,
                             InputDiag *diag)
{
    if (!registry || !out) {
        return RoboInputDiag(diag, RoboInputDomainDevice, RoboInvalidArgument,
                             "a registry and somewhere to put the descriptor are both needed");
    }
    if (index >= registry->count) {
        return RoboInputDiag(diag, RoboInputDomainDevice, RoboNotFound,
                             "no device at that index");
    }
    *out = registry->devices[index];
    return RoboOk;
}

Result InputDeviceRegistryFind(const InputDeviceRegistry *registry,
                               InputDeviceId id,
                               InputDeviceDescriptor *out,
                               InputDiag *diag)
{
    int32_t index;

    if (!registry || !out) {
        return RoboInputDiag(diag, RoboInputDomainDevice, RoboInvalidArgument,
                             "a registry and somewhere to put the descriptor are both needed");
    }
    index = InputDeviceRegistryIndexOf(registry, id);
    if (index < 0) {
        return RoboInputDiag(diag, RoboInputDomainDevice, RoboNotFound,
                             "no device with that id");
    }
    *out = registry->devices[index];
    return RoboOk;
}

/* The primary rule, written once: the first connected device of a type is its primary unless another
 * connect already raised one, and raising one takes the flag from whoever held it, so "the main keyboard"
 * is a question with exactly one answer. */
static void RaDeviceClaimPrimary(InputDeviceRegistry *registry, uint32_t index)
{
    InputDeviceType type = registry->devices[index].type;
    uint32_t other;

    for (other = 0u; other < registry->count; other++) {
        if (other == index || registry->devices[other].type != type) {
            continue;
        }
        registry->devices[other].device.flags &= ~(uint32_t)RoboInputDeviceFlagPrimary;
    }
    registry->devices[index].device.flags |= (uint32_t)RoboInputDeviceFlagPrimary;
}

static int RaRegistryHasPrimary(const InputDeviceRegistry *registry, InputDeviceType type)
{
    uint32_t index;

    for (index = 0u; index < registry->count; index++) {
        if (registry->devices[index].type == type &&
            (registry->devices[index].device.flags & (uint32_t)RoboInputDeviceFlagPrimary) != 0u) {
            return 1;
        }
    }
    return 0;
}

Result InputDeviceRegistryConnect(InputDeviceRegistry *registry,
                                  const InputDeviceDescriptor *description,
                                  InputDeviceId *outId,
                                  InputDiag *diag)
{
    InputDeviceDescriptor stored;
    int wantsPrimary;

    if (!registry || !description) {
        return RoboInputDiag(diag, RoboInputDomainDevice, RoboInvalidArgument,
                             "a registry and a descriptor are both needed");
    }
    if (!InputDeviceDescriptorIsValid(description)) {
        return RoboInputDiag(diag, RoboInputDomainDevice, RoboInvalidArgument,
                             "the descriptor is not one this build may table");
    }
    if (registry->count >= RoboInputDeviceRegistryMax) {
        return RoboInputDiag(diag, RoboInputDomainDevice, RoboNotSupported,
                             "the device table is full");
    }

    stored = *description;
    if (stored.device.id == RoboInputDeviceIdNone) {
        stored.device.id = registry->nextId;
        registry->nextId++;
    } else if (InputDeviceRegistryIndexOf(registry, stored.device.id) >= 0) {
        return RoboInputDiag(diag, RoboInputDomainDevice, RoboAlreadyExists,
                             "that device id is already connected");
    } else if (stored.device.id >= registry->nextId) {
        /* An id issued by the platform must not be handed out again later, so the counter steps over it.
         * A *collision* is still refused above; this is only about not reusing a number. */
        registry->nextId = stored.device.id + 1u;
    }

    /* The caller's own flag is honoured; when it did not say, the first of a type becomes its primary. */
    wantsPrimary = (stored.device.flags & (uint32_t)RoboInputDeviceFlagPrimary) != 0u;
    registry->devices[registry->count] = stored;
    registry->count++;
    if (wantsPrimary || !RaRegistryHasPrimary(registry, stored.type)) {
        RaDeviceClaimPrimary(registry, registry->count - 1u);
    }
    registry->connectedTotal++;
    if (outId) {
        *outId = registry->devices[registry->count - 1u].device.id;
    }
    return RoboOk;
}

Result InputDeviceRegistryDisconnect(InputDeviceRegistry *registry,
                                     InputDeviceId id,
                                     InputDeviceDescriptor *outRemoved,
                                     InputDiag *diag)
{
    int32_t index;
    uint32_t move;

    if (!registry) {
        return RoboInputDiag(diag, RoboInputDomainDevice, RoboInvalidArgument,
                             "no registry to disconnect from");
    }
    index = InputDeviceRegistryIndexOf(registry, id);
    if (index < 0) {
        return RoboInputDiag(diag, RoboInputDomainDevice, RoboNotFound,
                             "no device with that id to disconnect");
    }
    if (outRemoved) {
        *outRemoved = registry->devices[index];
    }
    /* Removed rather than marked: a table that kept the entry would leave a lookup that still answers and
     * a key still held (this module's header). */
    for (move = (uint32_t)index; move + 1u < registry->count; move++) {
        registry->devices[move] = registry->devices[move + 1u];
    }
    registry->count--;
    InputDeviceDescriptorInit(&registry->devices[registry->count]);
    registry->disconnectedTotal++;

    /* The type's primary may have just left. The next connected device of that type takes the flag, so a
     * caller asking "which keyboard" never gets an answer about a device that is gone. */
    if (!RaRegistryHasPrimary(registry, outRemoved ? outRemoved->type : RoboInputDeviceTypeUnknown)) {
        uint32_t scan;

        for (scan = 0u; scan < registry->count; scan++) {
            if (outRemoved && registry->devices[scan].type == outRemoved->type) {
                RaDeviceClaimPrimary(registry, scan);
                break;
            }
        }
    }
    return RoboOk;
}

Result InputDeviceRegistrySetConnection(InputDeviceRegistry *registry,
                                        InputDeviceId id,
                                        InputDeviceConnection connection,
                                        InputDiag *diag)
{
    int32_t index;

    if (!registry) {
        return RoboInputDiag(diag, RoboInputDomainDevice, RoboInvalidArgument,
                             "no registry to change");
    }
    if (!InputDeviceConnectionIsKnown(connection) ||
        connection == RoboInputDeviceConnectionUnknown) {
        return RoboInputDiag(diag, RoboInputDomainDevice, RoboInvalidArgument,
                             "a device needs a connection state this build defines");
    }
    index = InputDeviceRegistryIndexOf(registry, id);
    if (index < 0) {
        return RoboInputDiag(diag, RoboInputDomainDevice, RoboNotFound,
                             "no device with that id");
    }
    registry->devices[index].connection = connection;
    return RoboOk;
}

static int RaTypeMatches(InputDeviceType type, InputDeviceType wanted)
{
    if (type == wanted) {
        return 1;
    }
    /* A touchpad *is* a pointing device: a caller asking for the mouse that moves a pointer means either,
     * and refusing to answer would make every caller write the same two comparisons. Nothing else is
     * paired -- a pen and a touchscreen are genuinely different questions. */
    if (wanted == RoboInputDeviceTypeMouse &&
        type == RoboInputDeviceTypeTouchpad) {
        return 1;
    }
    return 0;
}

Result InputDeviceRegistryPrimary(const InputDeviceRegistry *registry,
                                  InputDeviceType type,
                                  InputDeviceDescriptor *out,
                                  InputDiag *diag)
{
    uint32_t index;
    int found = 0;
    InputDeviceDescriptor candidate;

    if (!registry || !out) {
        return RoboInputDiag(diag, RoboInputDomainDevice, RoboInvalidArgument,
                             "a registry and somewhere to put the descriptor are both needed");
    }
    InputDeviceDescriptorInit(&candidate);
    for (index = 0u; index < registry->count; index++) {
        if (!RaTypeMatches(registry->devices[index].type, type)) {
            continue;
        }
        if ((registry->devices[index].device.flags & (uint32_t)RoboInputDeviceFlagPrimary) != 0u) {
            *out = registry->devices[index];
            return RoboOk;
        }
        /* Remember the first of the type, so a table that has one only because nothing raised a primary
         * still answers. */
        if (!found) {
            candidate = registry->devices[index];
            found = 1;
        }
    }
    if (found) {
        *out = candidate;
        return RoboOk;
    }
    return RoboInputDiag(diag, RoboInputDomainDevice, RoboNotFound,
                         "no connected device of that kind");
}

Result InputDeviceRegistryFindByCapability(const InputDeviceRegistry *registry,
                                           uint32_t capability,
                                           InputDeviceDescriptor *out,
                                           InputDiag *diag)
{
    uint32_t index;

    if (!registry || !out) {
        return RoboInputDiag(diag, RoboInputDomainDevice, RoboInvalidArgument,
                             "a registry and somewhere to put the descriptor are both needed");
    }
    for (index = 0u; index < registry->count; index++) {
        if (InputDeviceDescriptorHasCapability(&registry->devices[index], capability)) {
            *out = registry->devices[index];
            return RoboOk;
        }
    }
    return RoboInputDiag(diag, RoboInputDomainDevice, RoboNotFound,
                         "no connected device can report that");
}

void InputDeviceRegistryClear(InputDeviceRegistry *registry)
{
    uint32_t index;

    if (!registry) {
        return;
    }
    for (index = 0u; index < registry->count; index++) {
        InputDeviceDescriptorInit(&registry->devices[index]);
    }
    registry->count = 0u;
}

Result InputDeviceRegistryWrite(const InputDeviceRegistry *registry,
                                char *buffer,
                                size_t capacity,
                                size_t *outLength,
                                InputDiag *diag)
{
    InputWriter text;
    uint32_t index;
    Result status = RoboOk;

    if (!RiWriterArgumentsAreValid(buffer, capacity, outLength)) {
        return RoboInputDiag(diag, RoboInputDomainDump, RoboInvalidArgument,
                             "a dump needs a buffer, a capacity and somewhere to put the length");
    }
    RiWriterInit(&text, buffer, capacity);
    if (!registry) {
        RiWriterText(&text, "devices <none>\n");
        return RiWriterFinish(&text, outLength, diag);
    }

    RiWriterText(&text, "devices count=");
    RiWriterU64(&text, (uint64_t)registry->count);
    RiWriterFieldU64(&text, " connected", registry->connectedTotal);
    RiWriterFieldU64(&text, " disconnected", registry->disconnectedTotal);
    RiWriterFieldU64(&text, " next", (uint64_t)registry->nextId);
    RiWriterChar(&text, '\n');

    for (index = 0u; index < registry->count && status == RoboOk; index++) {
        char line[RoboInputDeviceLineMax];
        size_t lineLength = 0u;

        if (InputDeviceDescriptorWrite(&registry->devices[index], line, sizeof(line),
                                       &lineLength, NULL) != RoboOk) {
            RiWriterText(&text, "device ?\n");
            continue;
        }
        RiWriterText(&text, line);
    }
    return RiWriterFinish(&text, outLength, diag);
}
