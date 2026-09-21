/*
 * RoboLog suite (Goal 1.5).
 */

#include "RoboBase/RoboLog.h"
#include "RoboTest.h"

#include <string.h>

#define CAP 8
#define CAP_LEN 256

static char gLines[CAP][CAP_LEN];
static LogLevel gLevels[CAP];
static int gCount;

static void CaptureReset(void)
{
    gCount = 0;
}

static void CaptureWriter(LogLevel level, const char *line, void *ud)
{
    size_t i;
    (void)ud;
    if (gCount >= CAP || !line) {
        return;
    }
    gLevels[gCount] = level;
    for (i = 0; i + 1 < CAP_LEN && line[i]; i++) {
        gLines[gCount][i] = line[i];
    }
    gLines[gCount][i] = '\0';
    gCount++;
}

static int HasPrefix(const char *prefix)
{
    size_t n = strlen(prefix);
    int i;
    for (i = 0; i < gCount; i++) {
        if (strncmp(gLines[i], prefix, n) == 0) {
            return 1;
        }
    }
    return 0;
}

static int contains(const char *needle)
{
    int i;
    for (i = 0; i < gCount; i++) {
        if (strstr(gLines[i], needle)) {
            return 1;
        }
    }
    return 0;
}

static void TestLoggingLevels(void)
{
    LogSetWriter(CaptureWriter, NULL);
    LogSetMinLevel(RoboLogDebug);

    CaptureReset();
    LogInfo("hello info");
    RoboCheckEqInt(gCount, 1);
    RoboCheckEqInt(gLevels[0], RoboLogInfo);
    RoboCheck(HasPrefix("[INFO]"));

    CaptureReset();
    LogWarn("hello warn");
    RoboCheck(HasPrefix("[WARN]"));

    CaptureReset();
    LogError("hello error");
    RoboCheck(HasPrefix("[ERROR]"));

    CaptureReset();
    LogDebug("hello debug");
    RoboCheck(HasPrefix("[DEBUG]"));

    CaptureReset();
    LogInfo("value=%d name=%s", 42, "robo");
    RoboCheck(contains("value=42"));
    RoboCheck(contains("name=robo"));

    CaptureReset();
    LogInfo(NULL);
    LogWrite(RoboLogInfo, NULL, 0, NULL);
    RoboCheckEqInt(gCount, 2);

    CaptureReset();
    LogSetMinLevel(RoboLogWarn);
    LogDebug("hidden");
    LogInfo("hidden");
    LogWarn("shown");
    RoboCheckEqInt(gCount, 1);
    RoboCheck(HasPrefix("[WARN]"));
    LogSetMinLevel(RoboLogDebug);

    CaptureReset();
    LogError("with location");
    RoboCheck(contains("SuiteRoboLog.c:"));
    RoboCheck(contains("with location"));

    LogSetWriter(NULL, NULL);
}

void TestRegisterLog(void)
{
    TestRegister("log.levels_format_filter", TestLoggingLevels);
}
