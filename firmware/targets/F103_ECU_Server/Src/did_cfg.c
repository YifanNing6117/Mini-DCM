#include "did_cfg.h"

#define DID_CFG_TRUE 1u
#define DID_CFG_FALSE 0u

static const DidCfg_EntryType s_did_cfg_table[] =
{
    {0xF190u, 4u, DID_CFG_TRUE, DID_CFG_FALSE},
    {0xF192u, 21u, DID_CFG_TRUE, DID_CFG_FALSE},
    {0xF193u, 16u, DID_CFG_TRUE, DID_CFG_TRUE},
    {0xF186u, 1u, DID_CFG_TRUE, DID_CFG_FALSE},
    {0xF187u, 1u, DID_CFG_TRUE, DID_CFG_TRUE},
    {0xF188u, 3u, DID_CFG_TRUE, DID_CFG_FALSE},
    {0xF189u, 1u, DID_CFG_TRUE, DID_CFG_FALSE},
    {0xF18Au, 4u, DID_CFG_TRUE, DID_CFG_FALSE},
    {0xF18Bu, 4u, DID_CFG_TRUE, DID_CFG_FALSE},
    {0xF18Cu, 2u, DID_CFG_TRUE, DID_CFG_FALSE},
    {0xF18Du, 4u, DID_CFG_TRUE, DID_CFG_FALSE},
    {0xF18Eu, 1u, DID_CFG_TRUE, DID_CFG_FALSE},
    {0xF18Fu, 4u, DID_CFG_TRUE, DID_CFG_FALSE},
    {0xF194u, 4u, DID_CFG_TRUE, DID_CFG_FALSE},
    {0xF195u, 4u, DID_CFG_TRUE, DID_CFG_FALSE},
    {0xF196u, 4u, DID_CFG_TRUE, DID_CFG_FALSE},
    {0xF197u, 4u, DID_CFG_TRUE, DID_CFG_FALSE},
    {0xF198u, 4u, DID_CFG_TRUE, DID_CFG_FALSE},
    {0xF199u, 4u, DID_CFG_TRUE, DID_CFG_FALSE},
    {0xF19Au, 1u, DID_CFG_TRUE, DID_CFG_FALSE},
    {0xF19Bu, 1u, DID_CFG_TRUE, DID_CFG_FALSE},
    {0xF19Cu, 4u, DID_CFG_TRUE, DID_CFG_FALSE},
    {0xF19Du, 4u, DID_CFG_TRUE, DID_CFG_FALSE},
    {0xF19Eu, 4u, DID_CFG_TRUE, DID_CFG_FALSE},
    {0xF1A0u, 4u, DID_CFG_TRUE, DID_CFG_FALSE},
    {0xF1A1u, 4u, DID_CFG_TRUE, DID_CFG_FALSE},
    {0xF1A2u, 4u, DID_CFG_TRUE, DID_CFG_FALSE},
    {0xF1A3u, 4u, DID_CFG_TRUE, DID_CFG_FALSE},
    {0xF1A4u, 4u, DID_CFG_TRUE, DID_CFG_FALSE},
    {0xF1A5u, 4u, DID_CFG_TRUE, DID_CFG_FALSE},
    {0xF1A6u, 24u, DID_CFG_TRUE, DID_CFG_TRUE},
    {0xF1B0u, 4u, DID_CFG_TRUE, DID_CFG_FALSE},
    {0xF1B1u, 4u, DID_CFG_TRUE, DID_CFG_FALSE},
    {0xF1B2u, 4u, DID_CFG_TRUE, DID_CFG_FALSE},
    {0xF1B3u, 4u, DID_CFG_TRUE, DID_CFG_FALSE},
    {0xF1B4u, 4u, DID_CFG_TRUE, DID_CFG_FALSE},
    {0xF1B5u, 4u, DID_CFG_TRUE, DID_CFG_FALSE},
    {0xF1B6u, 4u, DID_CFG_TRUE, DID_CFG_FALSE},
    {0xF1B7u, 2u, DID_CFG_TRUE, DID_CFG_FALSE},
    {0xF1C0u, 2u, DID_CFG_TRUE, DID_CFG_FALSE},
    {0xF1C1u, 2u, DID_CFG_TRUE, DID_CFG_FALSE},
    {0xF1C2u, 2u, DID_CFG_TRUE, DID_CFG_FALSE},
    {0xF1C3u, 2u, DID_CFG_TRUE, DID_CFG_FALSE},
    {0xF1C4u, 2u, DID_CFG_TRUE, DID_CFG_FALSE},
    {0xF1C5u, 4u, DID_CFG_TRUE, DID_CFG_FALSE},
    {0xF1C6u, 1u, DID_CFG_TRUE, DID_CFG_FALSE}
};

const DidCfg_EntryType *DidCfg_Find(uint16_t did)
{
    uint8_t i;

    for (i = 0u; i < (uint8_t)(sizeof(s_did_cfg_table) / sizeof(s_did_cfg_table[0])); i++)
    {
        if (s_did_cfg_table[i].did == did)
        {
            return &s_did_cfg_table[i];
        }
    }

    return 0;
}
