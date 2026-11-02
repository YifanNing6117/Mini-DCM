#include "security_cfg.h"

static const uint8_t s_security_cfg_seed[SECURITY_CFG_SEED_LEN] =
{
    0x12u, 0x34u, 0x56u, 0x78u
};

const uint8_t *SecurityCfg_GetSeed(void)
{
    return s_security_cfg_seed;
}

uint8_t SecurityCfg_CalcKeyByte(uint8_t seed_byte)
{
    return (uint8_t)(seed_byte ^ SECURITY_CFG_KEY_XOR_MASK);
}
