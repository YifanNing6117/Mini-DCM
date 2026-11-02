#include "mpu6050.h"

#define MPU6050_ACCEL_LSB_PER_G           16384L
#define MPU6050_TEMPERATURE_OFFSET_DECI_C 365

static int16_t Mpu6050_ReadS16Be(const uint8_t *data)
{
    return (int16_t)(((uint16_t)data[0] << 8) | data[1]);
}

void Mpu6050_DeviceInit(Mpu6050_DeviceType *device, const Mpu6050_BusType *bus)
{
    if ((device == 0) || (bus == 0))
    {
        return;
    }

    device->bus = *bus;
    device->who_am_i = 0u;
    device->initialized = MINIDCM_FALSE;
}

Mpu6050_StatusType Mpu6050_Start(Mpu6050_DeviceType *device)
{
    uint8_t who_am_i = 0u;

    if ((device == 0) || (device->bus.write_byte == 0) || (device->bus.read_block == 0))
    {
        return MPU6050_ERR_PARAM;
    }

    if (device->bus.read_block(device->bus.context, MPU6050_REG_WHO_AM_I, &who_am_i, 1u) == MINIDCM_FALSE)
    {
        return MPU6050_ERR_BUS;
    }
    device->who_am_i = who_am_i;
    if (who_am_i != MPU6050_WHO_AM_I_EXPECTED)
    {
        return MPU6050_ERR_DEVICE_ID;
    }

    /* Wake the part and select the ±2 g range (16384 LSB/g). */
    if ((device->bus.write_byte(device->bus.context, MPU6050_REG_PWR_MGMT_1, 0x00u) == MINIDCM_FALSE) ||
        (device->bus.write_byte(device->bus.context, MPU6050_REG_ACCEL_CONFIG, 0x00u) == MINIDCM_FALSE))
    {
        return MPU6050_ERR_BUS;
    }

    device->initialized = MINIDCM_TRUE;
    return MPU6050_OK;
}

Mpu6050_StatusType Mpu6050_ReadRaw(Mpu6050_DeviceType *device, Mpu6050_RawSampleType *sample)
{
    uint8_t raw[14] = {0};

    if ((device == 0) || (sample == 0) || (device->bus.read_block == 0))
    {
        return MPU6050_ERR_PARAM;
    }
    if (device->initialized == MINIDCM_FALSE)
    {
        return MPU6050_ERR_NOT_INITIALIZED;
    }
    if (device->bus.read_block(device->bus.context, MPU6050_REG_ACCEL_XOUT_H, raw, sizeof(raw)) == MINIDCM_FALSE)
    {
        return MPU6050_ERR_BUS;
    }

    sample->acceleration_x_raw = Mpu6050_ReadS16Be(&raw[0]);
    sample->acceleration_y_raw = Mpu6050_ReadS16Be(&raw[2]);
    sample->acceleration_z_raw = Mpu6050_ReadS16Be(&raw[4]);
    sample->temperature_raw = Mpu6050_ReadS16Be(&raw[6]);
    return MPU6050_OK;
}

int16_t Mpu6050_ConvertAccelRawToMg(int16_t raw)
{
    return (int16_t)(((int32_t)raw * 1000L) / MPU6050_ACCEL_LSB_PER_G);
}

int16_t Mpu6050_ConvertTemperatureRawToDeciC(int16_t raw)
{
    /* Datasheet: temperature in °C = raw / 340 + 36.53. */
    return (int16_t)(((int32_t)raw * 10L) / 340L + MPU6050_TEMPERATURE_OFFSET_DECI_C);
}
