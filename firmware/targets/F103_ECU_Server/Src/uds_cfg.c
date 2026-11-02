#include "uds_cfg.h"

static const UdsCfg_ServiceType s_uds_cfg_service_table[] =
{
    {0x10u, UdsServer_HandleDiagnosticSessionControl},
    {0x14u, UdsServer_HandleClearDiagnosticInformation},
    {0x19u, UdsServer_HandleReadDtcInformation},
    {0x22u, UdsServer_HandleReadDataByIdentifier},
    {0x27u, UdsServer_HandleSecurityAccess},
    {0x2Eu, UdsServer_HandleWriteDataByIdentifier},
    {0x31u, UdsServer_HandleRoutineControl},
    {0x3Eu, UdsServer_HandleTesterPresent}
};

const UdsCfg_ServiceType *UdsCfg_GetServiceTable(uint8_t *service_count)
{
    if (service_count != 0)
    {
        *service_count = (uint8_t)(sizeof(s_uds_cfg_service_table) / sizeof(s_uds_cfg_service_table[0]));
    }

    return s_uds_cfg_service_table;
}
