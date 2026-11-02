#ifndef SECURITY_CFG_H
#define SECURITY_CFG_H

#include <stdint.h>

#define SECURITY_CFG_SEED_LEN              ((uint8_t)4u)
#define SECURITY_CFG_KEY_LEN               ((uint8_t)4u)
#define SECURITY_CFG_MAX_FAILED_ATTEMPTS   ((uint8_t)3u)
#define SECURITY_CFG_LOCK_TIME_MS          ((uint32_t)10000u)
#define SECURITY_CFG_KEY_XOR_MASK          ((uint8_t)0xA5u)

const uint8_t *SecurityCfg_GetSeed(void);
uint8_t SecurityCfg_CalcKeyByte(uint8_t seed_byte);

#endif
