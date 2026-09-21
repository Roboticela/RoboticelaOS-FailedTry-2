#include "RoboI18N/RoboI18N.h"

#include "RoboI18N/RoboI18NLocaleService.h"
#include "RoboI18N/RoboIanaData.h"

const char *I18nVersionString(void)
{
    return RoboI18nVersionString;
}

const char *I18nIanaFileDate(void)
{
    return IanaFileDate();
}

const char *I18nCldrVersionString(void)
{
    return RoboCldrVersionString;
}

Result I18nInit(void)
{
    I18nLocale und;
    I18nLocaleClear(&und);
    und.language[0] = 'u';
    und.language[1] = 'n';
    und.language[2] = 'd';
    und.language[3] = '\0';
    return I18nSetDefaultLocale(&und);
}

void I18nShutdown(void)
{
    I18nClearApplicationLocale();
}
