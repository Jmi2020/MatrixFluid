#include "alerts.h"

#include <string.h>
#include <stdio.h>
#include <time.h>
#include <strings.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_http_client.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#define ALERTS_NVS_NAMESPACE      "alerts"
#define ALERTS_NVS_KEY_CONFIG     "config"
#define ALERTS_TASK_STACK         4096
#define ALERTS_TASK_PRIORITY      4
#define ALERTS_MIN_DELAY_MS       60000ULL
#define ALERTS_WEBHOOK_TIMEOUT_MS 8000

static const char *TAG = "alerts";

typedef struct {
    bool sending;
    bool last_success;
    bool last_attempt_failed;
    time_t last_attempt;
    time_t last_success_time;
    time_t next_run;
    char last_error[96];
} alert_runtime_t;

static alert_config_t s_config;
static alert_runtime_t s_runtime;
static alert_snapshot_t s_snapshot;
static bool s_snapshot_valid = false;
static SemaphoreHandle_t s_mutex = NULL;
static TaskHandle_t s_scheduler_task = NULL;
static bool s_initialised = false;

static void alerts_scheduler_task(void *arg);
static void alerts_notify_scheduler(void);
static void alerts_load_config(alert_config_t *cfg);
static void alerts_save_config(const alert_config_t *cfg);
static time_t alerts_compute_next_run(time_t now_utc, const alert_config_t *cfg);
static bool alerts_time_ready(void);
static esp_err_t alerts_dispatch(const alert_config_t *cfg, const alert_snapshot_t *snapshot);
static void alerts_finalize_send(bool success, const char *error_message);
static uint8_t alerts_default_bitmap(alert_frequency_t frequency);

static void alerts_lock(void) {
    if (s_mutex) {
        xSemaphoreTake(s_mutex, portMAX_DELAY);
    }
}

static void alerts_unlock(void) {
    if (s_mutex) {
        xSemaphoreGive(s_mutex);
    }
}

void alerts_init(void) {
    if (s_initialised) {
        return;
    }

    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) {
        ESP_LOGE(TAG, "Failed to create alert mutex");
        return;
    }

    memset(&s_config, 0, sizeof(s_config));
    memset(&s_runtime, 0, sizeof(s_runtime));
    memset(&s_snapshot, 0, sizeof(s_snapshot));

    alerts_load_config(&s_config);

    s_runtime.next_run = 0;
    s_runtime.last_error[0] = '\0';

    if (xTaskCreate(alerts_scheduler_task, "alerts_sched", ALERTS_TASK_STACK, NULL,
                    ALERTS_TASK_PRIORITY, &s_scheduler_task) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create alerts scheduler task");
        s_scheduler_task = NULL;
        return;
    }

    s_initialised = true;
}

void alerts_get_config(alert_config_t *config_out) {
    if (!config_out) {
        return;
    }
    alerts_lock();
    *config_out = s_config;
    alerts_unlock();
}

bool alerts_apply_config(const alert_config_t *config) {
    if (!config) {
        return false;
    }

    alert_config_t new_cfg = *config;

    if (new_cfg.hour >= 24 || new_cfg.minute >= 60) {
        ESP_LOGW(TAG, "Invalid alert time %02u:%02u", new_cfg.hour, new_cfg.minute);
        return false;
    }

    if (new_cfg.tz_offset_minutes < -720) {
        new_cfg.tz_offset_minutes = -720;
    } else if (new_cfg.tz_offset_minutes > 840) {
        new_cfg.tz_offset_minutes = 840;
    }

    if (new_cfg.frequency == ALERT_FREQUENCY_WEEKLY && new_cfg.days_bitmap == 0) {
        new_cfg.days_bitmap = alerts_default_bitmap(new_cfg.frequency);
    }

    alerts_lock();
    bool changed = memcmp(&s_config, &new_cfg, sizeof(alert_config_t)) != 0;
    s_config = new_cfg;
    alerts_unlock();

    alerts_save_config(&s_config);

    if (changed) {
        alerts_notify_scheduler();
    }

    return true;
}

void alerts_update_snapshot(const alert_snapshot_t *snapshot) {
    if (!snapshot) {
        return;
    }

    alerts_lock();
    s_snapshot = *snapshot;
    s_snapshot_valid = true;
    alerts_unlock();
}

void alerts_get_portal_status(alert_portal_status_t *status_out) {
    if (!status_out) {
        return;
    }

    memset(status_out, 0, sizeof(*status_out));

    alerts_lock();
    status_out->enabled = s_config.enabled;
    status_out->frequency = (uint8_t)s_config.frequency;
    status_out->days_bitmap = s_config.days_bitmap;
    status_out->hour = s_config.hour;
    status_out->minute = s_config.minute;
    status_out->tz_offset_minutes = s_config.tz_offset_minutes;
    status_out->last_success = s_runtime.last_success;
    status_out->last_attempt_failed = s_runtime.last_attempt_failed;
    status_out->last_attempt_epoch = (uint32_t)s_runtime.last_attempt;
    status_out->last_success_epoch = (uint32_t)s_runtime.last_success_time;
    status_out->next_run_epoch = (uint32_t)s_runtime.next_run;
    strncpy(status_out->webhook_url, s_config.webhook_url, sizeof(status_out->webhook_url) - 1);
    strncpy(status_out->auth_header, s_config.auth_header, sizeof(status_out->auth_header) - 1);
    strncpy(status_out->recipient, s_config.recipient, sizeof(status_out->recipient) - 1);
    strncpy(status_out->last_error, s_runtime.last_error, sizeof(status_out->last_error) - 1);
    alerts_unlock();
}

static uint8_t alerts_default_bitmap(alert_frequency_t frequency) {
    switch (frequency) {
        case ALERT_FREQUENCY_WEEKDAYS:
            return 0x3E; // Monday-Friday
        case ALERT_FREQUENCY_WEEKLY:
        case ALERT_FREQUENCY_DAILY:
        default:
            return 0x7F; // All days
    }
}

static void alerts_load_config(alert_config_t *cfg) {
    alert_config_t default_cfg = {
        .enabled = false,
        .frequency = ALERT_FREQUENCY_DAILY,
        .days_bitmap = alerts_default_bitmap(ALERT_FREQUENCY_DAILY),
        .hour = 6,
        .minute = 0,
        .tz_offset_minutes = 0,
        .webhook_url = "",
        .auth_header = "",
        .recipient = ""
    };

    if (!cfg) {
        return;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(ALERTS_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Unable to open NVS for alerts: %s", esp_err_to_name(err));
        *cfg = default_cfg;
        return;
    }

    size_t required = sizeof(alert_config_t);
    err = nvs_get_blob(handle, ALERTS_NVS_KEY_CONFIG, cfg, &required);
    if (err == ESP_ERR_NVS_NOT_FOUND || required != sizeof(alert_config_t)) {
        ESP_LOGI(TAG, "No existing alert config; using defaults");
        *cfg = default_cfg;
        nvs_set_blob(handle, ALERTS_NVS_KEY_CONFIG, cfg, sizeof(alert_config_t));
        nvs_commit(handle);
    } else if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed reading alert config: %s", esp_err_to_name(err));
        *cfg = default_cfg;
    }

    nvs_close(handle);
}

static void alerts_save_config(const alert_config_t *cfg) {
    if (!cfg) {
        return;
    }
    nvs_handle_t handle;
    esp_err_t err = nvs_open(ALERTS_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open alerts namespace: %s", esp_err_to_name(err));
        return;
    }
    err = nvs_set_blob(handle, ALERTS_NVS_KEY_CONFIG, cfg, sizeof(alert_config_t));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save alerts config: %s", esp_err_to_name(err));
    }
    nvs_close(handle);
}

static bool alerts_time_ready(void) {
    time_t now = 0;
    time(&now);
    return now > 1000;
}

static time_t alerts_compute_next_run(time_t now_utc, const alert_config_t *cfg) {
    if (!cfg || !cfg->enabled) {
        return 0;
    }

    uint8_t mask = cfg->days_bitmap;
    if (mask == 0) {
        mask = alerts_default_bitmap(cfg->frequency);
    }

    const int target_minutes = cfg->hour * 60 + cfg->minute;
    time_t now_local = now_utc + cfg->tz_offset_minutes * 60;

    struct tm tm_now;
    gmtime_r(&now_local, &tm_now);

    time_t midnight_local = now_local - (tm_now.tm_hour * 3600 + tm_now.tm_min * 60 + tm_now.tm_sec);

    for (int offset = 0; offset < 7; ++offset) {
        int day_index = (tm_now.tm_wday + offset) % 7;
        bool allowed = false;
        switch (cfg->frequency) {
            case ALERT_FREQUENCY_WEEKDAYS:
                allowed = (day_index >= 1 && day_index <= 5);
                break;
            case ALERT_FREQUENCY_WEEKLY:
                allowed = (mask & (1 << day_index)) != 0;
                break;
            case ALERT_FREQUENCY_DAILY:
            default:
                allowed = true;
                break;
        }

        if (!allowed) {
            continue;
        }

        time_t candidate_local = midnight_local + offset * 86400 + target_minutes * 60;
        if (candidate_local <= now_local + 60) {
            continue;
        }

        return candidate_local - cfg->tz_offset_minutes * 60;
    }

    time_t fallback_local = midnight_local + 7 * 86400 + target_minutes * 60;
    return fallback_local - cfg->tz_offset_minutes * 60;
}

static void alerts_scheduler_task(void *arg) {
    (void)arg;

    while (1) {
        alert_config_t cfg;
        alerts_get_config(&cfg);

        if (!cfg.enabled) {
            alerts_lock();
            s_runtime.next_run = 0;
            alerts_unlock();
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            continue;
        }

        if (!alerts_time_ready()) {
            ESP_LOGW(TAG, "Waiting for time sync before scheduling alerts");
            if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(ALERTS_MIN_DELAY_MS))) {
                continue;
            }
            continue;
        }

        time_t now_utc;
        time(&now_utc);
        time_t next_run = alerts_compute_next_run(now_utc, &cfg);

        alerts_lock();
        s_runtime.next_run = next_run;
        alerts_unlock();

        if (next_run == 0) {
            if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(ALERTS_MIN_DELAY_MS))) {
                continue;
            }
            continue;
        }

        int64_t diff_ms = (int64_t)(next_run - now_utc) * 1000LL;
        if (diff_ms < (int64_t)ALERTS_MIN_DELAY_MS) {
            diff_ms = ALERTS_MIN_DELAY_MS;
        }

        if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(diff_ms))) {
            continue;
        }

        alerts_lock();
        alert_snapshot_t snapshot = s_snapshot;
        bool snapshot_ok = s_snapshot_valid;
        alerts_unlock();

        if (!alerts_time_ready()) {
            continue;
        }

        alerts_lock();
        s_runtime.last_attempt = time(NULL);
        s_runtime.sending = true;
        s_runtime.last_attempt_failed = false;
        s_runtime.last_error[0] = '\0';
        alerts_unlock();

        if (!snapshot_ok) {
            ESP_LOGW(TAG, "Skipping alert: snapshot not yet available");
            alerts_finalize_send(false, "Snapshot unavailable");
            continue;
        }

        esp_err_t err = alerts_dispatch(&cfg, &snapshot);
        if (err != ESP_OK) {
            char err_msg[80];
            snprintf(err_msg, sizeof(err_msg), "%s (%d)", esp_err_to_name(err), err);
            alerts_finalize_send(false, err_msg);
        } else {
            alerts_finalize_send(true, NULL);
        }
    }
}

static void alerts_finalize_send(bool success, const char *error_message) {
    alerts_lock();
    s_runtime.sending = false;
    s_runtime.last_attempt_failed = !success;
    if (success) {
        s_runtime.last_success = true;
        s_runtime.last_success_time = s_runtime.last_attempt;
        s_runtime.last_error[0] = '\0';
    } else {
        if (error_message && *error_message) {
            strncpy(s_runtime.last_error, error_message, sizeof(s_runtime.last_error) - 1);
            s_runtime.last_error[sizeof(s_runtime.last_error) - 1] = '\0';
        }
    }
    time_t now_utc;
    time(&now_utc);
    s_runtime.next_run = alerts_compute_next_run(now_utc, &s_config);
    alerts_unlock();
}

static void alerts_notify_scheduler(void) {
    if (s_scheduler_task) {
        xTaskNotifyGive(s_scheduler_task);
    }
}

static esp_err_t alerts_dispatch(const alert_config_t *cfg, const alert_snapshot_t *snapshot) {
    if (!cfg || !snapshot) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!cfg->webhook_url[0]) {
        ESP_LOGW(TAG, "Alert webhook URL not configured");
        return ESP_ERR_INVALID_STATE;
    }

    const char *level_str = fluid_level_to_string(snapshot->fluid_level);
    char payload[512];
    snprintf(payload, sizeof(payload),
             "{\"recipient\":\"%s\",\"fluid_level\":\"%s\",\"full_sensor_submerged\":%s,"
             "\"half_sensor_submerged\":%s,\"low_sensor_submerged\":%s,\"empty_sensor_submerged\":%s,"
             "\"full_signal_high\":%s,\"half_signal_high\":%s,\"low_signal_high\":%s,\"empty_signal_high\":%s,"
             "\"uptime_seconds\":%lu,\"power_source\":\"%s\"}",
             cfg->recipient,
             level_str ? level_str : "UNKNOWN",
             snapshot->full_submerged ? "true" : "false",
             snapshot->half_submerged ? "true" : "false",
             snapshot->low_submerged ? "true" : "false",
             snapshot->empty_submerged ? "true" : "false",
             snapshot->full_signal_high ? "true" : "false",
             snapshot->half_signal_high ? "true" : "false",
             snapshot->low_signal_high ? "true" : "false",
             snapshot->empty_signal_high ? "true" : "false",
             (unsigned long)snapshot->uptime_seconds,
             snapshot->power_source);

    esp_http_client_config_t http_cfg = {
        .url = cfg->webhook_url,
        .timeout_ms = ALERTS_WEBHOOK_TIMEOUT_MS,
        .method = HTTP_METHOD_POST,
        .transport_type = HTTP_TRANSPORT_OVER_SSL,
        .skip_cert_common_name_check = true,
    };

    if (strncasecmp(cfg->webhook_url, "http://", 7) == 0) {
        http_cfg.transport_type = HTTP_TRANSPORT_OVER_TCP;
    }

    esp_http_client_handle_t client = esp_http_client_init(&http_cfg);
    if (!client) {
        return ESP_ERR_NO_MEM;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");
    if (cfg->auth_header[0]) {
        esp_http_client_set_header(client, "Authorization", cfg->auth_header);
    }

    esp_http_client_set_post_field(client, payload, strlen(payload));

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        if (status < 200 || status >= 300) {
            ESP_LOGW(TAG, "Alert webhook returned HTTP %d", status);
            err = ESP_FAIL;
        }
    }

    esp_http_client_cleanup(client);
    return err;
}
