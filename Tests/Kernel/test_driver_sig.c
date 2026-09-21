#include <stdio.h>
#include <stdint.h>
#include "drivers/signature/sha256.h"
#include "drivers/signature/sha512.h"
#include "drivers/signature/ed25519.h"
#include "drivers/package.h"
#include "drivers/driver.h"

static int fail(const char *name)
{
    printf("FAIL  %s\n", name);
    return 0;
}

static int pass(const char *name)
{
    printf("PASS  %s\n", name);
    return 1;
}

int main(void)
{
    static const uint8_t seed[32] = {
        0x9d,0x61,0xb1,0x9d,0xef,0xfd,0x5a,0x60,0xba,0x84,0x4a,0xf4,0x92,0xec,
        0x2c,0xc4,0x44,0x49,0xc5,0x69,0x7b,0x32,0x69,0x19,0x70,0x3b,0xac,0x03,
        0x1c,0xae,0x7f,0x60
    };
    static const uint8_t kid[16] = {
        'r','o','b','o','-','t','e','s','t','-','k','e','y','-','0','1'
    };
    static const uint8_t kid2[16] = {
        'u','n','k','n','o','w','n','-','k','e','y','-','0','0','0','1'
    };
    uint8_t pk[32], sk[64], pk2[32], sk2[64], pkg[512], tamp[512];
    uint8_t payload[16] = { 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15 };
    RoboRdrvInfo info;
    int n, i, ok = 1;

    if (!Sha256Selftest())
        ok &= fail("sha256");
    else
        pass("sha256");
    if (!Sha512Selftest())
        ok &= fail("sha512");
    else
        pass("sha512");
    if (!Ed25519Selftest())
        ok &= fail("ed25519");
    else
        pass("ed25519");
    if (!DriverRegistrySelftest())
        ok &= fail("registry");
    else
        pass("registry");

    RdrvTrustClear();
    if (Ed25519Keypair(pk, sk, seed) != 0)
        return 1;
    if (RdrvTrustAdd(kid, pk) != 0)
        return 1;

    n = RdrvBuild(pkg, sizeof(pkg), payload, 16, 0, 1, 0,
                        RoboArchX8664, 0x010200u, kid, sk, 1);
    if (n <= 0 || RdrvVerify(pkg, (size_t)n, &info, 0) != 0 ||
        !info.signedOk)
        ok &= fail("valid signature");
    else
        pass("valid signature");

    for (i = 0; i < n; i++)
        tamp[i] = pkg[i];
    tamp[sizeof(RoboRdrvHeader)] ^= 0x5A;
    if (RdrvVerify(tamp, (size_t)n, 0, 0) == 0)
        ok &= fail("modified package");
    else
        pass("modified package");

    tamp[sizeof(RoboRdrvHeader)] ^= 0x5A;
    tamp[8] ^= 1;
    if (RdrvVerify(tamp, (size_t)n, 0, 0) == 0)
        ok &= fail("modified manifest");
    else
        pass("modified manifest");

    {
        uint8_t seed2[32];
        for (i = 0; i < 32; i++)
            seed2[i] = (uint8_t)(seed[i] ^ 0xA5);
        Ed25519Keypair(pk2, sk2, seed2);
        n = RdrvBuild(pkg, sizeof(pkg), payload, 16, 0, 1, 0,
                            RoboArchX8664, 1, kid2, sk2, 1);
        if (n <= 0 || RdrvVerify(pkg, (size_t)n, 0, 0) == 0)
            ok &= fail("unknown key");
        else
            pass("unknown key");
    }

    n = RdrvBuild(pkg, sizeof(pkg), payload, 16, 0, 2, 0,
                        RoboArchX8664, 1, kid, sk, 1);
    if (n <= 0 || RdrvVerify(pkg, (size_t)n, 0, 0) == 0)
        ok &= fail("wrong ABI");
    else
        pass("wrong ABI");

    n = RdrvBuild(pkg, sizeof(pkg), payload, 16, 0, 1, 0,
                        RoboArchArm64, 1, kid, sk, 1);
    if (n <= 0 || RdrvVerify(pkg, (size_t)n, 0, 0) == 0)
        ok &= fail("wrong architecture");
    else
        pass("wrong architecture");

    n = RdrvBuild(pkg, sizeof(pkg), payload, 16, 0, 1, 0,
                        RoboArchX8664, 1, kid, 0, 0);
    if (n <= 0 || RdrvVerify(pkg, (size_t)n, 0, 0) == 0)
        ok &= fail("unsigned production");
    else
        pass("unsigned production");

    n = RdrvBuild(pkg, sizeof(pkg), payload, 16, RoboRdrvFlagTest, 1, 0,
                        RoboArchX8664, 1, kid, 0, 0);
    if (n <= 0 || RdrvVerify(pkg, (size_t)n, 0, 1) != 0)
        ok &= fail("unsigned test mode");
    else
        pass("unsigned test mode");
    if (n <= 0 || RdrvVerify(pkg, (size_t)n, 0, 0) == 0)
        ok &= fail("unsigned test in production");
    else
        pass("unsigned test in production");

    if (!RdrvSelftest())
        ok &= fail("package selftest");
    else
        pass("package selftest");

    printf("%s\n", ok ? "DRIVER_SIG_TEST_OK" : "DRIVER_SIG_TEST_FAIL");
    return ok ? 0 : 1;
}
