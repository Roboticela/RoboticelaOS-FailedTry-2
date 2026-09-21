/*
 * RoboBase suite (Goal 1.5).
 */

#include "RoboBase/RoboBase.h"
#include "RoboTest.h"

static void TestBaseVersion(void)
{
    Version ver;
    const char *s;

    s = BaseVersion();
    RoboCheck(s != NULL);
    RoboCheck(s[0] != '\0');
    RoboCheckStrEq(s, "0.1.0");

    RoboCheckEqInt(BaseGetVersion(NULL), RoboInvalidArgument);
    RoboCheckEqInt(BaseGetVersion(&ver), RoboOk);
    RoboCheckEqInt(ver.major, 0);
    RoboCheckEqInt(ver.minor, 1);
    RoboCheckEqInt(ver.patch, 0);
}

static void TestBaseInitShutdown(void)
{
    RoboCheckEqInt(BaseIsInitialized(), 0);
    RoboCheckEqInt(BaseShutdown(), RoboInitError);

    RoboCheckEqInt(BaseInit(), RoboOk);
    RoboCheckEqInt(BaseIsInitialized(), 1);
    RoboCheckEqInt(BaseInit(), RoboAlreadyExists);

    RoboCheckStrEq(BaseVersion(), "0.1.0");

    RoboCheckEqInt(BaseShutdown(), RoboOk);
    RoboCheckEqInt(BaseIsInitialized(), 0);
    RoboCheckEqInt(BaseShutdown(), RoboInitError);

    RoboCheckEqInt(BaseInit(), RoboOk);
    RoboCheckEqInt(BaseShutdown(), RoboOk);
}

void TestRegisterBase(void)
{
    TestRegister("base.version", TestBaseVersion);
    TestRegister("base.init_shutdown", TestBaseInitShutdown);
}
