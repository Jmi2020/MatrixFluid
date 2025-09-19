/**
 * @file qmi8658_accel.h
 * @brief QMI8658 Accelerometer Driver for ESP32-S3
 *
 * I2C driver for QMI8658 6-axis IMU with focus on accelerometer
 * functionality for tap detection.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// I2C configuration
#define QMI8658_I2C_ADDR          0x6B    ///< QMI8658 I2C address
#define QMI8658_SDA_GPIO          8       ///< I2C SDA pin
#define QMI8658_SCL_GPIO          9       ///< I2C SCL pin
#define QMI8658_I2C_FREQ_HZ       400000  ///< I2C frequency (400kHz)

// Accelerometer configuration
#define QMI8658_ACCEL_SCALE_2G    2.0f    ///< ±2g full scale
#define QMI8658_ACCEL_ODR_250HZ   250     ///< 250Hz output data rate

/**
 * @brief Accelerometer data structure
 */
typedef struct {
    float x;                ///< X-axis acceleration (g)
    float y;                ///< Y-axis acceleration (g)
    float z;                ///< Z-axis acceleration (g)
    uint32_t timestamp_ms;  ///< Reading timestamp
} accel_data_t;

/**
 * @brief QMI8658 configuration
 */
typedef struct {
    uint8_t accel_scale;    ///< Accelerometer scale (2, 4, 8, 16g)
    uint16_t accel_odr;     ///< Output data rate (Hz)
    bool enable_fifo;       ///< Enable FIFO buffering
    uint8_t fifo_watermark; ///< FIFO watermark level
} qmi8658_config_t;

/**
 * @brief Accelerometer interrupt callback function type
 *
 * @param accel_data Current acceleration data
 * @param user_ctx User context data
 */
typedef void (*accel_interrupt_callback_t)(const accel_data_t *accel_data, void *user_ctx);

/**
 * @brief Initialize QMI8658 accelerometer
 *
 * @param config Configuration parameters (NULL for defaults)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t qmi8658_init(const qmi8658_config_t *config);

/**
 * @brief Read current accelerometer data
 *
 * @param data Pointer to store acceleration data
 * @return esp_err_t ESP_OK on success
 */
esp_err_t qmi8658_read_accel(accel_data_t *data);

/**
 * @brief Configure tap detection parameters
 *
 * @param threshold_g Tap threshold in g-force (0.5 - 4.0g)
 * @param time_window_ms Time window for tap detection (50-500ms)
 * @param tap_count Number of taps to detect (1-4)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t qmi8658_config_tap_detection(float threshold_g, uint16_t time_window_ms, uint8_t tap_count);

/**
 * @brief Enable/disable tap detection interrupt
 *
 * @param enable Enable tap detection
 * @param callback Interrupt callback function
 * @param user_ctx User context for callback
 * @return esp_err_t ESP_OK on success
 */
esp_err_t qmi8658_enable_tap_interrupt(bool enable, accel_interrupt_callback_t callback, void *user_ctx);

/**
 * @brief Get accelerometer magnitude (total acceleration)
 *
 * @param data Acceleration data
 * @return float Magnitude in g-force
 */
float qmi8658_get_magnitude(const accel_data_t *data);

/**
 * @brief Check if device is properly connected
 *
 * @return true Device is connected and responding
 * @return false Device not found or communication error
 */
bool qmi8658_is_connected(void);

/**
 * @brief Get device ID
 *
 * @param device_id Pointer to store device ID
 * @return esp_err_t ESP_OK on success
 */
esp_err_t qmi8658_get_device_id(uint8_t *device_id);

/**
 * @brief Enable/disable accelerometer
 *
 * @param enable Enable accelerometer
 * @return esp_err_t ESP_OK on success
 */
esp_err_t qmi8658_enable_accel(bool enable);

/**
 * @brief Perform accelerometer self-test
 *
 * @return esp_err_t ESP_OK on success
 */
esp_err_t qmi8658_self_test(void);

/**
 * @brief Put device in low-power mode
 *
 * @return esp_err_t ESP_OK on success
 */
esp_err_t qmi8658_sleep(void);

/**
 * @brief Wake device from low-power mode
 *
 * @return esp_err_t ESP_OK on success
 */
esp_err_t qmi8658_wake(void);

/**
 * @brief Deinitialize QMI8658 driver
 *
 * @return esp_err_t ESP_OK on success
 */
esp_err_t qmi8658_deinit(void);

#ifdef __cplusplus
}
#endif