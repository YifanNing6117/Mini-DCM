#ifndef MPU6050_H
#define MPU6050_H

#include "minidcm_types.h"

#define MPU6050_I2C_ADDRESS_7BIT          ((uint8_t)0x68u)
#define MPU6050_WHO_AM_I_EXPECTED         ((uint8_t)0x68u)

#define MPU6050_REG_ACCEL_XOUT_H          ((uint8_t)0x3Bu)
#define MPU6050_REG_PWR_MGMT_1            ((uint8_t)0x6Bu)
#define MPU6050_REG_ACCEL_CONFIG          ((uint8_t)0x1Cu)
#define MPU6050_REG_WHO_AM_I              ((uint8_t)0x75u)

typedef enum
{
    MPU6050_OK = 0u,
    MPU6050_ERR_PARAM,
    MPU6050_ERR_BUS,
    MPU6050_ERR_DEVICE_ID,
    MPU6050_ERR_NOT_INITIALIZED
} Mpu6050_StatusType;

typedef MiniDcm_BoolType (*Mpu6050_BusWriteByteType)(void *context, uint8_t reg, uint8_t value);
typedef MiniDcm_BoolType (*Mpu6050_BusReadBlockType)(void *context, uint8_t start_reg, uint8_t *data, uint8_t len);

typedef struct
{
    Mpu6050_BusWriteByteType write_byte;
    Mpu6050_BusReadBlockType read_block;
    void *context;
} Mpu6050_BusType;

typedef struct
{
    int16_t acceleration_x_raw;
    int16_t acceleration_y_raw;
    int16_t acceleration_z_raw;
    int16_t temperature_raw;
} Mpu6050_RawSampleType;

typedef struct
{
    Mpu6050_BusType bus;
    uint8_t who_am_i;
    MiniDcm_BoolType initialized;
} Mpu6050_DeviceType;

void Mpu6050_DeviceInit(Mpu6050_DeviceType *device, const Mpu6050_BusType *bus);
Mpu6050_StatusType Mpu6050_Start(Mpu6050_DeviceType *device);
Mpu6050_StatusType Mpu6050_ReadRaw(Mpu6050_DeviceType *device, Mpu6050_RawSampleType *sample);
int16_t Mpu6050_ConvertAccelRawToMg(int16_t raw);
int16_t Mpu6050_ConvertTemperatureRawToDeciC(int16_t raw);

#endif
