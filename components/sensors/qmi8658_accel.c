/**
 * @file qmi8658_accel.c
 * @brief QMI8658 Accelerometer Implementation
 */

#include "qmi8658_accel.h"
#include "esp_log.h"
#include "esp_check.h"
#include "driver/i2c.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>

static const char *TAG = "qmi8658";

// QMI8658 Register definitions
#define QMI8658_REG_WHO_AM_I      0x00
#define QMI8658_REG_REVISION      0x01
#define QMI8658_REG_CTRL1         0x02
#define QMI8658_REG_CTRL2         0x03
#define QMI8658_REG_CTRL3         0x04
#define QMI8658_REG_CTRL4         0x05
#define QMI8658_REG_CTRL5         0x06
#define QMI8658_REG_CTRL6         0x07
#define QMI8658_REG_CTRL7         0x08
#define QMI8658_REG_CTRL8         0x09
#define QMI8658_REG_CTRL9         0x0A
#define QMI8658_REG_STATUS0       0x2D
#define QMI8658_REG_STATUS1       0x2E
#define QMI8658_REG_TEMP_L        0x33
#define QMI8658_REG_TEMP_H        0x34
#define QMI8658_REG_AX_L          0x35
#define QMI8658_REG_AX_H          0x36
#define QMI8658_REG_AY_L          0x37
#define QMI8658_REG_AY_H          0x38
#define QMI8658_REG_AZ_L          0x39
#define QMI8658_REG_AZ_H          0x3A

// Register values
#define QMI8658_WHO_AM_I_VALUE    0x05
#define QMI8658_CTRL1_ACCEL_EN    0x01
#define QMI8658_CTRL1_GYRO_EN     0x02
#define QMI8658_CTRL2_ACCEL_FS_2G 0x00
#define QMI8658_CTRL2_ACCEL_FS_4G 0x01
#define QMI8658_CTRL2_ACCEL_FS_8G 0x02
#define QMI8658_CTRL2_ACCEL_FS_16G 0x03
#define QMI8658_CTRL2_ACCEL_ODR_8KHZ   (0x00 << 4)
#define QMI8658_CTRL2_ACCEL_ODR_4KHZ   (0x01 << 4)
#define QMI8658_CTRL2_ACCEL_ODR_2KHZ   (0x02 << 4)
#define QMI8658_CTRL2_ACCEL_ODR_1KHZ   (0x03 << 4)
#define QMI8658_CTRL2_ACCEL_ODR_500HZ  (0x04 << 4)
#define QMI8658_CTRL2_ACCEL_ODR_250HZ  (0x05 << 4)
#define QMI8658_CTRL2_ACCEL_ODR_125HZ  (0x06 << 4)
#define QMI8658_CTRL2_ACCEL_ODR_62_5HZ (0x07 << 4)

#define I2C_MASTER_NUM           I2C_NUM_0
#define I2C_MASTER_TIMEOUT_MS    1000

// Module state
static struct {
    qmi8658_config_t config;
    float accel_scale_factor;
    bool initialized;
    bool connection_failed;        // Track connection failures
    uint32_t error_count;         // Count consecutive errors
    uint32_t last_error_time;     // Last error timestamp

    // Interrupt handling
    accel_interrupt_callback_t interrupt_callback;
    void *interrupt_ctx;
    bool tap_interrupt_enabled;

    // Tap detection parameters
    float tap_threshold_g;
    uint16_t tap_window_ms;
    uint8_t tap_count_target;
} qmi8658_state = {0};

// Error handling constants
#define MAX_CONSECUTIVE_ERRORS    5
#define ERROR_COOLDOWN_MS        5000

/**
 * @brief Update error state for graceful degradation
 */
static void update_error_state(esp_err_t error) {
    uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;

    if (error != ESP_OK) {
        qmi8658_state.error_count++;
        qmi8658_state.last_error_time = current_time;

        if (qmi8658_state.error_count >= MAX_CONSECUTIVE_ERRORS) {
            if (!qmi8658_state.connection_failed) {
                ESP_LOGW(TAG, "Accelerometer connection failed after %lu errors - entering graceful degradation mode",
                         qmi8658_state.error_count);
                qmi8658_state.connection_failed = true;
            }
        }
    } else {
        // Reset error state on successful operation
        if (qmi8658_state.error_count > 0) {
            ESP_LOGI(TAG, "Accelerometer recovered after %lu errors", qmi8658_state.error_count);
        }
        qmi8658_state.error_count = 0;
        qmi8658_state.connection_failed = false;
    }
}

/**
 * @brief Check if we should attempt operations (avoid error spam)
 */
static bool should_attempt_operation(void) {
    if (!qmi8658_state.connection_failed) {
        return true;  // Normal operation
    }

    uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
    return (current_time - qmi8658_state.last_error_time) > ERROR_COOLDOWN_MS;
}

/**
 * @brief Write register via I2C with error handling
 */
static esp_err_t qmi8658_write_reg(uint8_t reg_addr, uint8_t data) {
    if (!should_attempt_operation()) {
        return ESP_ERR_INVALID_STATE;  // Quiet failure during cooldown
    }

    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (QMI8658_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg_addr, true);
    i2c_master_write_byte(cmd, data, true);
    i2c_master_stop(cmd);

    esp_err_t ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, pdMS_TO_TICKS(I2C_MASTER_TIMEOUT_MS));
    i2c_cmd_link_delete(cmd);

    update_error_state(ret);
    return ret;
}

/**
 * @brief Read register via I2C with error handling
 */
static esp_err_t qmi8658_read_reg(uint8_t reg_addr, uint8_t *data) {
    if (!should_attempt_operation()) {
        return ESP_ERR_INVALID_STATE;  // Quiet failure during cooldown
    }

    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (QMI8658_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg_addr, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (QMI8658_I2C_ADDR << 1) | I2C_MASTER_READ, true);
    i2c_master_read_byte(cmd, data, I2C_MASTER_NACK);
    i2c_master_stop(cmd);

    esp_err_t ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, pdMS_TO_TICKS(I2C_MASTER_TIMEOUT_MS));
    i2c_cmd_link_delete(cmd);

    update_error_state(ret);
    return ret;
}

/**
 * @brief Read multiple registers via I2C with error handling
 */
static esp_err_t qmi8658_read_regs(uint8_t reg_addr, uint8_t *data, size_t len) {
    if (!should_attempt_operation()) {
        return ESP_ERR_INVALID_STATE;  // Quiet failure during cooldown
    }

    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (QMI8658_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg_addr, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (QMI8658_I2C_ADDR << 1) | I2C_MASTER_READ, true);

    for (size_t i = 0; i < len - 1; i++) {
        i2c_master_read_byte(cmd, &data[i], I2C_MASTER_ACK);
    }
    i2c_master_read_byte(cmd, &data[len - 1], I2C_MASTER_NACK);
    i2c_master_stop(cmd);

    esp_err_t ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, pdMS_TO_TICKS(I2C_MASTER_TIMEOUT_MS));
    i2c_cmd_link_delete(cmd);

    update_error_state(ret);
    return ret;
}

/**
 * @brief Initialize I2C master
 */
static esp_err_t init_i2c_master(void) {
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = QMI8658_SDA_GPIO,
        .scl_io_num = QMI8658_SCL_GPIO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = QMI8658_I2C_FREQ_HZ,
    };

    ESP_RETURN_ON_ERROR(i2c_param_config(I2C_MASTER_NUM, &conf), TAG, "I2C config failed");
    ESP_RETURN_ON_ERROR(i2c_driver_install(I2C_MASTER_NUM, conf.mode, 0, 0, 0), TAG, "I2C install failed");

    return ESP_OK;
}

/**
 * @brief Convert ODR value to register setting
 */
static uint8_t odr_to_reg_value(uint16_t odr) {
    if (odr >= 4000) return QMI8658_CTRL2_ACCEL_ODR_4KHZ;
    if (odr >= 2000) return QMI8658_CTRL2_ACCEL_ODR_2KHZ;
    if (odr >= 1000) return QMI8658_CTRL2_ACCEL_ODR_1KHZ;
    if (odr >= 500)  return QMI8658_CTRL2_ACCEL_ODR_500HZ;
    if (odr >= 250)  return QMI8658_CTRL2_ACCEL_ODR_250HZ;
    if (odr >= 125)  return QMI8658_CTRL2_ACCEL_ODR_125HZ;
    return QMI8658_CTRL2_ACCEL_ODR_62_5HZ;
}

/**
 * @brief Convert scale value to register setting and scale factor
 */
static uint8_t scale_to_reg_value(uint8_t scale_g, float *scale_factor) {
    switch (scale_g) {
        case 2:  *scale_factor = 2.0f / 32768.0f;  return QMI8658_CTRL2_ACCEL_FS_2G;
        case 4:  *scale_factor = 4.0f / 32768.0f;  return QMI8658_CTRL2_ACCEL_FS_4G;
        case 8:  *scale_factor = 8.0f / 32768.0f;  return QMI8658_CTRL2_ACCEL_FS_8G;
        case 16: *scale_factor = 16.0f / 32768.0f; return QMI8658_CTRL2_ACCEL_FS_16G;
        default: *scale_factor = 2.0f / 32768.0f;  return QMI8658_CTRL2_ACCEL_FS_2G;
    }
}

esp_err_t qmi8658_init(const qmi8658_config_t *config) {
    esp_err_t ret = ESP_OK;
    uint8_t device_id;

    if (qmi8658_state.initialized) {
        ESP_LOGW(TAG, "QMI8658 already initialized");
        return ESP_OK;
    }

    // Setup default configuration
    if (config) {
        qmi8658_state.config = *config;
    } else {
        qmi8658_state.config.accel_scale = 2;    // ±2g
        qmi8658_state.config.accel_odr = 250;    // 250Hz
        qmi8658_state.config.enable_fifo = false;
        qmi8658_state.config.fifo_watermark = 32;
    }

    // Initialize I2C
    ESP_GOTO_ON_ERROR(init_i2c_master(), cleanup, TAG, "I2C initialization failed");

    // Check device ID
    ESP_GOTO_ON_ERROR(qmi8658_read_reg(QMI8658_REG_WHO_AM_I, &device_id), cleanup, TAG, "Failed to read device ID");

    if (device_id != QMI8658_WHO_AM_I_VALUE) {
        ESP_LOGE(TAG, "Invalid device ID: 0x%02X (expected 0x%02X)", device_id, QMI8658_WHO_AM_I_VALUE);
        ret = ESP_ERR_NOT_FOUND;
        goto cleanup;
    }

    // Configure accelerometer scale and ODR
    uint8_t scale_reg = scale_to_reg_value(qmi8658_state.config.accel_scale, &qmi8658_state.accel_scale_factor);
    uint8_t odr_reg = odr_to_reg_value(qmi8658_state.config.accel_odr);
    uint8_t ctrl2_val = scale_reg | odr_reg;

    ESP_GOTO_ON_ERROR(qmi8658_write_reg(QMI8658_REG_CTRL2, ctrl2_val), cleanup, TAG, "Failed to configure CTRL2");

    // Enable accelerometer
    ESP_GOTO_ON_ERROR(qmi8658_write_reg(QMI8658_REG_CTRL1, QMI8658_CTRL1_ACCEL_EN), cleanup, TAG, "Failed to enable accelerometer");

    // Initialize tap detection parameters
    qmi8658_state.tap_threshold_g = 1.5f;
    qmi8658_state.tap_window_ms = 500;
    qmi8658_state.tap_count_target = 3;
    qmi8658_state.tap_interrupt_enabled = false;
    qmi8658_state.interrupt_callback = NULL;
    qmi8658_state.interrupt_ctx = NULL;

    qmi8658_state.initialized = true;

    ESP_LOGI(TAG, "QMI8658 initialized successfully, scale=±%dg, ODR=%dHz",
             qmi8658_state.config.accel_scale, qmi8658_state.config.accel_odr);

    return ESP_OK;

cleanup:
    i2c_driver_delete(I2C_MASTER_NUM);
    return ret;
}

esp_err_t qmi8658_read_accel(accel_data_t *data) {
    ESP_RETURN_ON_FALSE(qmi8658_state.initialized, ESP_ERR_INVALID_STATE, TAG, "not initialized");
    ESP_RETURN_ON_FALSE(data, ESP_ERR_INVALID_ARG, TAG, "data is NULL");

    // If connection has failed, return default values (simulating no movement)
    if (qmi8658_state.connection_failed && !should_attempt_operation()) {
        data->x = 0.0f;
        data->y = 0.0f;
        data->z = 1.0f;  // Simulate gravity in Z axis
        data->timestamp_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
        return ESP_ERR_INVALID_STATE;  // Indicate degraded mode
    }

    uint8_t accel_data[6];
    esp_err_t ret = qmi8658_read_regs(QMI8658_REG_AX_L, accel_data, 6);

    if (ret != ESP_OK) {
        // Return safe default values on error instead of failing completely
        data->x = 0.0f;
        data->y = 0.0f;
        data->z = 1.0f;  // Simulate gravity
        data->timestamp_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;

        // Only log the first few errors to avoid spam
        if (qmi8658_state.error_count <= 3) {
            ESP_LOGE(TAG, "Failed to read acceleration data: %s", esp_err_to_name(ret));
        }
        return ret;
    }

    // Convert raw data to signed 16-bit values
    int16_t raw_x = (int16_t)((accel_data[1] << 8) | accel_data[0]);
    int16_t raw_y = (int16_t)((accel_data[3] << 8) | accel_data[2]);
    int16_t raw_z = (int16_t)((accel_data[5] << 8) | accel_data[4]);

    // Apply scale factor
    data->x = raw_x * qmi8658_state.accel_scale_factor;
    data->y = raw_y * qmi8658_state.accel_scale_factor;
    data->z = raw_z * qmi8658_state.accel_scale_factor;
    data->timestamp_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;

    return ESP_OK;
}

esp_err_t qmi8658_config_tap_detection(float threshold_g, uint16_t time_window_ms, uint8_t tap_count) {
    ESP_RETURN_ON_FALSE(qmi8658_state.initialized, ESP_ERR_INVALID_STATE, TAG, "not initialized");
    ESP_RETURN_ON_FALSE(threshold_g >= 0.5f && threshold_g <= 4.0f, ESP_ERR_INVALID_ARG, TAG, "invalid threshold");
    ESP_RETURN_ON_FALSE(time_window_ms >= 50 && time_window_ms <= 1000, ESP_ERR_INVALID_ARG, TAG, "invalid time window");
    ESP_RETURN_ON_FALSE(tap_count >= 1 && tap_count <= 4, ESP_ERR_INVALID_ARG, TAG, "invalid tap count");

    qmi8658_state.tap_threshold_g = threshold_g;
    qmi8658_state.tap_window_ms = time_window_ms;
    qmi8658_state.tap_count_target = tap_count;

    ESP_LOGI(TAG, "Tap detection configured: threshold=%.1fg, window=%dms, count=%d",
             threshold_g, time_window_ms, tap_count);

    return ESP_OK;
}

esp_err_t qmi8658_enable_tap_interrupt(bool enable, accel_interrupt_callback_t callback, void *user_ctx) {
    ESP_RETURN_ON_FALSE(qmi8658_state.initialized, ESP_ERR_INVALID_STATE, TAG, "not initialized");

    qmi8658_state.tap_interrupt_enabled = enable;
    qmi8658_state.interrupt_callback = callback;
    qmi8658_state.interrupt_ctx = user_ctx;

    if (enable) {
        ESP_RETURN_ON_FALSE(callback, ESP_ERR_INVALID_ARG, TAG, "callback is NULL");
        ESP_LOGI(TAG, "Tap interrupt enabled");
    } else {
        ESP_LOGI(TAG, "Tap interrupt disabled");
    }

    return ESP_OK;
}

float qmi8658_get_magnitude(const accel_data_t *data) {
    if (!data) return 0.0f;
    return sqrtf(data->x * data->x + data->y * data->y + data->z * data->z);
}

bool qmi8658_is_connected(void) {
    if (!qmi8658_state.initialized) {
        return false;
    }

    // If we're in failed state, avoid spamming the bus
    if (qmi8658_state.connection_failed && !should_attempt_operation()) {
        return false;
    }

    uint8_t device_id;
    esp_err_t ret = qmi8658_read_reg(QMI8658_REG_WHO_AM_I, &device_id);

    return (ret == ESP_OK && device_id == QMI8658_WHO_AM_I_VALUE);
}

esp_err_t qmi8658_get_device_id(uint8_t *device_id) {
    ESP_RETURN_ON_FALSE(qmi8658_state.initialized, ESP_ERR_INVALID_STATE, TAG, "not initialized");
    ESP_RETURN_ON_FALSE(device_id, ESP_ERR_INVALID_ARG, TAG, "device_id is NULL");

    return qmi8658_read_reg(QMI8658_REG_WHO_AM_I, device_id);
}

esp_err_t qmi8658_enable_accel(bool enable) {
    ESP_RETURN_ON_FALSE(qmi8658_state.initialized, ESP_ERR_INVALID_STATE, TAG, "not initialized");

    uint8_t ctrl1_val = enable ? QMI8658_CTRL1_ACCEL_EN : 0x00;
    ESP_RETURN_ON_ERROR(qmi8658_write_reg(QMI8658_REG_CTRL1, ctrl1_val), TAG, "Failed to %s accelerometer", enable ? "enable" : "disable");

    ESP_LOGI(TAG, "Accelerometer %s", enable ? "enabled" : "disabled");
    return ESP_OK;
}

esp_err_t qmi8658_self_test(void) {
    ESP_RETURN_ON_FALSE(qmi8658_state.initialized, ESP_ERR_INVALID_STATE, TAG, "not initialized");

    accel_data_t data;
    ESP_RETURN_ON_ERROR(qmi8658_read_accel(&data), TAG, "Self-test read failed");

    float magnitude = qmi8658_get_magnitude(&data);

    // Basic sanity check - should see approximately 1g due to gravity
    if (magnitude < 0.5f || magnitude > 2.0f) {
        ESP_LOGE(TAG, "Self-test failed: magnitude %.2fg (expected ~1g)", magnitude);
        return ESP_ERR_INVALID_RESPONSE;
    }

    ESP_LOGI(TAG, "Self-test passed: magnitude %.2fg", magnitude);
    return ESP_OK;
}

esp_err_t qmi8658_sleep(void) {
    ESP_RETURN_ON_FALSE(qmi8658_state.initialized, ESP_ERR_INVALID_STATE, TAG, "not initialized");

    // Disable accelerometer to enter low-power mode
    ESP_RETURN_ON_ERROR(qmi8658_write_reg(QMI8658_REG_CTRL1, 0x00), TAG, "Failed to enter sleep mode");

    ESP_LOGI(TAG, "Entered sleep mode");
    return ESP_OK;
}

esp_err_t qmi8658_wake(void) {
    ESP_RETURN_ON_FALSE(qmi8658_state.initialized, ESP_ERR_INVALID_STATE, TAG, "not initialized");

    // Re-enable accelerometer
    ESP_RETURN_ON_ERROR(qmi8658_write_reg(QMI8658_REG_CTRL1, QMI8658_CTRL1_ACCEL_EN), TAG, "Failed to wake from sleep");

    ESP_LOGI(TAG, "Woke from sleep mode");
    return ESP_OK;
}

esp_err_t qmi8658_deinit(void) {
    if (!qmi8658_state.initialized) {
        return ESP_OK;
    }

    // Disable accelerometer
    qmi8658_write_reg(QMI8658_REG_CTRL1, 0x00);

    // Deinitialize I2C
    i2c_driver_delete(I2C_MASTER_NUM);

    qmi8658_state.initialized = false;
    ESP_LOGI(TAG, "QMI8658 deinitialized");

    return ESP_OK;
}