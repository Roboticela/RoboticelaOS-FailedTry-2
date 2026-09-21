/*
 * RoboError suite (Goal 1.5).
 */

#include "RoboBase/RoboError.h"
#include "RoboTest.h"

#include <string.h>

static void TestErrorCodesAndStrings(void)
{
    const Result codes[] = {
        RoboOk,
        RoboError,
        RoboInvalidArgument,
        RoboOutOfMemory,
        RoboNotFound,
        RoboAlreadyExists,
        RoboPermissionDenied,
        RoboNotSupported,
        RoboTimeout,
        RoboIoError,
        RoboParseError,
        RoboInitError,
        RoboBusy
    };
    const int n = (int)(sizeof(codes) / sizeof(codes[0]));
    int i;
    Result unknown = (Result)9999;

    RoboCheck(ResultIsOk(RoboOk));
    RoboCheck(!ResultIsError(RoboOk));
    RoboCheckStrEq(ErrorString(RoboOk), "Success");
    RoboCheckEqInt(n, (int)(RoboResultLast - RoboResultFirst + 1));

    for (i = 0; i < n; i++) {
        const char *s = ErrorString(codes[i]);
        RoboCheck(ResultIsKnown(codes[i]));
        RoboCheck(s != NULL && s[0] != '\0');
        if (codes[i] != RoboOk) {
            RoboCheck(ResultIsError(codes[i]));
            RoboCheck(strcmp(s, "Success") != 0);
        }
    }

    RoboCheck(!ResultIsKnown(unknown));
    RoboCheck(ResultIsError(unknown));
    RoboCheckStrEq(ErrorString(unknown), "Unknown error");

    RoboCheckEqInt(RoboErrAlreadyInitialized, RoboAlreadyExists);
    RoboCheckEqInt(RoboErrNotInitialized, RoboInitError);
    RoboCheckEqInt(RoboErrInvalidArgument, RoboInvalidArgument);
}

static void TestErrorInfo(void)
{
    ErrorInfo info;
    Result st;

    RoboCheckEqInt(
        ErrorInfoSet(NULL, RoboNotFound, "t", "m", "f", 1, RoboOk),
        RoboInvalidArgument);
    ErrorInfoReset(NULL);

    ErrorInfoReset(&info);
    RoboCheckEqInt(info.code, RoboOk);

    st = ErrorInfoSet(&info, RoboParseError, "RoboHTML", "bad token",
                             "Parser.c", 42, RoboIoError);
    RoboCheckEqInt(st, RoboParseError);
    RoboCheckEqInt(info.code, RoboParseError);
    RoboCheckStrEq(info.subsystem, "RoboHTML");
    RoboCheckStrEq(info.message, "bad token");
    RoboCheckStrEq(info.file, "Parser.c");
    RoboCheckEqInt(info.line, 42);
    RoboCheckEqInt(info.underlying, RoboIoError);

    st = ErrorInfoSet(&info, RoboOk, NULL, NULL, NULL, 0, RoboError);
    RoboCheckEqInt(st, RoboOk);
    RoboCheckEqInt(info.code, RoboOk);
}

void TestRegisterError(void)
{
    TestRegister("error.codes_strings", TestErrorCodesAndStrings);
    TestRegister("error.info", TestErrorInfo);
}
