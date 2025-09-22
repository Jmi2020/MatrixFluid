/**
 * @file wifi_config.c
 * @brief WiFi Access Point for MatrixFluid Configuration Implementation
 */

#include <string.h>
#include <stdio.h>
#include <sys/param.h>
#include "wifi_config.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_check.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "cJSON.h"
#include "display_controller.h"

static const char *TAG = "wifi_config";

// WiFi and HTTP server handles
static esp_netif_t *wifi_netif = NULL;
static httpd_handle_t http_server = NULL;
static bool wifi_initialized = false;
static bool server_running = false;

// Configuration storage
static display_config_t current_display_config = {0};
static bool manual_trigger_pending = false;
static wifi_config_init_t init_config = {0};
static wifi_portal_status_t portal_status = {0};
static bool demo_mode_requested = false;

typedef struct {
    bool in_progress;
    bool pending_reboot;
    size_t bytes_written;
    size_t expected_size;
    esp_err_t last_error;
    esp_ota_handle_t handle;
    const esp_partition_t *partition;
} ota_state_t;

static ota_state_t ota_state = {0};

// Forward declarations for internal helpers
static void clamp_display_config(display_config_t *config);
static void sync_display_controller_config(void);
static void ota_reset_state(void);
static void schedule_reboot(void);

// Configuration limits
#define MIN_DISPLAY_INTERVAL_SECONDS  60U
#define MAX_DISPLAY_INTERVAL_SECONDS 3600U
#define MIN_DISPLAY_DURATION_SECONDS  1U
#define MAX_DISPLAY_DURATION_SECONDS 30U

// HTML content for the web interface
static const char* html_page =
"<!DOCTYPE html>"
"<html lang='en'><head><meta charset='utf-8'><title>MatrixFluid Portal</title>"
"<meta name='viewport' content='width=device-width, initial-scale=1'>"
"<style>"
"body{font-family:system-ui,Arial,sans-serif;margin:0;background:#eef1f6;color:#202124;}"
"header{background:#1b73e8;color:white;padding:24px 16px;text-align:center;}"
"main{max-width:720px;margin:0 auto;padding:24px;}"
"section{background:white;margin-bottom:18px;padding:20px;border-radius:12px;box-shadow:0 8px 24px rgba(32,33,36,0.1);}"
"h2{margin:0 0 16px;font-size:1.3rem;}"
".status-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(140px,1fr));gap:12px;}"
".status-card{background:#f6f9fe;border-radius:10px;padding:14px;text-align:center;}"
".status-card span{display:block;font-size:.82rem;color:#5f6368;}"
".status-value{font-size:1.2rem;font-weight:600;margin-top:6px;}"
".level-ok{color:#188038;}.level-warn{color:#fbbc04;}.level-crit{color:#d93025;}"
"label{display:block;margin:12px 0 6px;font-weight:600;}"
"input[type='number'],select{width:100%;padding:10px;border-radius:8px;border:1px solid #d2d6dc;font-size:1rem;box-sizing:border-box;}"
"input[type='range']{width:100%;}"
".btn{display:inline-block;margin-top:10px;padding:10px 18px;border:none;border-radius:8px;font-size:1rem;cursor:pointer;transition:background .2s;}"
".btn-primary{background:#1b73e8;color:white;}"
".btn-primary:hover{background:#175ab8;}"
".btn-success{background:#188038;color:white;}"
".btn-success:hover{background:#0f6b28;}"
".switch{display:flex;align-items:center;gap:10px;font-weight:600;}"
".badge{display:inline-flex;align-items:center;padding:4px 10px;border-radius:999px;font-size:.8rem;font-weight:600;margin-left:8px;}"
".badge-on{background:#e6f4ea;color:#188038;}"
".badge-off{background:#fce8e6;color:#d93025;}"
".help{font-size:.82rem;color:#5f6368;margin-top:6px;}"
"</style></head><body>"
"<header><h1>MatrixFluid Controller</h1><p>Access Point: <code>" WIFI_AP_SSID "</code> &bull; Password: <code>" WIFI_AP_PASSWORD "</code></p></header>"
"<main>"
"<section><h2>Live Status</h2><div class='status-grid'>"
"<div class='status-card'><span>Fluid Level</span><div id='fluid-level' class='status-value level-ok'>Loading...</div></div>"
"<div class='status-card'><span>Display</span><div id='display-state' class='status-value'>...</div></div>"
"<div class='status-card'><span>Power Source</span><div id='power-source' class='status-value'>--</div></div>"
"<div class='status-card'><span>Next Wake</span><div id='next-wake' class='status-value'>--</div></div>"
"<div class='status-card'><span>Manual Triggers</span><div id='manual-count' class='status-value'>0</div></div>"
"<div class='status-card'><span>Periodic Triggers</span><div id='periodic-count' class='status-value'>0</div></div>"
"<div class='status-card'><span>Uptime</span><div id='uptime' class='status-value'>--</div></div>"
"<div class='status-card'><span>Connected Clients</span><div id='client-count' class='status-value'>0</div></div>"
"<div class='status-card'><span>Half Sensor</span><div id='sensor-half' class='status-value'>--</div></div>"
"<div class='status-card'><span>Empty Sensor</span><div id='sensor-empty' class='status-value'>--</div></div>"
"</div></section>"
"<section><h2>Manual Actions</h2>"
"<button class='btn btn-success' onclick='triggerDisplay()'>Show Fluid Level Now</button>"
"<div class='switch'><input type='checkbox' id='demo-toggle' onchange='toggleDemo(this.checked)'>"
"<label for='demo-toggle' style='margin:0;'>Demo Mode<span id='demo-state' class='badge badge-off'>Inactive</span></label></div>"
"<p class='help'>Enable demo mode for kiosk demonstrations when the USB data link is connected. Disable for normal timed behaviour.</p>"
"<button class='btn btn-primary' type='button' onclick='requestLedStatus()'>LED Status Snapshot</button>"
"<p id='led-status' class='help'>LED status: Not requested.</p>"
"<button class='btn btn-primary' type='button' onclick='requestHealth()'>Device Health Snapshot</button>"
"<pre id='health-status' class='help' style='white-space:pre-wrap'>Device health not requested yet.</pre>"
"</section>"
"<section><h2>Automatic Display Settings</h2><form onsubmit='saveConfig(event)'>"
"<label for='periodic'>Enable periodic display</label>"
"<select id='periodic'><option value='true'>Enabled</option><option value='false'>Disabled</option></select>"
"<label for='interval'>Display interval (minutes)</label>"
"<input type='number' id='interval' min='1' max='60' value='15'>"
"<label for='duration'>Display duration (seconds)</label>"
"<input type='number' id='duration' min='1' max='30' value='7'>"
"<label for='brightness'>Brightness (1-5)</label>"
"<input type='range' id='brightness' min='1' max='5' value='3'><span id='brightness-val'>3</span>"
"<button type='submit' class='btn btn-primary'>Save Settings</button>"
"</form></section>"
"<section><h2>Pin Reference</h2>"
"<p><strong>GPIO2</strong> - Half-full sensor (drives 3.3V when closed)</p>"
"<p><strong>GPIO3</strong> - Near-empty sensor (drives 3.3V when closed)</p>"
"<p><strong>GPIO14</strong> - LED matrix data line</p>"
"<p><strong>5V / GND</strong> - Supply via buck converter (vehicle) or USB-C (demo)</p>"
"</section>"
"<section><h2>Firmware Update</h2>"
"<form onsubmit='uploadFirmware(event)'>"
"<label for='ota-file'>Select firmware (.bin)</label>"
"<input type='file' id='ota-file' accept='.bin'>"
"<button type='submit' class='btn btn-primary'>Upload &amp; Install</button>"
"</form>"
"<p id='ota-status' class='help'>No update in progress.</p>"
"</section></main>"
"<script>\n"
"const brightnessInput=document.getElementById('brightness');\n"
"brightnessInput.addEventListener('input',()=>{document.getElementById('brightness-val').textContent=brightnessInput.value;});\n"
"function formatLevel(level){const lower=(level||'UNKNOWN').toLowerCase();if(lower.includes('empty'))return 'NEAR EMPTY';return (level||'UNKNOWN').replace(/_/g,' ');}\n"
"function formatTime(seconds){if(seconds===null||seconds===undefined)return '--';if(seconds<60)return seconds+'s';const mins=Math.floor(seconds/60);const secs=seconds%60;return mins+'m '+secs+'s';}\n"
"function formatUptime(seconds){if(!seconds)return '--';const hrs=Math.floor(seconds/3600);const mins=Math.floor((seconds%3600)/60);return (hrs?hrs+'h ':'')+mins+'m';}\n"
"function formatPowerSource(source){if(!source)return '--';const normalized=String(source).toLowerCase();if(normalized.includes('usb'))return 'USB';if(normalized.includes('buck'))return '5V Buck';return source;}\n"
"function describeOta(data){if(!data)return'--';if(data.ota_pending_reboot)return'Ready to reboot';if(data.ota_in_progress){const pct=data.ota_total_size?(data.ota_bytes_written/data.ota_total_size*100).toFixed(1):'--';return`Uploading ${pct}%`; }if(data.ota_last_error&&data.ota_last_error!==0)return'Error '+data.ota_last_error;return'Idle';}\n"
"function describeSensor(submerged, signalHigh){if(signalHigh&&submerged)return'WET (HIGH)';if(!signalHigh&&!submerged)return'DRY (LOW)';if(signalHigh&&!submerged)return'Mixed (HIGH)';if(!signalHigh&&submerged)return'Mixed (LOW)';return'UNKNOWN';}\n"
"function sensorClass(submerged, signalHigh){if(signalHigh&&submerged)return'level-ok';if(!signalHigh&&!submerged)return'level-crit';return'level-warn';}\n"
"function loadStatus(){\n"
"  fetch('/api/status')\n"
"    .then(r=>r.json())\n"
"    .then(data=>{\n"
"      const level=data.fluid_level||'UNKNOWN';\n"
"      const displayLevel=data.displayed_fluid_level||level;\n"
"      const levelDiv=document.getElementById('fluid-level');\n"
"      const className=level==='ABOVE_HALF'?'level-ok':level==='BELOW_HALF'?'level-warn':'level-crit';\n"
"      levelDiv.className='status-value '+className;\n"
"      levelDiv.textContent=formatLevel(level);\n"
"      const displayState=document.getElementById('display-state');\n"
"      if(displayState){displayState.textContent=data.display_active?('ACTIVE - '+formatLevel(displayLevel)):('OFF - '+formatLevel(displayLevel));}\n"
"      const periodicEnabled=!!(data.config&&data.config.periodic_display_enabled);\n"
"      document.getElementById('next-wake').textContent=periodicEnabled?formatTime(data.next_wake_seconds):'Manual Only';\n"
"      document.getElementById('power-source').textContent=formatPowerSource(data.power_source);\n"
"      document.getElementById('manual-count').textContent=data.stats?data.stats.manual_trigger_count:0;\n"
"      document.getElementById('periodic-count').textContent=data.stats?data.stats.periodic_trigger_count:0;\n"
"      document.getElementById('uptime').textContent=formatUptime(data.uptime_seconds);\n"
"      document.getElementById('client-count').textContent=data.connected_clients||0;\n"
"      document.getElementById('periodic').value=String(periodicEnabled);\n"
"      document.getElementById('interval').value=Math.max(1,Math.round((data.config.display_interval_seconds||60)/60));\n"
"      document.getElementById('duration').value=data.config.display_duration_seconds||7;\n"
"      brightnessInput.value=data.config.display_brightness||3;\n"
"      document.getElementById('brightness-val').textContent=brightnessInput.value;\n"
"      const sensorHalf=document.getElementById('sensor-half');\n"
"      const sensorEmpty=document.getElementById('sensor-empty');\n"
"      if(sensorHalf){const text=describeSensor(!!data.half_sensor_submerged,!!data.half_sensor_signal_high);sensorHalf.textContent=text;sensorHalf.className='status-value '+sensorClass(!!data.half_sensor_submerged,!!data.half_sensor_signal_high);}\n"
"      if(sensorEmpty){const text=describeSensor(!!data.empty_sensor_submerged,!!data.empty_sensor_signal_high);sensorEmpty.textContent=text;sensorEmpty.className='status-value '+sensorClass(!!data.empty_sensor_submerged,!!data.empty_sensor_signal_high);}\n"
"      const demoToggle=document.getElementById('demo-toggle');\n"
"      if(demoToggle){\n"
"        const portalRequested=!!data.demo_mode_requested;\n"
"        const autoRequested=!!data.auto_demo_requested;\n"
"        const active=!!data.demo_mode_active;\n"
"        demoToggle.checked=portalRequested||active;\n"
"        const badge=document.getElementById('demo-state');\n"
"        const anyRequested=active||portalRequested||autoRequested;\n"
"        badge.className='badge '+(anyRequested?'badge-on':'badge-off');\n"
"        badge.textContent=active?'Running':portalRequested?'Requested':autoRequested?'USB Host':'Inactive';\n"
"      }\n"
"      const otaStatus=document.getElementById('ota-status');\n"
"      if(otaStatus){otaStatus.textContent=describeOta(data);}\n"
"    })\n"
"    .catch(e=>console.error('Status load failed:',e));\n"
"}\n"
"function triggerDisplay(){fetch('/api/trigger',{method:'POST'}).then(r=>r.text()).then(msg=>alert('Display triggered! '+msg)).catch(e=>alert('Trigger failed: '+e));}\n"
"function toggleDemo(enabled){fetch('/api/demo',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({enable:!!enabled})}).then(()=>setTimeout(loadStatus,400)).catch(e=>alert('Demo toggle failed: '+e));}\n"
"function saveConfig(event){event.preventDefault();const config={periodic_display_enabled:document.getElementById('periodic').value==='true',display_interval_seconds:Math.max(60,parseInt(document.getElementById('interval').value||15,10)*60),display_duration_seconds:Math.max(1,parseInt(document.getElementById('duration').value||7,10)),display_brightness:Math.min(5,Math.max(1,parseInt(brightnessInput.value||3,10)))};fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(config)}).then(r=>r.text()).then(msg=>alert('Settings saved! '+msg)).then(()=>setTimeout(loadStatus,400)).catch(e=>alert('Save failed: '+e));}\n"
"function uploadFirmware(event){event.preventDefault();const fileInput=document.getElementById('ota-file');if(!fileInput||!fileInput.files.length){alert('Select a firmware .bin file first.');return;}const file=fileInput.files[0];const statusEl=document.getElementById('ota-status');statusEl.textContent='Uploading firmware...';fetch('/api/ota',{method:'POST',headers:{'Content-Type':'application/octet-stream'},body:file}).then(r=>r.text()).then(msg=>{statusEl.textContent=msg||'Upload complete';setTimeout(loadStatus,1000);}).catch(e=>{statusEl.textContent='OTA failed: '+e;});}\n"
"function requestLedStatus(){fetch('/api/led').then(r=>r.json()).then(data=>{const statusEl=document.getElementById('led-status');if(statusEl){statusEl.textContent=`${data.active?'ACTIVE':'OFF'} - ${data.display_level} (brightness ${data.brightness}, mode ${data.mode}, last ${data.last_trigger})`;}}).catch(e=>alert('LED status failed: '+e));}\n"
"function requestHealth(){fetch('/api/health').then(r=>r.json()).then(data=>{const healthEl=document.getElementById('health-status');if(healthEl){healthEl.textContent=JSON.stringify(data,null,2);}}).catch(e=>alert('Health check failed: '+e));}\n"
"loadStatus();setInterval(loadStatus,5000);\n"
"</script></body></html>";


static void clamp_display_config(display_config_t *config) {
    if (!config) {
        return;
    }

    if (config->display_interval_seconds < MIN_DISPLAY_INTERVAL_SECONDS) {
        config->display_interval_seconds = MIN_DISPLAY_INTERVAL_SECONDS;
    } else if (config->display_interval_seconds > MAX_DISPLAY_INTERVAL_SECONDS) {
        config->display_interval_seconds = MAX_DISPLAY_INTERVAL_SECONDS;
    }

    if (config->display_duration_seconds < MIN_DISPLAY_DURATION_SECONDS) {
        config->display_duration_seconds = MIN_DISPLAY_DURATION_SECONDS;
    } else if (config->display_duration_seconds > MAX_DISPLAY_DURATION_SECONDS) {
        config->display_duration_seconds = MAX_DISPLAY_DURATION_SECONDS;
    }

    if (config->display_brightness < 1) {
        config->display_brightness = 1;
    } else if (config->display_brightness > 5) {
        config->display_brightness = 5;
    }
}

static void ota_reset_state(void) {
    ota_state.in_progress = false;
    ota_state.pending_reboot = false;
    ota_state.bytes_written = 0;
    ota_state.expected_size = 0;
    ota_state.last_error = ESP_OK;
    ota_state.handle = 0;
    ota_state.partition = NULL;
}

static void ota_reboot_task(void *arg) {
    vTaskDelay(pdMS_TO_TICKS(1200));
    esp_restart();
}

static void schedule_reboot(void) {
    xTaskCreate(ota_reboot_task, "ota_reboot", 2048, NULL, 5, NULL);
}

static void sync_display_controller_config(void) {
    clamp_display_config(&current_display_config);

    display_controller_config_t controller_config;
    esp_err_t ret = display_controller_get_config(&controller_config);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Display controller not ready; skipping sync");
        return;
    }

    if (current_display_config.periodic_display_enabled) {
        controller_config.mode = DISPLAY_MODE_PERIODIC;
        controller_config.periodic_interval_ms = current_display_config.display_interval_seconds * 1000U;
    } else {
        controller_config.mode = DISPLAY_MODE_MANUAL_ONLY;
    }

    controller_config.display_duration_ms = current_display_config.display_duration_seconds * 1000U;
    controller_config.brightness = current_display_config.display_brightness;

    ret = display_controller_set_config(&controller_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to sync display controller config: %s", esp_err_to_name(ret));
    }

    if (display_controller_is_active()) {
        display_controller_set_brightness(current_display_config.display_brightness);
    }

    if (!current_display_config.manual_trigger_enabled) {
        manual_trigger_pending = false;
    }
}

void wifi_config_update_status(const wifi_portal_status_t *status) {
    if (!status) {
        return;
    }
    portal_status = *status;
    portal_status.display_config = current_display_config;
    portal_status.demo_mode_requested = demo_mode_requested;
    portal_status.demo_mode_active = status->demo_mode_active;
    portal_status.auto_demo_requested = status->auto_demo_requested;
    portal_status.power_source = status->power_source;
    portal_status.half_sensor_submerged = status->half_sensor_submerged;
    portal_status.empty_sensor_submerged = status->empty_sensor_submerged;
    portal_status.half_sensor_signal_high = status->half_sensor_signal_high;
    portal_status.empty_sensor_signal_high = status->empty_sensor_signal_high;
    portal_status.ota_in_progress = ota_state.in_progress;
    portal_status.ota_bytes_written = (uint32_t)ota_state.bytes_written;
    portal_status.ota_total_size = (uint32_t)ota_state.expected_size;
    portal_status.ota_pending_reboot = ota_state.pending_reboot;
    portal_status.ota_last_error = ota_state.last_error;
}

// Forward declarations
static esp_err_t get_handler(httpd_req_t *req);
static cJSON *build_status_json(void);
static esp_err_t api_status_handler(httpd_req_t *req);
static esp_err_t api_config_handler(httpd_req_t *req);
static esp_err_t api_trigger_handler(httpd_req_t *req);
static esp_err_t api_demo_handler(httpd_req_t *req);
static esp_err_t api_ota_handler(httpd_req_t *req);
static esp_err_t api_led_status_handler(httpd_req_t *req);
static esp_err_t api_health_handler(httpd_req_t *req);

// HTTP URI handlers
static httpd_uri_t uri_get = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = get_handler,
    .user_ctx = NULL
};

static httpd_uri_t uri_api_status = {
    .uri = "/api/status",
    .method = HTTP_GET,
    .handler = api_status_handler,
    .user_ctx = NULL
};

static httpd_uri_t uri_api_config = {
    .uri = "/api/config",
    .method = HTTP_POST,
    .handler = api_config_handler,
    .user_ctx = NULL
};

static httpd_uri_t uri_api_trigger = {
    .uri = "/api/trigger",
    .method = HTTP_POST,
    .handler = api_trigger_handler,
    .user_ctx = NULL
};

static httpd_uri_t uri_api_demo = {
    .uri = "/api/demo",
    .method = HTTP_POST,
    .handler = api_demo_handler,
    .user_ctx = NULL
};

static httpd_uri_t uri_api_ota = {
    .uri = "/api/ota",
    .method = HTTP_POST,
    .handler = api_ota_handler,
    .user_ctx = NULL
};

static httpd_uri_t uri_api_led = {
    .uri = "/api/led",
    .method = HTTP_GET,
    .handler = api_led_status_handler,
    .user_ctx = NULL
};

static httpd_uri_t uri_api_health = {
    .uri = "/api/health",
    .method = HTTP_GET,
    .handler = api_health_handler,
    .user_ctx = NULL
};

/**
 * @brief WiFi event handler
 */
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data) {
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(TAG, "Client connected: " MACSTR, MAC2STR(event->mac));
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGI(TAG, "Client disconnected: " MACSTR, MAC2STR(event->mac));
    }
}

/**
 * @brief HTTP GET handler for main page
 */
static esp_err_t get_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, html_page, strlen(html_page));
    return ESP_OK;
}

/**
 * @brief API handler for status information
 */
static cJSON *build_status_json(void) {
    cJSON *json = cJSON_CreateObject();
    if (!json) {
        return NULL;
    }

    cJSON *config = cJSON_CreateObject();
    if (!config) {
        cJSON_Delete(json);
        return NULL;
    }

    display_config_t config_copy = current_display_config;
    clamp_display_config(&config_copy);

    cJSON_AddBoolToObject(config, "periodic_display_enabled", config_copy.periodic_display_enabled);
    cJSON_AddNumberToObject(config, "display_interval_seconds", config_copy.display_interval_seconds);
    cJSON_AddNumberToObject(config, "display_duration_seconds", config_copy.display_duration_seconds);
    cJSON_AddNumberToObject(config, "display_brightness", config_copy.display_brightness);
    cJSON_AddBoolToObject(config, "manual_trigger_enabled", config_copy.manual_trigger_enabled);
    cJSON_AddBoolToObject(config, "auto_brightness", config_copy.auto_brightness);
    cJSON_AddItemToObject(json, "config", config);

    const char *level_str = fluid_level_to_string(portal_status.fluid_level);
    cJSON_AddStringToObject(json, "fluid_level", level_str ? level_str : "UNKNOWN");
    const char *display_level_str = fluid_level_to_string(portal_status.displayed_fluid_level);
    cJSON_AddStringToObject(json, "displayed_fluid_level", display_level_str ? display_level_str : "UNKNOWN");
    cJSON_AddBoolToObject(json, "half_sensor_submerged", portal_status.half_sensor_submerged);
    cJSON_AddBoolToObject(json, "empty_sensor_submerged", portal_status.empty_sensor_submerged);
    cJSON_AddBoolToObject(json, "half_sensor_signal_high", portal_status.half_sensor_signal_high);
    cJSON_AddBoolToObject(json, "empty_sensor_signal_high", portal_status.empty_sensor_signal_high);
    cJSON_AddBoolToObject(json, "display_active", portal_status.display_active);
    cJSON_AddNumberToObject(json, "uptime_seconds", portal_status.uptime_seconds);
    cJSON_AddNumberToObject(json, "next_wake_seconds", portal_status.next_wake_seconds);
    cJSON_AddBoolToObject(json, "demo_mode_active", portal_status.demo_mode_active);
    cJSON_AddBoolToObject(json, "demo_mode_requested", portal_status.demo_mode_requested);
    cJSON_AddBoolToObject(json, "auto_demo_requested", portal_status.auto_demo_requested);
    cJSON_AddStringToObject(json, "power_source",
                            demo_mode_power_source_to_string(portal_status.power_source));
    cJSON_AddBoolToObject(json, "ota_in_progress", portal_status.ota_in_progress);
    cJSON_AddNumberToObject(json, "ota_bytes_written", portal_status.ota_bytes_written);
    cJSON_AddNumberToObject(json, "ota_total_size", portal_status.ota_total_size);
    cJSON_AddBoolToObject(json, "ota_pending_reboot", portal_status.ota_pending_reboot);
    cJSON_AddNumberToObject(json, "ota_last_error", portal_status.ota_last_error);

    cJSON *stats = cJSON_CreateObject();
    if (stats) {
        cJSON_AddNumberToObject(stats, "total_display_count", portal_status.total_display_count);
        cJSON_AddNumberToObject(stats, "manual_trigger_count", portal_status.manual_trigger_count);
        cJSON_AddNumberToObject(stats, "periodic_trigger_count", portal_status.periodic_trigger_count);
        cJSON_AddItemToObject(json, "stats", stats);
    }

    uint8_t clients = 0;
    char ap_ip[16] = "0.0.0.0";
    wifi_config_get_status(&clients, ap_ip);
    cJSON_AddNumberToObject(json, "connected_clients", clients);
    cJSON_AddStringToObject(json, "ap_ip", ap_ip);

    return json;
}

static esp_err_t api_status_handler(httpd_req_t *req) {
    cJSON *json = build_status_json();
    if (!json) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    char *json_string = cJSON_PrintUnformatted(json);
    if (!json_string) {
        cJSON_Delete(json);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_string, strlen(json_string));

    cJSON_free(json_string);
    cJSON_Delete(json);
    return ESP_OK;
}

/**
 * @brief API handler for configuration updates
 */
static esp_err_t api_config_handler(httpd_req_t *req) {
    char content[MAX_HTTP_REQUEST_SIZE];
    size_t recv_size = MIN(req->content_len, sizeof(content) - 1);

    int ret = httpd_req_recv(req, content, recv_size);
    if (ret <= 0) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    content[ret] = '\0';

    cJSON *json = cJSON_Parse(content);
    if (json == NULL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    // Update configuration
    cJSON *periodic = cJSON_GetObjectItem(json, "periodic_display_enabled");
    if (cJSON_IsBool(periodic)) {
        current_display_config.periodic_display_enabled = cJSON_IsTrue(periodic);
    }

    cJSON *interval = cJSON_GetObjectItem(json, "display_interval_seconds");
    if (cJSON_IsNumber(interval)) {
        current_display_config.display_interval_seconds = interval->valueint;
    }

    cJSON *duration = cJSON_GetObjectItem(json, "display_duration_seconds");
    if (cJSON_IsNumber(duration)) {
        current_display_config.display_duration_seconds = duration->valueint;
    }

    cJSON *brightness = cJSON_GetObjectItem(json, "display_brightness");
    if (cJSON_IsNumber(brightness)) {
        current_display_config.display_brightness = brightness->valueint;
        // Clamp to safe range
        if (current_display_config.display_brightness > 5) {
            current_display_config.display_brightness = 5;
        }
        if (current_display_config.display_brightness < 1) {
            current_display_config.display_brightness = 1;
        }
    }

    cJSON *manual = cJSON_GetObjectItem(json, "manual_trigger_enabled");
    if (cJSON_IsBool(manual)) {
        current_display_config.manual_trigger_enabled = cJSON_IsTrue(manual);
    }

    cJSON *auto_brightness = cJSON_GetObjectItem(json, "auto_brightness");
    if (cJSON_IsBool(auto_brightness)) {
        current_display_config.auto_brightness = cJSON_IsTrue(auto_brightness);
    }

    cJSON_Delete(json);

    clamp_display_config(&current_display_config);
    sync_display_controller_config();

    ESP_LOGI(TAG, "Configuration updated: periodic=%d, interval=%lus, duration=%lus, brightness=%d, manual=%d",
             current_display_config.periodic_display_enabled,
             (unsigned long)current_display_config.display_interval_seconds,
             (unsigned long)current_display_config.display_duration_seconds,
             current_display_config.display_brightness,
             current_display_config.manual_trigger_enabled);

    httpd_resp_send(req, "Configuration saved successfully", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/**
 * @brief API handler for manual display trigger
 */
static esp_err_t api_trigger_handler(httpd_req_t *req) {
    esp_err_t ret = wifi_config_trigger_display();
    if (ret == ESP_ERR_INVALID_STATE) {
        httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Manual trigger disabled");
        return ret;
    } else if (ret != ESP_OK) {
        httpd_resp_send_500(req);
        return ret;
    }

    ESP_LOGI(TAG, "Manual display trigger requested via web interface");
    httpd_resp_send(req, "Display triggered", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t api_demo_handler(httpd_req_t *req) {
    char content[128] = {0};
    size_t recv_size = MIN(req->content_len, sizeof(content) - 1);

    int ret = httpd_req_recv(req, content, recv_size);
    if (ret <= 0) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    content[ret] = '\0';

    cJSON *json = cJSON_Parse(content);
    if (!json) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON *enable = cJSON_GetObjectItem(json, "enable");
    if (!cJSON_IsBool(enable)) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing enable flag");
        return ESP_FAIL;
    }

    bool request = cJSON_IsTrue(enable);
    wifi_config_set_demo_mode_requested(request);
    cJSON_Delete(json);

    ESP_LOGI(TAG, "Demo mode request updated: %s", request ? "ENABLED" : "DISABLED");
    httpd_resp_send(req, request ? "Demo mode enabled" : "Demo mode disabled", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t api_ota_handler(httpd_req_t *req) {
    if (req->content_len <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty firmware payload");
        return ESP_ERR_INVALID_ARG;
    }

    if (ota_state.in_progress) {
        httpd_resp_set_status(req, "409 Conflict");
        httpd_resp_send(req, "OTA already in progress", HTTPD_RESP_USE_STRLEN);
        return ESP_ERR_INVALID_STATE;
    }

    ota_reset_state();
    ota_state.in_progress = true;
    ota_state.expected_size = req->content_len;
    ota_state.last_error = ESP_OK;

    ota_state.partition = esp_ota_get_next_update_partition(NULL);
    if (!ota_state.partition) {
        ota_state.last_error = ESP_ERR_NOT_FOUND;
        ota_state.in_progress = false;
        httpd_resp_send_500(req);
        return ESP_ERR_NOT_FOUND;
    }

    esp_err_t err = esp_ota_begin(ota_state.partition, ota_state.expected_size, &ota_state.handle);
    if (err != ESP_OK) {
        ota_state.last_error = err;
        ota_state.in_progress = false;
        httpd_resp_send_500(req);
        return err;
    }

    char buffer[1024];
    size_t remaining = req->content_len;

    while (remaining > 0) {
        int to_read = remaining > sizeof(buffer) ? sizeof(buffer) : remaining;
        int received = httpd_req_recv(req, buffer, to_read);
        if (received <= 0) {
            if (received == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            esp_ota_abort(ota_state.handle);
            ota_state.last_error = ESP_FAIL;
            ota_state.in_progress = false;
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Firmware receive failed");
            return ESP_FAIL;
        }

        err = esp_ota_write(ota_state.handle, buffer, received);
        if (err != ESP_OK) {
            esp_ota_abort(ota_state.handle);
            ota_state.last_error = err;
            ota_state.in_progress = false;
            httpd_resp_send_500(req);
            return err;
        }

        remaining -= received;
        ota_state.bytes_written += received;
    }

    err = esp_ota_end(ota_state.handle);
    if (err != ESP_OK) {
        ota_state.last_error = err;
        ota_state.in_progress = false;
        httpd_resp_send_500(req);
        return err;
    }

    err = esp_ota_set_boot_partition(ota_state.partition);
    if (err != ESP_OK) {
        ota_state.last_error = err;
        ota_state.in_progress = false;
        httpd_resp_send_500(req);
        return err;
    }

    ota_state.in_progress = false;
    ota_state.pending_reboot = true;
    httpd_resp_sendstr(req, "Firmware uploaded. Rebooting...");
    schedule_reboot();
    return ESP_OK;
}

static esp_err_t api_led_status_handler(httpd_req_t *req) {
    cJSON *json = cJSON_CreateObject();
    if (!json) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    display_controller_config_t cfg;
    esp_err_t ret = display_controller_get_config(&cfg);
    if (ret != ESP_OK) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Display controller not ready");
        return ret;
    }

    display_stats_t stats;
    display_controller_get_stats(&stats);

    cJSON_AddBoolToObject(json, "active", display_controller_is_active());
    const char *display_level = fluid_level_to_string(display_controller_get_current_level());
    cJSON_AddStringToObject(json, "display_level", display_level ? display_level : "UNKNOWN");
    cJSON_AddNumberToObject(json, "brightness", cfg.brightness);
    cJSON_AddStringToObject(json, "mode", display_controller_mode_to_string(cfg.mode));
    cJSON_AddNumberToObject(json, "periodic_interval_ms", cfg.periodic_interval_ms);
    cJSON_AddNumberToObject(json, "display_duration_ms", cfg.display_duration_ms);
    cJSON_AddBoolToObject(json, "show_startup_sequence", cfg.show_startup_sequence);
    cJSON_AddBoolToObject(json, "fade_in_out", cfg.fade_in_out);
    cJSON_AddNumberToObject(json, "total_displays", stats.total_displays);
    cJSON_AddNumberToObject(json, "manual_triggers", stats.manual_triggers);
    cJSON_AddNumberToObject(json, "periodic_triggers", stats.periodic_triggers);
    cJSON_AddNumberToObject(json, "fluid_change_triggers", stats.fluid_change_triggers);
    cJSON_AddNumberToObject(json, "last_display_time_ms", stats.last_display_time);
    cJSON_AddStringToObject(json, "last_trigger",
                            display_controller_trigger_to_string(stats.last_trigger));

    char *payload = cJSON_PrintUnformatted(json);
    if (!payload) {
        cJSON_Delete(json);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, payload, strlen(payload));

    cJSON_free(payload);
    cJSON_Delete(json);
    return ESP_OK;
}

static esp_err_t api_health_handler(httpd_req_t *req) {
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    cJSON *status_json = build_status_json();
    if (!status_json) {
        cJSON_Delete(root);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    cJSON_AddItemToObject(root, "status", status_json);

    cJSON_AddNumberToObject(root, "uptime_seconds", portal_status.uptime_seconds);
    cJSON_AddNumberToObject(root, "free_heap_bytes", esp_get_free_heap_size());
    cJSON_AddNumberToObject(root, "minimum_free_heap_bytes", esp_get_minimum_free_heap_size());
    cJSON_AddStringToObject(root, "idf_version", esp_get_idf_version());
    cJSON_AddBoolToObject(root, "ota_in_progress", ota_state.in_progress);
    cJSON_AddBoolToObject(root, "ota_pending_reboot", ota_state.pending_reboot);
    cJSON_AddBoolToObject(root, "wifi_initialized", wifi_initialized);
    cJSON_AddBoolToObject(root, "http_server_running", server_running);

    char *payload = cJSON_PrintUnformatted(root);
    if (!payload) {
        cJSON_Delete(root);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, payload, strlen(payload));

    cJSON_free(payload);
    cJSON_Delete(root);
    return ESP_OK;
}

esp_err_t wifi_config_init(const wifi_config_init_t *config) {
    if (!config) {
        ESP_LOGE(TAG, "Configuration is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    // Store configuration
    init_config = *config;
    current_display_config = config->default_display;
    clamp_display_config(&current_display_config);
    manual_trigger_pending = false;
    portal_status.display_config = current_display_config;
    portal_status.demo_mode_requested = demo_mode_requested = false;
    portal_status.demo_mode_active = false;
    ota_reset_state();

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_RETURN_ON_ERROR(ret, TAG, "NVS init failed");

    // Initialize network interface
    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "Failed to init netif");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "Failed to create event loop");

    wifi_initialized = true;
    ESP_LOGI(TAG, "WiFi configuration initialized");

    return ESP_OK;
}

esp_err_t wifi_config_start_ap(void) {
    if (!wifi_initialized) {
        ESP_LOGE(TAG, "WiFi not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    // Create WiFi AP netif
    wifi_netif = esp_netif_create_default_wifi_ap();

    // Initialize WiFi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "WiFi init failed");

    // Register event handler
    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                   &wifi_event_handler, NULL),
                        TAG, "Failed to register WiFi event handler");

    // Configure AP
    wifi_config_t wifi_config = {
        .ap = {
            .ssid = WIFI_AP_SSID,
            .password = WIFI_AP_PASSWORD,
            .ssid_len = strlen(WIFI_AP_SSID),
            .channel = WIFI_AP_CHANNEL,
            .authmode = WIFI_AUTH_WPA2_PSK,
            .max_connection = WIFI_AP_MAX_CONNECTIONS,
            .pmf_cfg = {
                .required = false,
            },
        },
    };

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_AP), TAG, "Failed to set AP mode");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &wifi_config), TAG, "Failed to set AP config");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "Failed to start WiFi");

    ESP_LOGI(TAG, "WiFi AP started: SSID=%s, Channel=%d", WIFI_AP_SSID, WIFI_AP_CHANNEL);

    return ESP_OK;
}

esp_err_t wifi_config_start_server(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = HTTP_SERVER_PORT;
    config.lru_purge_enable = true;

    ESP_RETURN_ON_ERROR(httpd_start(&http_server, &config), TAG, "Failed to start HTTP server");

    // Register URI handlers
    httpd_register_uri_handler(http_server, &uri_get);
    httpd_register_uri_handler(http_server, &uri_api_status);
    httpd_register_uri_handler(http_server, &uri_api_config);
    httpd_register_uri_handler(http_server, &uri_api_trigger);
    httpd_register_uri_handler(http_server, &uri_api_demo);
    httpd_register_uri_handler(http_server, &uri_api_ota);
    httpd_register_uri_handler(http_server, &uri_api_led);
    httpd_register_uri_handler(http_server, &uri_api_health);

    server_running = true;
    ESP_LOGI(TAG, "HTTP server started on port %d", HTTP_SERVER_PORT);

    return ESP_OK;
}

esp_err_t wifi_config_stop_ap(void) {
    esp_err_t ret = esp_wifi_stop();
    if (ret == ESP_OK) {
        esp_wifi_deinit();
    }
    return ret;
}

esp_err_t wifi_config_stop_server(void) {
    if (http_server) {
        esp_err_t ret = httpd_stop(http_server);
        http_server = NULL;
        server_running = false;
        return ret;
    }
    return ESP_OK;
}

esp_err_t wifi_config_get_display_config(display_config_t *config) {
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }
    clamp_display_config(&current_display_config);
    *config = current_display_config;
    return ESP_OK;
}

esp_err_t wifi_config_set_display_config(const display_config_t *config) {
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }
    current_display_config = *config;
    clamp_display_config(&current_display_config);
    sync_display_controller_config();
    portal_status.display_config = current_display_config;
    ESP_LOGI(TAG, "Display configuration updated via API");
    return ESP_OK;
}

esp_err_t wifi_config_trigger_display(void) {
    if (!current_display_config.manual_trigger_enabled) {
        return ESP_ERR_INVALID_STATE;
    }
    manual_trigger_pending = true;
    return ESP_OK;
}

bool wifi_config_should_trigger_display(void) {
    return manual_trigger_pending && current_display_config.manual_trigger_enabled;
}

void wifi_config_clear_trigger_flag(void) {
    manual_trigger_pending = false;
}

bool wifi_config_manual_trigger_enabled(void) {
    return current_display_config.manual_trigger_enabled;
}

bool wifi_config_demo_mode_requested(void) {
    return demo_mode_requested;
}

void wifi_config_set_demo_mode_requested(bool enable) {
    demo_mode_requested = enable;
    portal_status.demo_mode_requested = enable;
}

esp_err_t wifi_config_get_status(uint8_t *connected_clients, char *ap_ip) {
    if (connected_clients) {
        *connected_clients = 0; // TODO: Get actual count
    }
    if (ap_ip) {
        strcpy(ap_ip, "192.168.4.1"); // Default AP IP
    }
    return ESP_OK;
}

void wifi_config_print_info(void) {
    printf("\n=== MatrixFluid WiFi Configuration ===\n");
    printf("WiFi Network: %s\n", WIFI_AP_SSID);
    printf("Password: %s\n", WIFI_AP_PASSWORD);
    printf("Web Interface: http://192.168.4.1/\n");
    printf("Features:\n");
    printf("  - Manual fluid level display trigger\n");
    printf("  - Configurable automatic display timing\n");
    printf("  - Brightness control\n");
    printf("  - Pin assignment reference\n");
    printf("=====================================\n\n");
}

esp_err_t wifi_config_deinit(void) {
    wifi_config_stop_server();
    wifi_config_stop_ap();

    if (wifi_netif) {
        esp_netif_destroy(wifi_netif);
        wifi_netif = NULL;
    }

    wifi_initialized = false;
    return ESP_OK;
}
