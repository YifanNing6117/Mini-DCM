#include "minidcm_build_info.h"

MiniDcm_VersionType MiniDcm_BuildInfo_GetFrameworkVersion(void)
{
    MiniDcm_VersionType version;

    version.major = MINIDCM_FRAMEWORK_VERSION_MAJOR;
    version.minor = MINIDCM_FRAMEWORK_VERSION_MINOR;
    version.patch = MINIDCM_FRAMEWORK_VERSION_PATCH;

    return version;
}

uint8_t MiniDcm_BuildInfo_GetNodeRole(void)
{
    return MINIDCM_NODE_ROLE_ECU_SERVER;
}

const char *MiniDcm_BuildInfo_GetFrameworkName(void)
{
    return "MiniDCM";
}
