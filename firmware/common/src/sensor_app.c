#include "sensor_app.h"

#include "dtc_mgr.h"
#include "minidcm_error.h"

static SensorApp_SnapshotType s_sensor_snapshot;
static uint8_t s_sensor_initialized;
static const SensorApp_ProviderType *s_sensor_provider;
static uint8_t s_provider_initialized;
static uint8_t s_manual_fault_active;

static int16_t SensorApp_GetMockWave(uint8_t phase, int16_t amplitude)
{
    int16_t value;

    if (phase < 10u)
    {
        value = (int16_t)((int16_t)phase * amplitude / 10);
    }
    else
    {
        value = (int16_t)((int16_t)(20u - phase) * amplitude / 10);
    }

    return (phase < 5u) ? value : (int16_t)(-value);
}

void SensorApp_Init(void)
{
    s_sensor_snapshot.source = (s_sensor_provider != 0) ? s_sensor_provider->source : SENSOR_APP_SOURCE_MOCK;
    s_sensor_snapshot.health = SENSOR_APP_HEALTH_OK;
    s_sensor_snapshot.acceleration_x_mg = 0;
    s_sensor_snapshot.acceleration_y_mg = 0;
    s_sensor_snapshot.acceleration_z_mg = 1000;
    s_sensor_snapshot.temperature_deci_c = 250;
    s_sensor_snapshot.sample_count = 0u;
    s_sensor_snapshot.fault_flags = SENSOR_APP_FAULT_NONE;
    s_provider_initialized = MINIDCM_FALSE;
    s_manual_fault_active = MINIDCM_FALSE;
    s_sensor_initialized = MINIDCM_TRUE;
}

void SensorApp_SetProvider(const SensorApp_ProviderType *provider)
{
    s_sensor_provider = provider;
    s_sensor_initialized = MINIDCM_FALSE;
}

static void SensorApp_SetProviderFault(SensorApp_ProviderStatusType status)
{
    uint8_t flags = SENSOR_APP_FAULT_COMMUNICATION;

    if (status == SENSOR_APP_PROVIDER_ERR_DEVICE_ID)
    {
        flags |= SENSOR_APP_FAULT_DEVICE_ID;
    }
    else if (status == SENSOR_APP_PROVIDER_ERR_NOT_READY)
    {
        flags |= SENSOR_APP_FAULT_INITIALIZATION;
    }

    if ((s_sensor_snapshot.fault_flags & SENSOR_APP_FAULT_COMMUNICATION) == 0u)
    {
        MiniDcm_Error_Report(MINIDCM_E_SENSOR_COMMUNICATION);
    }
    s_sensor_snapshot.fault_flags = flags;
    s_sensor_snapshot.health = SENSOR_APP_HEALTH_FAULT;
}

void SensorApp_MainFunction(uint32_t now_ms)
{
    uint8_t phase;
    SensorApp_SampleType sample;
    SensorApp_ProviderStatusType provider_status;

    if (s_sensor_initialized == MINIDCM_FALSE)
    {
        SensorApp_Init();
    }

    if (s_manual_fault_active != MINIDCM_FALSE)
    {
        return;
    }

    if (s_sensor_provider == 0)
    {
        phase = (uint8_t)((now_ms / 100u) % 20u);
        s_sensor_snapshot.acceleration_x_mg = SensorApp_GetMockWave(phase, 400);
        s_sensor_snapshot.acceleration_y_mg = SensorApp_GetMockWave((uint8_t)((phase + 5u) % 20u), 250);
        s_sensor_snapshot.acceleration_z_mg = (int16_t)(1000 + SensorApp_GetMockWave((uint8_t)((phase + 10u) % 20u), 80));
        s_sensor_snapshot.temperature_deci_c = (int16_t)(250 + (int16_t)phase);
        s_sensor_snapshot.sample_count++;
        return;
    }

    if ((s_provider_initialized == MINIDCM_FALSE) && (s_sensor_provider->init != 0))
    {
        provider_status = s_sensor_provider->init(s_sensor_provider->context);
        if (provider_status != SENSOR_APP_PROVIDER_OK)
        {
            SensorApp_SetProviderFault(provider_status);
            return;
        }
        s_provider_initialized = MINIDCM_TRUE;
    }

    if ((s_sensor_provider->read == 0) ||
        ((provider_status = s_sensor_provider->read(s_sensor_provider->context, &sample)) != SENSOR_APP_PROVIDER_OK))
    {
        SensorApp_SetProviderFault((s_sensor_provider->read == 0) ? SENSOR_APP_PROVIDER_ERR_NOT_READY : provider_status);
        s_provider_initialized = MINIDCM_FALSE;
        return;
    }

    s_sensor_snapshot.acceleration_x_mg = sample.acceleration_x_mg;
    s_sensor_snapshot.acceleration_y_mg = sample.acceleration_y_mg;
    s_sensor_snapshot.acceleration_z_mg = sample.acceleration_z_mg;
    s_sensor_snapshot.temperature_deci_c = sample.temperature_deci_c;
    s_sensor_snapshot.health = SENSOR_APP_HEALTH_OK;
    s_sensor_snapshot.fault_flags = SENSOR_APP_FAULT_NONE;
    s_sensor_snapshot.sample_count++;
}

void SensorApp_InjectMockCommunicationFault(void)
{
    if (s_sensor_initialized == MINIDCM_FALSE)
    {
        SensorApp_Init();
    }

    s_manual_fault_active = MINIDCM_TRUE;
    SensorApp_SetProviderFault(SENSOR_APP_PROVIDER_ERR_COMMUNICATION);
}

void SensorApp_RestoreMockNormal(void)
{
    if (s_sensor_initialized == MINIDCM_FALSE)
    {
        SensorApp_Init();
    }

    s_manual_fault_active = MINIDCM_FALSE;
    s_sensor_snapshot.fault_flags = SENSOR_APP_FAULT_NONE;
    s_sensor_snapshot.health = SENSOR_APP_HEALTH_OK;
    (void)DtcMgr_ClearByGroup(DTC_MGR_DTC_SENSOR_COMMUNICATION_ERROR);
}

void SensorApp_GetSnapshot(SensorApp_SnapshotType *snapshot)
{
    if (snapshot == 0)
    {
        return;
    }

    if (s_sensor_initialized == MINIDCM_FALSE)
    {
        SensorApp_Init();
    }

    *snapshot = s_sensor_snapshot;
}
