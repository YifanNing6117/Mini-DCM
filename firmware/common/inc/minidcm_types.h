#ifndef MINIDCM_TYPES_H
#define MINIDCM_TYPES_H

#include <stddef.h>
#include <stdint.h>

typedef uint8_t boolean_t;

#ifndef TRUE
#define TRUE 1u
#endif

#ifndef FALSE
#define FALSE 0u
#endif

typedef uint8_t Std_ReturnType;

#define E_OK 0u
#define E_NOT_OK 1u

typedef enum
{
    MINIDCM_FALSE = 0u,
    MINIDCM_TRUE = 1u
} MiniDcm_BoolType;

typedef struct
{
    uint8_t major;
    uint8_t minor;
    uint8_t patch;
} MiniDcm_VersionType;

#define MINIDCM_UNUSED(x) ((void)(x))

#endif
