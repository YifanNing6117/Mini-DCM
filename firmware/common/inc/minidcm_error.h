#ifndef MINIDCM_ERROR_H
#define MINIDCM_ERROR_H

#include "minidcm_types.h"

typedef uint16_t MiniDcm_ErrorCodeType;

#define MINIDCM_E_NONE                    ((MiniDcm_ErrorCodeType)0x0000u)
#define MINIDCM_E_PARAM                   ((MiniDcm_ErrorCodeType)0x0001u)
#define MINIDCM_E_NOT_INIT                ((MiniDcm_ErrorCodeType)0x0002u)
#define MINIDCM_E_BUSY                    ((MiniDcm_ErrorCodeType)0x0003u)
#define MINIDCM_E_TIMEOUT                 ((MiniDcm_ErrorCodeType)0x0004u)
#define MINIDCM_E_OVERFLOW                ((MiniDcm_ErrorCodeType)0x0005u)
#define MINIDCM_E_UNSUPPORTED             ((MiniDcm_ErrorCodeType)0x0006u)
#define MINIDCM_E_CAN_TX                  ((MiniDcm_ErrorCodeType)0x0100u)
#define MINIDCM_E_CAN_RX                  ((MiniDcm_ErrorCodeType)0x0101u)
#define MINIDCM_E_CANTP_FORMAT            ((MiniDcm_ErrorCodeType)0x0200u)
#define MINIDCM_E_UDS_NRC                 ((MiniDcm_ErrorCodeType)0x0300u)
#define MINIDCM_E_SECURITY_LOCKED         ((MiniDcm_ErrorCodeType)0x0400u)
#define MINIDCM_E_SENSOR_COMMUNICATION    ((MiniDcm_ErrorCodeType)0x0500u)

typedef struct
{
    MiniDcm_ErrorCodeType last_error;
    uint32_t error_count;
} MiniDcm_ErrorStateType;

void MiniDcm_Error_Init(void);
void MiniDcm_Error_Report(MiniDcm_ErrorCodeType error);
MiniDcm_ErrorCodeType MiniDcm_Error_GetLast(void);
uint32_t MiniDcm_Error_GetCount(void);
void MiniDcm_Error_Clear(void);

#endif
