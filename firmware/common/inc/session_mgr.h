#ifndef SESSION_MGR_H
#define SESSION_MGR_H

#include <stdint.h>

#define SESSION_MGR_DEFAULT_SESSION 0x01u
#define SESSION_MGR_PROGRAMMING_SESSION 0x02u
#define SESSION_MGR_EXTENDED_SESSION 0x03u
#define SESSION_MGR_S3_TIMEOUT_MS 5000u

typedef enum
{
    SESSION_MGR_OK = 0u,
    SESSION_MGR_ERR_UNSUPPORTED
} SessionMgr_StatusType;

void SessionMgr_Init(void);
SessionMgr_StatusType SessionMgr_SetSession(uint8_t session, uint32_t now_ms);
void SessionMgr_MainFunction(uint32_t now_ms);
void SessionMgr_OnUdsActivity(uint32_t now_ms);
uint8_t SessionMgr_GetSession(void);

#endif
