#include "mpu6050_hal_port.h"

#include "main.h"
#include "mpu6050.h"
#include "sensor_app.h"

#define F103_MPU6050_I2C_TIMEOUT_MS        20u

/* MX_I2C1_Init() creates this global handle in the CubeIDE main.c file. */
extern I2C_HandleTypeDef hi2c1;

static Mpu6050_DeviceType s_mpu6050_device;

static MiniDcm_BoolType F103_Mpu6050_WriteByte(void *context, uint8_t reg, uint8_t value)
{
    I2C_HandleTypeDef *i2c = (I2C_HandleTypeDef *)context;

    return ((i2c != 0) &&
            (HAL_I2C_Mem_Write(i2c, (uint16_t)(MPU6050_I2C_ADDRESS_7BIT << 1), reg,
                               I2C_MEMADD_SIZE_8BIT, &value, 1u,
                               F103_MPU6050_I2C_TIMEOUT_MS) == HAL_OK))
               ? MINIDCM_TRUE
               : MINIDCM_FALSE;
}

static MiniDcm_BoolType F103_Mpu6050_ReadBlock(void *context, uint8_t start_reg, uint8_t *data, uint8_t len)
{
    I2C_HandleTypeDef *i2c = (I2C_HandleTypeDef *)context;

    return ((i2c != 0) && (data != 0) && (len > 0u) &&
            (HAL_I2C_Mem_Read(i2c, (uint16_t)(MPU6050_I2C_ADDRESS_7BIT << 1), start_reg,
                              I2C_MEMADD_SIZE_8BIT, data, len,
                              F103_MPU6050_I2C_TIMEOUT_MS) == HAL_OK))
               ? MINIDCM_TRUE
               : MINIDCM_FALSE;
}

static SensorApp_ProviderStatusType F103_Mpu6050_ProviderInit(void *context)
{
    Mpu6050_StatusType status;

    (void)context;
    status = Mpu6050_Start(&s_mpu6050_device);
    if (status == MPU6050_OK)
    {
        return SENSOR_APP_PROVIDER_OK;
    }
    if (status == MPU6050_ERR_DEVICE_ID)
    {
        return SENSOR_APP_PROVIDER_ERR_DEVICE_ID;
    }
    return SENSOR_APP_PROVIDER_ERR_COMMUNICATION;
}

static SensorApp_ProviderStatusType F103_Mpu6050_ProviderRead(void *context, SensorApp_SampleType *sample)
{
    Mpu6050_RawSampleType raw;

    (void)context;
    if ((sample == 0) || (Mpu6050_ReadRaw(&s_mpu6050_device, &raw) != MPU6050_OK))
    {
        return SENSOR_APP_PROVIDER_ERR_COMMUNICATION;
    }

    sample->acceleration_x_mg = Mpu6050_ConvertAccelRawToMg(raw.acceleration_x_raw);
    sample->acceleration_y_mg = Mpu6050_ConvertAccelRawToMg(raw.acceleration_y_raw);
    sample->acceleration_z_mg = Mpu6050_ConvertAccelRawToMg(raw.acceleration_z_raw);
    sample->temperature_deci_c = Mpu6050_ConvertTemperatureRawToDeciC(raw.temperature_raw);
    return SENSOR_APP_PROVIDER_OK;
}

void F103_Mpu6050_RegisterSensorApp(void)
{
    static const Mpu6050_BusType bus =
    {
        F103_Mpu6050_WriteByte,
        F103_Mpu6050_ReadBlock,
        &hi2c1
    };
    static const SensorApp_ProviderType provider =
    {
        SENSOR_APP_SOURCE_MPU6050,
        F103_Mpu6050_ProviderInit,
        F103_Mpu6050_ProviderRead,
        0
    };

    Mpu6050_DeviceInit(&s_mpu6050_device, &bus);
    SensorApp_SetProvider(&provider);
}
