#include "RoboI18N/RoboI18N.h"

#include "RoboI18N/RoboIanaData.h"

const char *robo_i18n_version_string(void)
{
    return ROBO_I18N_VERSION_STRING;
}

const char *robo_i18n_iana_file_date(void)
{
    return robo_iana_file_date();
}

const char *robo_i18n_cldr_version_string(void)
{
    return ROBO_CLDR_VERSION_STRING;
}

robo_result_t robo_i18n_init(void)
{
    robo_i18n_locale_t und;
    robo_i18n_locale_clear(&und);
    und.language[0] = 'u';
    und.language[1] = 'n';
    und.language[2] = 'd';
    und.language[3] = '\0';
    return robo_i18n_set_default_locale(&und);
}

void robo_i18n_shutdown(void)
{
}
