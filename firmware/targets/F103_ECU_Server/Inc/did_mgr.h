#ifndef DID_MGR_H
#define DID_MGR_H

#include <stdint.h>

#define DID_MGR_OK 0u
#define DID_MGR_NOT_FOUND 1u
#define DID_MGR_BUFFER_TOO_SMALL 2u
#define DID_MGR_READ_ONLY 3u
#define DID_MGR_INVALID_LENGTH 4u

uint8_t DidMgr_Read(uint16_t did, uint8_t *data, uint8_t *data_len, uint8_t max_len);
uint8_t DidMgr_Write(uint16_t did, const uint8_t *data, uint8_t data_len);

#endif
