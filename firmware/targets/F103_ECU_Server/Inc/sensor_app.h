#ifndef SENSOR_APP_H
#define SENSOR_APP_H

#include "minidcm_types.h"

#define SENSOR_APP_SOURCE_MOCK              ((uint8_t)0x01u)
#define SENSOR_APP_SOURCE_MPU6050           ((uint8_t)0x02u)
#define SENSOR_APP_HEALTH_OK                ((uint8_t)0x01u)
#define SENSOR_APP_HEALTH_FAULT             ((uint8_t)0x02u)
#define SENSOR_APP_FAULT_NONE               ((uint8_t)0x00u)
#define SENSOR_APP_FAULT_COMMUNICATION      ((uint8_t)0x01u)
#define SENSOR_APP_FAULT_INITIALIZATION     ((uint8_t)0x02u)
#define SENSOR_APP_FAULT_DEVICE_ID          ((uint8_t)0x04u)

typedef enum
{
    SENSOR_APP_PROVIDER_OK = 0u,
    SENSOR_APP_PROVIDER_ERR_COMMUNICATION,
    SENSOR_APP_PROVIDER_ERR_DEVICE_ID,
    SENSOR_APP_PROVIDER_ERR_NOT_READY
} SensorApp_ProviderStatusType;

typedef struct
{
    int16_t acceleration_x_mg;
    int16_t acceleration_y_mg;
    int16_t acceleration_z_mg;
    int16_t temperature_deci_c;
} SensorApp_SampleType;

typedef SensorApp_ProviderStatusType (*SensorApp_ProviderInitType)(void *context);
typedef SensorApp_ProviderStatusType (*SensorApp_ProviderReadType)(void *context, SensorApp_SampleType *sample);

typedef struct
{
    uint8_t source;
    SensorApp_ProviderInitType init;
    SensorApp_ProviderReadType read;
    void *context;
} SensorApp_ProviderType;

typedef struct
{
    uint8_t source;
    uint8_t health;
    int16_t acceleration_x_mg;
    int16_t acceleration_y_mg;
    int16_t acceleration_z_mg;
    int16_t temperature_deci_c;
    uint32_t sample_count;
    uint8_t fault_flags;
} SensorApp_SnapshotType;

void SensorApp_Init(void);
void SensorApp_SetProvider(const SensorApp_ProviderType *provider);
void SensorApp_MainFunction(uint32_t now_ms);
void SensorApp_InjectMockCommunicationFault(void);
void SensorApp_RestoreMockNormal(void);
void SensorApp_GetSnapshot(SensorApp_SnapshotType *snapshot);

#endif
