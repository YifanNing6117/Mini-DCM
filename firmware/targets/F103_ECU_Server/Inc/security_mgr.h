#ifndef SECURITY_MGR_H
#define SECURITY_MGR_H

#include <stdint.h>

#define SECURITY_MGR_SEED_LEN 4u
#define SECURITY_MGR_KEY_LEN 4u
#define SECURITY_MGR_MAX_FAILED_ATTEMPTS 3u
#define SECURITY_MGR_LOCK_TIME_MS 10000u

typedef enum
{
    SECURITY_MGR_OK = 0u,
    SECURITY_MGR_ERR_LOCKED,
    SECURITY_MGR_ERR_INVALID_KEY
} SecurityMgr_StatusType;

void SecurityMgr_Init(void);
SecurityMgr_StatusType SecurityMgr_RequestSeed(uint32_t now_ms, uint8_t *seed, uint8_t seed_len);
SecurityMgr_StatusType SecurityMgr_ValidateKey(uint32_t now_ms, const uint8_t *key, uint8_t key_len);
uint8_t SecurityMgr_IsUnlocked(void);
uint8_t SecurityMgr_GetFailedAttempts(void);

#endif
