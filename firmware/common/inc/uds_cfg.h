#ifndef UDS_CFG_H
#define UDS_CFG_H

#include <stdint.h>

typedef void (*UdsCfg_ServiceHandlerType)(const uint8_t *request,
                                          uint8_t request_len,
                                          uint32_t now_ms,
                                          uint8_t *response,
                                          uint8_t *response_len,
                                          uint8_t response_max_len);

typedef struct
{
    uint8_t sid;
    UdsCfg_ServiceHandlerType handler;
} UdsCfg_ServiceType;

const UdsCfg_ServiceType *UdsCfg_GetServiceTable(uint8_t *service_count);

void UdsServer_HandleDiagnosticSessionControl(const uint8_t *request,
                                              uint8_t request_len,
                                              uint32_t now_ms,
                                              uint8_t *response,
                                              uint8_t *response_len,
                                              uint8_t response_max_len);

void UdsServer_HandleClearDiagnosticInformation(const uint8_t *request,
                                                uint8_t request_len,
                                                uint32_t now_ms,
                                                uint8_t *response,
                                                uint8_t *response_len,
                                                uint8_t response_max_len);

void UdsServer_HandleReadDtcInformation(const uint8_t *request,
                                        uint8_t request_len,
                                        uint32_t now_ms,
                                        uint8_t *response,
                                        uint8_t *response_len,
                                        uint8_t response_max_len);

void UdsServer_HandleReadDataByIdentifier(const uint8_t *request,
                                          uint8_t request_len,
                                          uint32_t now_ms,
                                          uint8_t *response,
                                          uint8_t *response_len,
                                          uint8_t response_max_len);

void UdsServer_HandleSecurityAccess(const uint8_t *request,
                                    uint8_t request_len,
                                    uint32_t now_ms,
                                    uint8_t *response,
                                    uint8_t *response_len,
                                    uint8_t response_max_len);

void UdsServer_HandleWriteDataByIdentifier(const uint8_t *request,
                                           uint8_t request_len,
                                           uint32_t now_ms,
                                           uint8_t *response,
                                           uint8_t *response_len,
                                           uint8_t response_max_len);

void UdsServer_HandleRoutineControl(const uint8_t *request,
                                    uint8_t request_len,
                                    uint32_t now_ms,
                                    uint8_t *response,
                                    uint8_t *response_len,
                                    uint8_t response_max_len);

void UdsServer_HandleTesterPresent(const uint8_t *request,
                                   uint8_t request_len,
                                   uint32_t now_ms,
                                   uint8_t *response,
                                   uint8_t *response_len,
                                   uint8_t response_max_len);

#endif
