#ifndef DID_CFG_H
#define DID_CFG_H

#include <stdint.h>

typedef struct
{
    uint16_t did;
    uint8_t data_len;
    uint8_t readable;
    uint8_t writable;
} DidCfg_EntryType;

const DidCfg_EntryType *DidCfg_Find(uint16_t did);

#endif
