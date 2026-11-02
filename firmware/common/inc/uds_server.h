#ifndef UDS_SERVER_H
#define UDS_SERVER_H

#include <stdint.h>

#define UDS_SERVER_MAX_RESPONSE_LEN 64u

void UdsServer_Dispatch(const uint8_t *request, uint8_t request_len, uint32_t now_ms, uint8_t *response, uint8_t *response_len, uint8_t response_max_len);

#endif
