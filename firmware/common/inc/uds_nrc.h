#ifndef UDS_NRC_H
#define UDS_NRC_H

#include <stdint.h>

#define UDS_NRC_SERVICE_NOT_SUPPORTED          ((uint8_t)0x11u)
#define UDS_NRC_SUBFUNCTION_NOT_SUPPORTED      ((uint8_t)0x12u)
#define UDS_NRC_INCORRECT_MESSAGE_LENGTH       ((uint8_t)0x13u)
#define UDS_NRC_REQUEST_OUT_OF_RANGE           ((uint8_t)0x31u)
#define UDS_NRC_SECURITY_ACCESS_DENIED         ((uint8_t)0x33u)
#define UDS_NRC_INVALID_KEY                    ((uint8_t)0x35u)
#define UDS_NRC_EXCEED_NUMBER_OF_ATTEMPTS      ((uint8_t)0x36u)

#endif
