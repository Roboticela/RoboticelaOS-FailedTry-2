/*
 * RoboBase suite (Goal 1.5).
 */

#include "RoboBase/RoboBase.h"
#include "RoboTest.h"

static void test_base_version(void)
{
    robo_version_t ver;
    const char *s;

    s = robo_base_version();
    ROBO_CHECK(s != NULL);
    ROBO_CHECK(s[0] != '\0');
    ROBO_CHECK_STR_EQ(s, "0.1.0");

    ROBO_CHECK_EQ_INT(robo_base_get_version(NULL), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_base_get_version(&ver), ROBO_OK);
    ROBO_CHECK_EQ_INT(ver.major, 0);
    ROBO_CHECK_EQ_INT(ver.minor, 1);
    ROBO_CHECK_EQ_INT(ver.patch, 0);
}

static void test_base_init_shutdown(void)
{
    ROBO_CHECK_EQ_INT(robo_base_is_initialized(), 0);
    ROBO_CHECK_EQ_INT(robo_base_shutdown(), ROBO_INIT_ERROR);

    ROBO_CHECK_EQ_INT(robo_base_init(), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_base_is_initialized(), 1);
    ROBO_CHECK_EQ_INT(robo_base_init(), ROBO_ALREADY_EXISTS);

    ROBO_CHECK_STR_EQ(robo_base_version(), "0.1.0");

    ROBO_CHECK_EQ_INT(robo_base_shutdown(), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_base_is_initialized(), 0);
    ROBO_CHECK_EQ_INT(robo_base_shutdown(), ROBO_INIT_ERROR);

    ROBO_CHECK_EQ_INT(robo_base_init(), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_base_shutdown(), ROBO_OK);
}

void robo_test_register_base(void)
{
    robo_test_register("base.version", test_base_version);
    robo_test_register("base.init_shutdown", test_base_init_shutdown);
}
