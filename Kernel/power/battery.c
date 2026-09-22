#include "power/battery.h"
#include "arch/x86_64/acpi/acpi.h"
#include "kio.h"

/*
 * ACPI battery (_BIF / _BST) needs an AML interpreter. Until that lands,
 * expose a stable API that reports unavailable without inventing numbers.
 * Laptops with working AML will fill this path later; desktops stay honest.
 */

static int gReady;
static int gPresent;

int BatteryInit(void)
{
    RoboFadtInfo fadt;

    gReady = 1;
    gPresent = 0;
    /* FADT presence alone does not mean a battery exists. */
    if (AcpiFadtGet(&fadt))
        kinfo("[INFO] [POWER] battery API ready (no AML battery yet)\n");
    return 1;
}

int BatteryPresent(void)
{
    return gReady && gPresent;
}

int BatteryGetStatus(RoboBatteryStatus *out)
{
    if (!out || !gReady)
        return 0;
    out->state = RoboBatteryUnavailable;
    out->percent = 0xFFFFFFFFu;
    out->voltageMv = 0;
    out->rateMw = 0;
    out->capacityMah = 0;
    out->remainingMah = 0;
    return 1;
}
