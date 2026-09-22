#include "usb/usb_hid.h"
#include "usb/usb_core.h"
#include "input/input.h"
#include "kio.h"

static int HidAttach(RoboUsbDevice *dev)
{
    if (!dev)
        return -1;
    if (dev->protocol != 1 && dev->protocol != 2)
        return -1;
    kinfo("[INFO] [USB] HID boot %s attached\n",
            dev->protocol == 1 ? "keyboard" : "mouse");
    if (dev->protocol == 1)
        InputSetKeyboardReady(1);
    else
        InputSetMouseReady(1);
    return 0;
}

static void HidDetach(RoboUsbDevice *dev)
{
    (void)dev;
}

static void HidPoll(RoboUsbDevice *dev)
{
    /*
     * Interrupt IN reports land here once xHCI transfer rings complete
     * control/bulk/interrupt pipes. Boot-protocol report layout:
     *   keyboard: modifiers + 6 keycodes
     *   mouse: buttons + dx + dy
     */
    (void)dev;
}

static const RoboUsbClassDriver hidKbd = {
    "usb-hid-boot",
    RoboUsbClassHid,
    0x01,
    0xFF,
    HidAttach,
    HidDetach,
    HidPoll
};

int UsbHidRegister(void)
{
    return UsbRegisterClass(&hidKbd);
}
