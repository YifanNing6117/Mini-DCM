#ifndef MINIDCM_BUILD_INFO_H
#define MINIDCM_BUILD_INFO_H

#include "minidcm_types.h"

#define MINIDCM_FRAMEWORK_VERSION_MAJOR 2u
#define MINIDCM_FRAMEWORK_VERSION_MINOR 0u
#define MINIDCM_FRAMEWORK_VERSION_PATCH 0u

#define MINIDCM_NODE_ROLE_ECU_SERVER 0x01u
#define MINIDCM_NODE_ROLE_TESTER     0x02u

MiniDcm_VersionType MiniDcm_BuildInfo_GetFrameworkVersion(void);
uint8_t MiniDcm_BuildInfo_GetNodeRole(void);
const char *MiniDcm_BuildInfo_GetFrameworkName(void);

#endif
