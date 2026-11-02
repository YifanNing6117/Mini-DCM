#ifndef SESSION_CFG_H
#define SESSION_CFG_H

#include <stdint.h>

#define SESSION_CFG_DEFAULT_SESSION      ((uint8_t)0x01u)
#define SESSION_CFG_PROGRAMMING_SESSION  ((uint8_t)0x02u)
#define SESSION_CFG_EXTENDED_SESSION     ((uint8_t)0x03u)

#define SESSION_CFG_S3_TIMEOUT_MS        ((uint32_t)5000u)
#define SESSION_CFG_P2_SERVER_MAX_MS     ((uint16_t)50u)
#define SESSION_CFG_P2_STAR_SERVER_MS    ((uint16_t)500u)

uint8_t SessionCfg_IsSupported(uint8_t session);

#endif
