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
#include "alerts.h"

static const char *TAG = "wifi_config";

// WiFi and HTTP server handles
static esp_netif_t *wifi_netif = NULL;
static esp_netif_t *sta_netif = NULL;
static httpd_handle_t http_server = NULL;
static bool wifi_initialized = false;
static bool server_running = false;

// Configuration storage
static display_config_t current_display_config = {0};
static bool manual_trigger_pending = false;
static wifi_config_init_t init_config = {0};
static wifi_portal_status_t portal_status = {0};
static bool demo_mode_requested = false;

#define WIFI_STA_NVS_NAMESPACE "wifi_sta"
#define WIFI_STA_NVS_KEY_CONFIG "config"

typedef struct {
    bool enabled;
    char ssid[WIFI_STA_MAX_SSID_LEN + 1];
    char password[WIFI_STA_MAX_PASS_LEN + 1];
} portal_wifi_sta_config_t;

typedef struct {
    bool has_credentials;
    bool connecting;
    bool connected;
    char ip[16];
    int last_disconnect_reason;
    char last_error[64];
} portal_wifi_sta_runtime_t;

static portal_wifi_sta_config_t sta_config = {0};
static portal_wifi_sta_runtime_t sta_runtime = {0};
static bool wifi_handlers_registered = false;

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
static bool wifi_sta_should_connect(void);
static void wifi_sta_update_status(void);

static void wifi_sta_reset_runtime(void) {
    sta_runtime.has_credentials = wifi_sta_should_connect();
    sta_runtime.connecting = false;
    sta_runtime.connected = false;
    sta_runtime.ip[0] = '\0';
    sta_runtime.last_disconnect_reason = 0;
    sta_runtime.last_error[0] = '\0';
    wifi_sta_update_status();
}

static void wifi_sta_load_config(void) {
    portal_wifi_sta_config_t default_cfg = {
        .enabled = false,
    };
    memset(default_cfg.ssid, 0, sizeof(default_cfg.ssid));
    memset(default_cfg.password, 0, sizeof(default_cfg.password));

    sta_config = default_cfg;

    nvs_handle_t handle;
    esp_err_t err = nvs_open(WIFI_STA_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_OK) {
        size_t required = sizeof(sta_config);
        err = nvs_get_blob(handle, WIFI_STA_NVS_KEY_CONFIG, &sta_config, &required);
        if (err != ESP_OK || required != sizeof(sta_config)) {
            sta_config = default_cfg;
        }
        nvs_close(handle);
    }

    if (!wifi_sta_should_connect()) {
        sta_config.password[0] = '\0';
    }

    wifi_sta_reset_runtime();
}

static void wifi_sta_save_config(void) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(WIFI_STA_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for STA config: %s", esp_err_to_name(err));
        return;
    }

    err = nvs_set_blob(handle, WIFI_STA_NVS_KEY_CONFIG, &sta_config, sizeof(sta_config));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save STA config: %s", esp_err_to_name(err));
    }
    nvs_close(handle);
}

static bool wifi_sta_should_connect(void) {
    return sta_config.enabled && sta_config.ssid[0] != '\0';
}

static void wifi_sta_update_status(void) {
    portal_status.sta_enabled = sta_config.enabled;
    portal_status.sta_has_credentials = sta_runtime.has_credentials;
    portal_status.sta_connecting = sta_runtime.connecting;
    portal_status.sta_connected = sta_runtime.connected;
    strncpy(portal_status.sta_ssid, sta_config.ssid, sizeof(portal_status.sta_ssid) - 1);
    portal_status.sta_ssid[sizeof(portal_status.sta_ssid) - 1] = '\0';
    strncpy(portal_status.sta_ip, sta_runtime.ip, sizeof(portal_status.sta_ip) - 1);
    portal_status.sta_ip[sizeof(portal_status.sta_ip) - 1] = '\0';
    portal_status.sta_last_disconnect_reason = sta_runtime.last_disconnect_reason;
    strncpy(portal_status.sta_last_error, sta_runtime.last_error, sizeof(portal_status.sta_last_error) - 1);
    portal_status.sta_last_error[sizeof(portal_status.sta_last_error) - 1] = '\0';
}

static void wifi_sta_set_error(const char *message, int reason_code) {
    sta_runtime.last_disconnect_reason = reason_code;
    if (message) {
        strncpy(sta_runtime.last_error, message, sizeof(sta_runtime.last_error) - 1);
        sta_runtime.last_error[sizeof(sta_runtime.last_error) - 1] = '\0';
    } else {
        sta_runtime.last_error[0] = '\0';
    }
    wifi_sta_update_status();
}

static esp_err_t wifi_sta_apply_config(bool restart_wifi) {
    bool sta_enabled = wifi_sta_should_connect();

    wifi_config_t ap_config;
    memset(&ap_config, 0, sizeof(ap_config));
    strncpy((char *)ap_config.ap.ssid, WIFI_AP_SSID, sizeof(ap_config.ap.ssid));
    ap_config.ap.ssid_len = strlen(WIFI_AP_SSID);
    strncpy((char *)ap_config.ap.password, WIFI_AP_PASSWORD, sizeof(ap_config.ap.password));
    ap_config.ap.channel = WIFI_AP_CHANNEL;
    ap_config.ap.max_connection = WIFI_AP_MAX_CONNECTIONS;
    ap_config.ap.authmode = WIFI_AP_PASSWORD[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    ap_config.ap.pmf_cfg.required = false;

    wifi_config_t sta_wifi_config;
    memset(&sta_wifi_config, 0, sizeof(sta_wifi_config));
    if (sta_enabled) {
        strncpy((char *)sta_wifi_config.sta.ssid, sta_config.ssid, sizeof(sta_wifi_config.sta.ssid));
        strncpy((char *)sta_wifi_config.sta.password, sta_config.password, sizeof(sta_wifi_config.sta.password));
        sta_wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
        sta_wifi_config.sta.pmf_cfg.capable = true;
        sta_wifi_config.sta.pmf_cfg.required = false;
    }

    if (restart_wifi) {
        esp_wifi_disconnect();
        esp_err_t stop_ret = esp_wifi_stop();
        if (stop_ret != ESP_OK && stop_ret != ESP_ERR_WIFI_NOT_INIT && stop_ret != ESP_ERR_WIFI_NOT_STARTED) {
            ESP_RETURN_ON_ERROR(stop_ret, TAG, "Failed to stop WiFi");
        }
    }

    wifi_mode_t target_mode = sta_enabled ? WIFI_MODE_APSTA : WIFI_MODE_AP;
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(target_mode), TAG, "Failed to set WiFi mode");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &ap_config), TAG, "Failed to set AP config");

    if (sta_enabled) {
        ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &sta_wifi_config), TAG, "Failed to set STA config");
    }

    esp_err_t start_ret = esp_wifi_start();
    if (start_ret != ESP_OK && start_ret != ESP_ERR_WIFI_STATE) {
        ESP_RETURN_ON_ERROR(start_ret, TAG, "Failed to start WiFi");
    }

    if (sta_enabled) {
        sta_runtime.has_credentials = true;
        sta_runtime.connecting = true;
        sta_runtime.connected = false;
        sta_runtime.ip[0] = '\0';
        wifi_sta_set_error(NULL, 0);
        esp_wifi_connect();
    } else {
        sta_runtime.has_credentials = false;
        sta_runtime.connecting = false;
        if (sta_runtime.connected) {
            esp_wifi_disconnect();
        }
        sta_runtime.connected = false;
        sta_runtime.ip[0] = '\0';
        wifi_sta_set_error(NULL, 0);
    }

    wifi_sta_update_status();
    return ESP_OK;
}


static void clamp_display_config(display_config_t *config);
static void sync_display_controller_config(void);
static void ota_reset_state(void);
static void schedule_reboot(void);
static void wifi_sta_load_config(void);
static void wifi_sta_save_config(void);
static void wifi_sta_reset_runtime(void);
static bool wifi_sta_should_connect(void);
static esp_err_t wifi_sta_apply_config(bool restart_wifi);
static void wifi_sta_update_status(void);
static void wifi_sta_set_error(const char *message, int reason_code);
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data);
static void ip_event_handler(void* arg, esp_event_base_t event_base,
                             int32_t event_id, void* event_data);

// Configuration limits
#define MIN_DISPLAY_INTERVAL_SECONDS  60U
#define MAX_DISPLAY_INTERVAL_SECONDS 3600U
#define MIN_DISPLAY_DURATION_SECONDS  1U
#define MAX_DISPLAY_DURATION_SECONDS 30U

// HTML content for the web interface
static const char* html_page =
"<!DOCTYPE html><html lang='en'><head><meta charset='utf-8'><title>MatrixFluid Portal</title>"
"<meta name='viewport' content='width=device-width, initial-scale=1'>"
"<style>"
":root{--bg:#0f172a;--card:#15223a;--accent:#38bdf8;--accent-alt:#818cf8;--text:#e2e8f0;--muted:#94a3b8;}*{box-sizing:border-box;}"
"body{font-family:'Inter',system-ui,-apple-system,sans-serif;margin:0;background:linear-gradient(160deg,#0b1220,#111c31);color:var(--text);}"
"header{padding:36px 18px;text-align:center;}header h1{margin:0;font-size:1.9rem;letter-spacing:.04em;}header p{margin:10px 0 0;font-size:.95rem;color:var(--muted);}"
"main{max-width:860px;margin:0 auto;padding:0 18px 48px;}section{background:var(--card);border-radius:18px;margin-bottom:20px;padding:24px;box-shadow:0 20px 45px rgba(15,23,42,.45);backdrop-filter:blur(12px);}"
"h2{margin:0 0 18px;font-size:1.18rem;letter-spacing:.03em;text-transform:uppercase;color:var(--muted);}"
".status-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(160px,1fr));gap:16px;}"
".status-card{padding:16px;border-radius:14px;background:rgba(148,163,184,.08);border:1px solid rgba(148,163,184,.18);min-height:112px;display:flex;flex-direction:column;justify-content:space-between;}"
".status-card span{font-size:.78rem;letter-spacing:.08em;text-transform:uppercase;color:var(--muted);}"
".status-value{font-size:1.3rem;font-weight:600;margin-top:8px;transition:color .25s;}"
".level-ok{color:#34d399;}.level-warn{color:#fbbf24;}.level-crit{color:#f87171;}"
"label{display:block;margin:16px 0 6px;font-weight:600;color:var(--muted);}"
"input[type='number'],select{width:100%;padding:12px;border-radius:12px;border:1px solid rgba(148,163,184,.4);background:rgba(15,23,42,.6);color:var(--text);font-size:1rem;}"
"input[type='number']:focus,select:focus{outline:2px solid var(--accent);}input[type='range']{width:100%;accent-color:var(--accent-alt);}"
".controls{display:flex;flex-wrap:wrap;gap:12px;margin-top:6px;}"
".btn{flex:0 0 auto;padding:12px 20px;border:none;border-radius:12px;font-size:.98rem;font-weight:600;cursor:pointer;transition:transform .15s ease,box-shadow .2s ease;}"
".btn:hover{transform:translateY(-1px);}"
".btn-primary{background:var(--accent);color:#041123;box-shadow:0 10px 25px rgba(56,189,248,.35);}"
".btn-primary:hover{box-shadow:0 16px 32px rgba(56,189,248,.45);}"
".btn-success{background:#34d399;color:#022c22;box-shadow:0 10px 24px rgba(52,211,153,.38);}"
".btn-success:hover{box-shadow:0 16px 40px rgba(52,211,153,.48);}"
".switch{display:flex;align-items:center;gap:12px;margin-top:18px;padding:14px;border:1px dashed rgba(148,163,184,.3);border-radius:12px;background:rgba(148,163,184,.06);}"
".switch input{width:40px;height:22px;}"
".badge{display:inline-flex;align-items:center;padding:6px 12px;border-radius:999px;font-size:.82rem;font-weight:600;margin-left:10px;letter-spacing:.04em;}"
".badge-on{background:rgba(74,222,128,.2);color:#4ade80;}"
".badge-off{background:rgba(248,113,113,.2);color:#f87171;}"
".help{font-size:.85rem;color:var(--muted);margin-top:10px;white-space:pre-wrap;}"
".grid-cols{display:grid;grid-template-columns:repeat(auto-fit,minmax(220px,1fr));gap:16px;}"
".tile{padding:18px;border-radius:14px;background:rgba(15,23,42,.55);border:1px solid rgba(148,163,184,.15);}"
".tile h3{margin:0 0 12px;font-size:1rem;color:var(--text);}"
".progress{height:6px;background:rgba(148,163,184,.2);border-radius:999px;margin-top:12px;overflow:hidden;}"
".progress-bar{height:100%;width:0;background:var(--accent-alt);border-radius:inherit;transition:width .3s ease;}"
".brightness-readout{text-align:center;margin-top:8px;font-weight:600;letter-spacing:.02em;color:var(--text);}"
".days-grid{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:8px;font-size:.85rem;}"
".days-grid label{display:flex;align-items:center;gap:6px;margin:0;font-weight:500;color:var(--text);}"
".status-meta{display:grid;gap:6px;font-size:.85rem;color:var(--muted);}"
".status-meta span{color:var(--text);}"
"code{background:rgba(148,163,184,.16);padding:4px 6px;border-radius:8px;color:var(--text);}"
"@media (max-width:640px){header h1{font-size:1.6rem;}section{padding:20px;}.controls{flex-direction:column;align-items:stretch;}.btn{width:100%;text-align:center;}}"
"</style></head><body>"
"<header><h1>MatrixFluid Controller</h1><p>Join <code>" WIFI_AP_SSID "</code> &bull; Password <code>" WIFI_AP_PASSWORD "</code></p></header><main>"
"<section><h2>Live Status</h2><div class='status-grid'>"
"<div class='status-card'><span>Fluid Level</span><div id='fluid-level' class='status-value level-ok'>Loading...</div></div>"
"<div class='status-card'><span>Display</span><div id='display-state' class='status-value'>...</div></div>"
"<div class='status-card'><span>Power Source</span><div id='power-source' class='status-value'>--</div></div>"
"<div class='status-card'><span>Next Wake</span><div id='next-wake' class='status-value'>--</div></div>"
"<div class='status-card'><span>Manual Triggers</span><div id='manual-count' class='status-value'>0</div></div>"
"<div class='status-card'><span>Periodic Triggers</span><div id='periodic-count' class='status-value'>0</div></div>"
"<div class='status-card'><span>Uptime</span><div id='uptime' class='status-value'>--</div></div>"
"<div class='status-card'><span>Connected Clients</span><div id='client-count' class='status-value'>0</div></div>"
"<div class='status-card'><span>Wi-Fi STA</span><div id='wifi-sta' class='status-value'>--</div></div>"
"<div class='status-card'><span>Wi-Fi IP</span><div id='wifi-ip' class='status-value'>--</div></div>"
"<div class='status-card'><span>Half Sensor</span><div id='sensor-half' class='status-value'>--</div></div>"
"<div class='status-card'><span>Empty Sensor</span><div id='sensor-empty' class='status-value'>--</div></div>"
"</div></section>"
"<section><h2>Manual Actions</h2><div class='controls'>"
"<button class='btn btn-success' onclick='triggerDisplay()'>Show Fluid Level Now</button>"
"<button class='btn btn-primary' type='button' onclick='requestLedStatus()'>LED Snapshot</button>"
"<button class='btn btn-primary' type='button' onclick='requestHealth()'>Device Health</button></div>"
"<div class='switch'><input type='checkbox' id='demo-toggle' onchange='toggleDemo(this.checked)'>"
"<label for='demo-toggle' style='margin:0;'>Demo Mode<span id='demo-state' class='badge badge-off'>Inactive</span></label></div>"
"<p class='help'>Enable demo mode for kiosk demonstrations when the USB data link is connected. Disable for normal timed behaviour.</p>"
"<div class='tile'><h3>LED Controller</h3><p id='led-status' class='help'>LED status: Not requested.</p></div>"
"<div class='tile'><h3>Device Health</h3><pre id='health-status' class='help'>Device health not requested yet.</pre></div>"
"</section>"
"<section><h2>Wi-Fi Network</h2>"
"<form id='wifi-form' onsubmit='saveWifi(event)'>"
"<div class='switch'><input type='checkbox' id='wifi-enabled' onchange='updateWifiControls();'>"
"<label for='wifi-enabled' style='margin:0;'>Connect to local Wi-Fi<span id='wifi-state' class='badge badge-off'>Disabled</span></label></div>"
"<div class='grid-cols'>"
"<div class='tile'>"
"<label for='wifi-ssid'>Network name (SSID)</label>"
"<input type='text' id='wifi-ssid' class='wifi-input' placeholder='HomeNetwork'>"
"<label for='wifi-password'>Password</label>"
"<input type='password' id='wifi-password' class='wifi-input' placeholder='Leave blank to keep current'>"
"<p class='help'>Passwords shorter than 8 characters are treated as open networks. Leave blank to keep the saved password.</p>"
"</div>"
"<div class='tile status-meta'>"
"<div>Connection: <span id='wifi-connection-state'>--</span></div>"
"<div>IP Address: <span id='wifi-ip-status'>--</span></div>"
"<div>Last error: <span id='wifi-last-error'>--</span></div>"
"</div>"
"</div>"
"<div class='controls'>"
"<button type='submit' class='btn btn-primary'>Save Wi-Fi Settings</button>"
"<button type='button' id='wifi-forget' class='btn btn-success' onclick='forgetWifi()'>Forget Credentials</button>"
"</div>"
"</form>"
"</section>"
"<section><h2>Alert Settings</h2>"
"<form id='alert-form' onsubmit='saveAlerts(event)'>"
"<div class='switch'><input type='checkbox' id='alert-enabled' onchange='updateAlertControls();'>"
"<label for='alert-enabled' style='margin:0;'>Alerts<span id='alert-state' class='badge badge-off'>Disabled</span></label></div>"
"<div class='grid-cols'>"
"<div class='tile'>"
"<label for='alert-frequency'>Frequency</label>"
"<select id='alert-frequency' class='alert-input'><option value='DAILY'>Daily</option><option value='WEEKDAYS'>Weekdays</option><option value='WEEKLY'>Custom Days</option></select>"
"<label for='alert-time'>Send at</label>"
"<input type='time' id='alert-time' class='alert-input' value='06:00'>"
"<label for='alert-tz-select'>Timezone</label>"
"<select id='alert-tz-select' class='alert-input'><option value=''>Custom (enter offset)</option><option value='-480'>Pacific Standard (UTC-8)</option><option value='-420'>Pacific Daylight (UTC-7)</option></select>"
"<label for='alert-tz'>Timezone offset (minutes)</label>"
"<input type='number' id='alert-tz' class='alert-input' min='-720' max='840' step='30' value='0'>"
"</div>"
"<div class='tile'><h3>Days</h3>"
"<div id='alert-days' class='days-grid'>"
"<label><input type='checkbox' class='alert-day alert-input' value='0'>Sun</label>"
"<label><input type='checkbox' class='alert-day alert-input' value='1'>Mon</label>"
"<label><input type='checkbox' class='alert-day alert-input' value='2'>Tue</label>"
"<label><input type='checkbox' class='alert-day alert-input' value='3'>Wed</label>"
"<label><input type='checkbox' class='alert-day alert-input' value='4'>Thu</label>"
"<label><input type='checkbox' class='alert-day alert-input' value='5'>Fri</label>"
"<label><input type='checkbox' class='alert-day alert-input' value='6'>Sat</label>"
"</div></div>"
"<div class='tile'>"
"<label for='alert-webhook'>Webhook URL</label><input type='url' id='alert-webhook' class='alert-input' placeholder='https://example.com/webhook'>"
"<label for='alert-auth'>Authorization header</label><input type='text' id='alert-auth' class='alert-input' placeholder='Bearer &lt;token&gt;'>"
"<label for='alert-recipient'>Recipient</label><input type='text' id='alert-recipient' class='alert-input' placeholder='alerts@example.com'>"
"</div></div>"
"<button type='submit' class='btn btn-primary'>Save Alert Settings</button>"
"</form>"
"<div class='tile status-meta'>"
"<div>Next run: <span id='alert-next'>--</span></div>"
"<div>Last attempt: <span id='alert-last-attempt'>--</span></div>"
"<div>Last success: <span id='alert-last-success'>--</span></div>"
"<div>Last error: <span id='alert-last-error'>--</span></div>"
"</div>"
"<div class='controls'>"
"<a id='alert-email-link' class='btn btn-success' href='#' target='_blank' style='display:none;'>Compose Email Alert</a>"
"</div>"
"</section>"
"<section><h2>Automatic Display Settings</h2><form onsubmit='saveConfig(event)'>"
"<label for='periodic'>Enable periodic display</label>"
"<select id='periodic'><option value='true'>Enabled</option><option value='false'>Disabled</option></select>"
"<label for='interval'>Display interval (minutes)</label>"
"<input type='number' id='interval' min='1' max='60' value='15'>"
"<label for='duration'>Display duration (seconds)</label>"
"<input type='number' id='duration' min='1' max='30' value='7'>"
"<label for='brightness'>Brightness (1-5)</label>"
"<input type='range' id='brightness' min='1' max='5' value='3'>"
"<div class='brightness-readout'>Brightness level: <span id='brightness-val'>3</span></div>"
"<button type='submit' class='btn btn-primary'>Save Settings</button>"
"</form></section>"
"<section><h2>Pin Reference</h2>"
"<p><strong>GPIO2</strong> - Half-full sensor (drives 3.3V when closed)</p>"
"<p><strong>GPIO3</strong> - Near-empty sensor (drives 3.3V when closed)</p>"
"<p><strong>GPIO14</strong> - LED matrix data line</p>"
"<p><strong>5V / GND</strong> - Supply via buck converter (vehicle) or USB-C (demo)</p>"
"</section>"
"</section></main>"
"<script>\nconst brightnessInput=document.getElementById('brightness');\nbrightnessInput.addEventListener('input',()=>{document.getElementById('brightness-val').textContent=brightnessInput.value;});\nlet alertFormDirty=false;\nlet suppressAlertChange=false;\nlet wifiFormDirty=false;\nlet suppressWifiChange=false;\nfunction markAlertDirty(){if(!suppressAlertChange){alertFormDirty=true;}}\nfunction syncTimezoneSelect(offset){const select=document.getElementById('alert-tz-select');if(!select)return;if(offset===null||offset===undefined||Number.isNaN(offset)){select.value='';return;}const value=String(offset);const match=Array.from(select.options).some(opt=>opt.value===value&&opt.value!=='');select.value=match?value:'';}\nfunction handleTimezoneSelectChange(){const select=document.getElementById('alert-tz-select');const tzField=document.getElementById('alert-tz');if(!select||!tzField)return;if(select.value!==''){tzField.value=select.value;handleTimezoneInputChange();}else{markAlertDirty();}}\nfunction handleTimezoneInputChange(){const tzField=document.getElementById('alert-tz');if(!tzField)return;const parsed=parseInt(tzField.value,10);syncTimezoneSelect(Number.isNaN(parsed)?null:parsed);markAlertDirty();}\nfunction formatLevel(level){const lower=(level||'UNKNOWN').toLowerCase();if(lower.includes('empty'))return 'NEAR EMPTY';return (level||'UNKNOWN').replace(/_/g,' ');}\nfunction formatTime(seconds){if(seconds===null||seconds===undefined)return '--';if(seconds<60)return seconds+'s';const mins=Math.floor(seconds/60);const secs=seconds%60;return mins+'m '+secs+'s';}\nfunction formatUptime(seconds){if(!seconds)return '--';const hrs=Math.floor(seconds/3600);const mins=Math.floor((seconds%3600)/60);return (hrs?hrs+'h ':'')+mins+'m';}\nfunction formatPowerSource(source){if(!source)return '--';const normalized=String(source).toLowerCase();if(normalized.includes('usb'))return 'USB';if(normalized.includes('buck'))return '5V Buck';return source;}\nfunction describeSensor(submerged,signalHigh){if(signalHigh&&submerged)return 'WET (HIGH)';if(!signalHigh&&!submerged)return 'DRY (LOW)';if(signalHigh&&!submerged)return 'Mixed (HIGH)';if(!signalHigh&&submerged)return 'Mixed (LOW)';return 'UNKNOWN';}\nfunction sensorClass(submerged,signalHigh){if(signalHigh&&submerged)return 'level-ok';if(!signalHigh&&!submerged)return 'level-crit';return 'level-warn';}\nfunction pad(num){return String(num).padStart(2,'0');}\nfunction formatTimestamp(epoch){if(!epoch)return '--';const date=new Date(epoch*1000);if(Number.isNaN(date.getTime()))return '--';return date.toLocaleString();}\nfunction setAlertDays(bitmap){document.querySelectorAll('.alert-day').forEach(cb=>{const bit=1<<parseInt(cb.value,10);cb.checked=!!(bitmap&bit);});}\nfunction getAlertDaysBitmap(){let mask=0;document.querySelectorAll('.alert-day:checked').forEach(cb=>{mask|=(1<<parseInt(cb.value,10));});return mask;}\nfunction alertFrequencyToString(value){const upper=String(value).toUpperCase();if(upper==='WEEKLY'||upper==='1')return 'WEEKLY';if(upper==='WEEKDAYS'||upper==='2')return 'WEEKDAYS';return 'DAILY';}\nfunction updateAlertDaysAvailability(freq){const enabledEl=document.getElementById('alert-enabled');const enabled=enabledEl?enabledEl.checked:false;const upper=(freq||'DAILY').toUpperCase();document.querySelectorAll('.alert-day').forEach(cb=>{if(!enabled){cb.disabled=true;return;}if(upper==='DAILY'){cb.disabled=true;cb.checked=true;}else if(upper==='WEEKDAYS'){const val=parseInt(cb.value,10);const allowed=val>=1&&val<=5;cb.disabled=true;cb.checked=allowed;}else{cb.disabled=false;}});}\nfunction updateAlertControls(){const enabledEl=document.getElementById('alert-enabled');const enabled=enabledEl?enabledEl.checked:false;document.querySelectorAll('.alert-input').forEach(el=>{if(el.classList.contains('alert-day'))return;el.disabled=!enabled;});const frequency=document.getElementById('alert-frequency');if(frequency){updateAlertDaysAvailability(frequency.value);}const badge=document.getElementById('alert-state');if(badge){badge.className='badge '+(enabled?'badge-on':'badge-off');badge.textContent=enabled?'Enabled':'Disabled';}}\nfunction applyAlertStatus(alerts){const editing=alertFormDirty;const enabledToggle=document.getElementById('alert-enabled');suppressAlertChange=true;if(!editing&&enabledToggle){enabledToggle.checked=!!(alerts&&alerts.enabled);}const frequency=document.getElementById('alert-frequency');if(!editing&&frequency){const freqValue=alerts&&(alerts.frequency_string||alertFrequencyToString(alerts.frequency));frequency.value=freqValue||'DAILY';}const timeField=document.getElementById('alert-time');if(!editing&&timeField&&alerts){timeField.value=pad(alerts.hour||0)+':'+pad(alerts.minute||0);}const tzField=document.getElementById('alert-tz');let tzValue=null;if(tzField){if(!editing&&alerts){tzValue=alerts.tz_offset_minutes;tzField.value=tzValue!==undefined&&tzValue!==null?tzValue:0;}else{const parsed=parseInt(tzField.value,10);tzValue=Number.isNaN(parsed)?null:parsed;}syncTimezoneSelect(tzValue);}if(!editing&&alerts){setAlertDays(alerts.days_bitmap);}const webhook=document.getElementById('alert-webhook');if(!editing&&webhook&&alerts){webhook.value=alerts.webhook_url||'';}const auth=document.getElementById('alert-auth');if(!editing&&auth&&alerts){auth.value=alerts.auth_header||'';}const recipient=document.getElementById('alert-recipient');if(!editing&&recipient&&alerts){recipient.value=alerts.recipient||'';}suppressAlertChange=false;const next=document.getElementById('alert-next');if(next){next.textContent=alerts?formatTimestamp(alerts.next_run_epoch):'--';}const lastAttempt=document.getElementById('alert-last-attempt');if(lastAttempt){lastAttempt.textContent=alerts?formatTimestamp(alerts.last_attempt_epoch):'--';}const lastSuccess=document.getElementById('alert-last-success');if(lastSuccess){lastSuccess.textContent=alerts?formatTimestamp(alerts.last_success_epoch):'--';}const lastError=document.getElementById('alert-last-error');if(lastError){lastError.textContent=alerts&&alerts.last_error?alerts.last_error:'--';}updateAlertControls();}\nfunction markWifiDirty(){if(!suppressWifiChange){wifiFormDirty=true;}}\nfunction updateWifiControls(){const enabledToggle=document.getElementById('wifi-enabled');const badge=document.getElementById('wifi-state');const staInfo=window.__wifiStatusCache||null;const enabled=enabledToggle?enabledToggle.checked:false;let text='Disabled';let cls='badge badge-off';if(staInfo){if(staInfo.connected){text='Connected';cls='badge badge-on';}else if(staInfo.connecting){text='Connecting';cls='badge badge-on';}else if(staInfo.enabled&&staInfo.has_credentials){text='Enabled';cls='badge badge-on';}else if(staInfo.has_credentials){text='Ready';cls='badge badge-off';}}else if(enabled){text='Enabled';cls='badge badge-on';}if(badge){badge.className=cls;badge.textContent=text;}const forgetBtn=document.getElementById('wifi-forget');if(forgetBtn){forgetBtn.disabled=!(staInfo&&staInfo.has_credentials);}}\nfunction applyWifiStatus(wifi){window.__wifiStatusCache=wifi&&wifi.sta?wifi.sta:null;const editing=wifiFormDirty;const enabledToggle=document.getElementById('wifi-enabled');suppressWifiChange=true;if(!editing&&enabledToggle){enabledToggle.checked=!!(wifi&&wifi.sta&&wifi.sta.enabled);}const ssidField=document.getElementById('wifi-ssid');if(!editing&&ssidField){ssidField.value=(wifi&&wifi.sta&&wifi.sta.ssid)||'';}const passwordField=document.getElementById('wifi-password');if(!editing&&passwordField){passwordField.value='';}suppressWifiChange=false;const sta=wifi?wifi.sta:null;const connectionEl=document.getElementById('wifi-connection-state');const ipEl=document.getElementById('wifi-ip-status');const errorEl=document.getElementById('wifi-last-error');const connectionText=sta?(sta.connected?'Connected':(sta.connecting?'Connecting':(sta.enabled?'Enabled':(sta.has_credentials?'Ready':'Disabled')))):'Disabled';if(connectionEl){connectionEl.textContent=connectionText;}if(ipEl){ipEl.textContent=(sta&&sta.ip)?sta.ip:'--';}if(errorEl){errorEl.textContent=(sta&&sta.last_error)?sta.last_error:'--';}updateWifiControls();}\nfunction saveWifi(event){event.preventDefault();const payload={};const enabledToggle=document.getElementById('wifi-enabled');if(enabledToggle){payload.enabled=!!enabledToggle.checked;}const ssidField=document.getElementById('wifi-ssid');if(ssidField&&ssidField.value.trim().length){payload.ssid=ssidField.value.trim();}const passwordField=document.getElementById('wifi-password');if(passwordField&&passwordField.value.length){payload.password=passwordField.value;}fetch('/api/wifi',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(payload)}).then(r=>r.text()).then(()=>{alert('Wi-Fi settings saved');wifiFormDirty=false;if(passwordField){passwordField.value='';}setTimeout(loadStatus,400);}).catch(e=>alert('Save Wi-Fi failed: '+e));}\nfunction forgetWifi(){const passwordField=document.getElementById('wifi-password');fetch('/api/wifi',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({forget:true})}).then(r=>r.text()).then(()=>{alert('Wi-Fi credentials cleared');wifiFormDirty=false;if(passwordField){passwordField.value='';}setTimeout(loadStatus,400);}).catch(e=>alert('Forget Wi-Fi failed: '+e));}\nfunction alertEmailTemplate(level){const upper=String(level||'UNKNOWN').toUpperCase();if(upper==='EMPTY'){return{subject:'MatrixFluid Tank EMPTY - Immediate Action Required',body:'The tank is EMPTY. Please refill immediately to restore service.',button:'Send Emergency Email'};}if(upper==='NEAR_EMPTY'){return{subject:'MatrixFluid Tank Low - Refill Soon',body:'The tank is near empty. Please schedule a refill as soon as possible.',button:'Send Low-Level Email'};}if(upper==='BELOW_HALF'||upper==='AT_HALF'){return{subject:'MatrixFluid Tank Below Half',body:'The tank level is below half capacity. Monitor usage and plan a refill.',button:'Send Status Email'};}if(upper==='ABOVE_HALF'){return{subject:'MatrixFluid Tank Status - Normal',body:'The tank level is above half capacity. No immediate action required.',button:'Share Status Email'};}return{subject:'MatrixFluid Tank Status Update',body:'Tank status update available from MatrixFluid.',button:'Compose Status Email'};}\nfunction updateAlertEmailLink(status){const link=document.getElementById('alert-email-link');if(!link){return;}const template=alertEmailTemplate(status&&status.fluid_level);if(!template){link.style.display='none';link.removeAttribute('href');return;}const alerts=status?status.alerts:null;const recipient=alerts&&alerts.recipient?alerts.recipient.trim():'';const levelLabel=formatLevel(status?status.fluid_level:null);const powerLabel=formatPowerSource(status?status.power_source:null)||'--';const timestamp=new Date().toLocaleString();const lines=[template.body,'','Current level: '+levelLabel,'Power source: '+powerLabel,'Timestamp: '+timestamp,'','Sent from MatrixFluid Portal'];const subject=encodeURIComponent(template.subject);const body=encodeURIComponent(lines.join('\n'));const mailto='mailto:'+(recipient?encodeURIComponent(recipient):'')+'?subject='+subject+'&body='+body;link.href=mailto;link.textContent=template.button;link.style.display='inline-block';}\nfunction loadStatus(){\n  fetch('/api/status')\n    .then(r=>r.json())\n    .then(data=>{\n      const level=data.fluid_level||'UNKNOWN';\n      const displayLevel=data.displayed_fluid_level||level;\n      const levelDiv=document.getElementById('fluid-level');\n      const className=level==='ABOVE_HALF'?'level-ok':level==='BELOW_HALF'?'level-warn':'level-crit';\n      levelDiv.className='status-value '+className;\n      levelDiv.textContent=formatLevel(level);\n      const displayState=document.getElementById('display-state');\n      if(displayState){displayState.textContent=data.display_active?('ACTIVE - '+formatLevel(displayLevel)):('OFF - '+formatLevel(displayLevel));}\n      const periodicEnabled=!!(data.config&&data.config.periodic_display_enabled);\n      document.getElementById('next-wake').textContent=periodicEnabled?formatTime(data.next_wake_seconds):'Manual Only';\n      document.getElementById('power-source').textContent=formatPowerSource(data.power_source);\n      document.getElementById('manual-count').textContent=data.stats?data.stats.manual_trigger_count:0;\n      document.getElementById('periodic-count').textContent=data.stats?data.stats.periodic_trigger_count:0;\n      document.getElementById('uptime').textContent=formatUptime(data.uptime_seconds);\n      document.getElementById('client-count').textContent=data.connected_clients||0;\n      const wifiInfo=data.wifi&&data.wifi.sta?data.wifi.sta:null;\n      const wifiStaCard=document.getElementById('wifi-sta');\n      if(wifiStaCard){const connected=wifiInfo&&wifiInfo.connected;const connecting=wifiInfo&&!wifiInfo.connected&&wifiInfo.connecting;const hasCred=wifiInfo&&wifiInfo.has_credentials;let stateText='Disabled';let cls='status-value level-crit';if(connected){stateText='Connected';cls='status-value level-ok';}else if(connecting){stateText='Connecting';cls='status-value level-warn';}else if(wifiInfo&&wifiInfo.enabled&&hasCred){stateText='Enabled';cls='status-value level-warn';}else if(hasCred){stateText='Ready';cls='status-value level-warn';}wifiStaCard.textContent=stateText;wifiStaCard.className=cls;}\n      const wifiIpCard=document.getElementById('wifi-ip');\n      if(wifiIpCard){const ip=wifiInfo&&wifiInfo.ip?wifiInfo.ip:(data.wifi&&data.wifi.ap?data.wifi.ap.ip:'--');wifiIpCard.textContent=ip||'--';wifiIpCard.className=(wifiInfo&&wifiInfo.connected)?'status-value level-ok':'status-value level-warn';}\n      document.getElementById('periodic').value=String(periodicEnabled);\n      document.getElementById('interval').value=Math.max(1,Math.round((data.config.display_interval_seconds||60)/60));\n      document.getElementById('duration').value=data.config.display_duration_seconds||7;\n      brightnessInput.value=data.config.display_brightness||3;\n      document.getElementById('brightness-val').textContent=brightnessInput.value;\n      const sensorHalf=document.getElementById('sensor-half');\n      const sensorEmpty=document.getElementById('sensor-empty');\n      if(sensorHalf){const text=describeSensor(!!data.half_sensor_submerged,!!data.half_sensor_signal_high);sensorHalf.textContent=text;sensorHalf.className='status-value '+sensorClass(!!data.half_sensor_submerged,!!data.half_sensor_signal_high);}\n      if(sensorEmpty){const text=describeSensor(!!data.empty_sensor_submerged,!!data.empty_sensor_signal_high);sensorEmpty.textContent=text;sensorEmpty.className='status-value '+sensorClass(!!data.empty_sensor_submerged,!!data.empty_sensor_signal_high);}\n      const demoToggle=document.getElementById('demo-toggle');\n      if(demoToggle){\n        const portalRequested=!!data.demo_mode_requested;\n        const autoRequested=!!data.auto_demo_requested;\n        const active=!!data.demo_mode_active;\n        demoToggle.checked=portalRequested||active;\n        const badge=document.getElementById('demo-state');\n        const anyRequested=active||portalRequested||autoRequested;\n        badge.className='badge '+(anyRequested?'badge-on':'badge-off');\n        badge.textContent=active?'Running':portalRequested?'Requested':autoRequested?'USB Host':'Inactive';\n      }\n      applyWifiStatus(data.wifi);\n      applyAlertStatus(data.alerts);\n      updateAlertEmailLink(data);\n    })\n    .catch(e=>console.error('Status load failed:',e));\n}\nfunction triggerDisplay(){fetch('/api/trigger',{method:'POST'}).then(r=>r.text()).then(msg=>alert('Display triggered! '+msg)).catch(e=>alert('Trigger failed: '+e));}\nfunction toggleDemo(enabled){fetch('/api/demo',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({enable:!!enabled})}).then(()=>setTimeout(loadStatus,400)).catch(e=>alert('Demo toggle failed: '+e));}\nfunction saveConfig(event){event.preventDefault();const config={periodic_display_enabled:document.getElementById('periodic').value==='true',display_interval_seconds:Math.max(60,parseInt(document.getElementById('interval').value||15,10)*60),display_duration_seconds:Math.max(1,parseInt(document.getElementById('duration').value||7,10)),display_brightness:Math.min(5,Math.max(1,parseInt(brightnessInput.value||3,10)))};fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(config)}).then(r=>r.text()).then(msg=>alert('Settings saved! '+msg)).then(()=>setTimeout(loadStatus,400)).catch(e=>alert('Save failed: '+e));}\nfunction saveAlerts(event){event.preventDefault();var enabled=document.getElementById('alert-enabled')?document.getElementById('alert-enabled').checked:false;var frequency=document.getElementById('alert-frequency')?document.getElementById('alert-frequency').value.toUpperCase():'DAILY';var timeValue=document.getElementById('alert-time')?document.getElementById('alert-time').value:'06:00';var parts=timeValue.split(':');var hour=parseInt(parts[0]||'0',10);var minute=parseInt(parts[1]||'0',10);var tz=parseInt(document.getElementById('alert-tz')?document.getElementById('alert-tz').value:'0',10);var payload={enabled:enabled,frequency:frequency,days_bitmap:getAlertDaysBitmap(),hour:hour,minute:minute,tz_offset_minutes:tz,webhook_url:(document.getElementById('alert-webhook')?document.getElementById('alert-webhook').value.trim():''),auth_header:(document.getElementById('alert-auth')?document.getElementById('alert-auth').value.trim():''),recipient:(document.getElementById('alert-recipient')?document.getElementById('alert-recipient').value.trim():'')};fetch('/api/alerts',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(payload)}).then(r=>r.text()).then(msg=>{alert('Alert settings saved');alertFormDirty=false;setTimeout(loadStatus,400);}).catch(e=>alert('Save alerts failed: '+e));}\nfunction requestLedStatus(){fetch('/api/led').then(r=>r.json()).then(data=>{const statusEl=document.getElementById('led-status');if(statusEl){statusEl.textContent=`${data.active?'ACTIVE':'OFF'} - ${data.display_level} (brightness ${data.brightness}, mode ${data.mode}, last ${data.last_trigger})`;}}).catch(e=>alert('LED status failed: '+e));}\nfunction requestHealth(){fetch('/api/health').then(r=>r.json()).then(data=>{const healthEl=document.getElementById('health-status');if(healthEl){healthEl.textContent=JSON.stringify(data,null,2);}}).catch(e=>alert('Health check failed: '+e));}\nconst alertTzSelect=document.getElementById('alert-tz-select');if(alertTzSelect){alertTzSelect.addEventListener('change',handleTimezoneSelectChange);}\nconst alertTzField=document.getElementById('alert-tz');if(alertTzField){alertTzField.addEventListener('input',handleTimezoneInputChange);alertTzField.addEventListener('change',handleTimezoneInputChange);}\ndocument.querySelectorAll('.alert-input').forEach(el=>{el.addEventListener('input',markAlertDirty);el.addEventListener('change',markAlertDirty);});\ndocument.querySelectorAll('.wifi-input').forEach(el=>{el.addEventListener('input',markWifiDirty);el.addEventListener('change',markWifiDirty);});\nconst alertFrequencyField=document.getElementById('alert-frequency');if(alertFrequencyField){alertFrequencyField.addEventListener('change',function(){updateAlertDaysAvailability(alertFrequencyField.value);});}\nconst alertEnabledToggle=document.getElementById('alert-enabled');if(alertEnabledToggle){alertEnabledToggle.addEventListener('change',()=>{updateAlertControls();markAlertDirty();});}\nconst wifiEnabledToggle=document.getElementById('wifi-enabled');if(wifiEnabledToggle){wifiEnabledToggle.addEventListener('change',()=>{markWifiDirty();updateWifiControls();});}\nupdateAlertControls();\nupdateWifiControls();\nsuppressAlertChange=true;\nhandleTimezoneInputChange();\nsuppressAlertChange=false;\nloadStatus();setInterval(loadStatus,5000);\n</script></body></html>";


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
    wifi_sta_update_status();
    alerts_get_portal_status(&portal_status.alerts);
}

// Forward declarations
static esp_err_t get_handler(httpd_req_t *req);
static cJSON *build_status_json(void);
static const char *alert_frequency_to_string(alert_frequency_t freq);
static esp_err_t api_status_handler(httpd_req_t *req);
static esp_err_t api_config_handler(httpd_req_t *req);
static esp_err_t api_trigger_handler(httpd_req_t *req);
static esp_err_t api_demo_handler(httpd_req_t *req);
static esp_err_t api_ota_handler(httpd_req_t *req);
static esp_err_t api_led_status_handler(httpd_req_t *req);
static esp_err_t api_health_handler(httpd_req_t *req);
static esp_err_t api_alerts_handler(httpd_req_t *req);
static esp_err_t api_wifi_handler(httpd_req_t *req);

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

static httpd_uri_t uri_api_alerts = {
    .uri = "/api/alerts",
    .method = HTTP_POST,
    .handler = api_alerts_handler,
    .user_ctx = NULL
};

static httpd_uri_t uri_api_wifi = {
    .uri = "/api/wifi",
    .method = HTTP_POST,
    .handler = api_wifi_handler,
    .user_ctx = NULL
};

/**
 * @brief WiFi event handler
 */
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data) {
    if (event_base != WIFI_EVENT) {
        return;
    }

    switch (event_id) {
        case WIFI_EVENT_AP_STACONNECTED: {
            wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
            ESP_LOGI(TAG, "Client connected: " MACSTR, MAC2STR(event->mac));
            break;
        }
        case WIFI_EVENT_AP_STADISCONNECTED: {
            wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *)event_data;
            ESP_LOGI(TAG, "Client disconnected: " MACSTR, MAC2STR(event->mac));
            break;
        }
        case WIFI_EVENT_STA_START: {
            if (wifi_sta_should_connect()) {
                sta_runtime.connecting = true;
                sta_runtime.connected = false;
                sta_runtime.ip[0] = '\0';
                wifi_sta_set_error(NULL, 0);
                esp_wifi_connect();
            } else {
                sta_runtime.connecting = false;
                sta_runtime.connected = false;
                sta_runtime.ip[0] = '\0';
                wifi_sta_set_error(NULL, 0);
            }
            break;
        }
        case WIFI_EVENT_STA_CONNECTED: {
            sta_runtime.has_credentials = wifi_sta_should_connect();
            sta_runtime.connecting = true;
            sta_runtime.connected = false;
            wifi_sta_set_error(NULL, 0);
            break;
        }
        case WIFI_EVENT_STA_DISCONNECTED: {
            wifi_event_sta_disconnected_t *event = (wifi_event_sta_disconnected_t *)event_data;
            sta_runtime.connected = false;
            sta_runtime.connecting = wifi_sta_should_connect();
            sta_runtime.ip[0] = '\0';

            char msg[64];
            snprintf(msg, sizeof(msg), "Disconnect reason %d", event->reason);
            wifi_sta_set_error(msg, event->reason);

            if (wifi_sta_should_connect()) {
                ESP_LOGW(TAG, "STA disconnected (reason=%d), retrying", event->reason);
                esp_wifi_connect();
            } else {
                ESP_LOGI(TAG, "STA disconnected (reason=%d)", event->reason);
            }
            break;
        }
        default:
            break;
    }
}

static void ip_event_handler(void* arg, esp_event_base_t event_base,
                             int32_t event_id, void* event_data) {
    if (event_base != IP_EVENT || event_id != IP_EVENT_STA_GOT_IP) {
        return;
    }

    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    sta_runtime.connected = true;
    sta_runtime.connecting = false;
    snprintf(sta_runtime.ip, sizeof(sta_runtime.ip), IPSTR, IP2STR(&event->ip_info.ip));
    wifi_sta_set_error(NULL, 0);
    wifi_sta_update_status();
    ESP_LOGI(TAG, "STA obtained IP: %s", sta_runtime.ip);
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
static const char *alert_frequency_to_string(alert_frequency_t freq) {
    switch (freq) {
        case ALERT_FREQUENCY_WEEKLY:
            return "WEEKLY";
        case ALERT_FREQUENCY_WEEKDAYS:
            return "WEEKDAYS";
        case ALERT_FREQUENCY_DAILY:
        default:
            return "DAILY";
    }
}

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

    cJSON *wifi = cJSON_CreateObject();
    if (wifi) {
        cJSON *ap = cJSON_CreateObject();
        if (ap) {
            cJSON_AddStringToObject(ap, "ssid", WIFI_AP_SSID);
            cJSON_AddStringToObject(ap, "ip", ap_ip);
            cJSON_AddNumberToObject(ap, "clients", clients);
            cJSON_AddItemToObject(wifi, "ap", ap);
        }

        cJSON *sta = cJSON_CreateObject();
        if (sta) {
            cJSON_AddBoolToObject(sta, "enabled", portal_status.sta_enabled);
            cJSON_AddBoolToObject(sta, "has_credentials", portal_status.sta_has_credentials);
            cJSON_AddBoolToObject(sta, "connecting", portal_status.sta_connecting);
            cJSON_AddBoolToObject(sta, "connected", portal_status.sta_connected);
            cJSON_AddStringToObject(sta, "ssid", portal_status.sta_ssid);
            cJSON_AddStringToObject(sta, "ip", portal_status.sta_ip);
            cJSON_AddNumberToObject(sta, "last_disconnect_reason", portal_status.sta_last_disconnect_reason);
            cJSON_AddStringToObject(sta, "last_error", portal_status.sta_last_error);
            cJSON_AddItemToObject(wifi, "sta", sta);
        }

        cJSON_AddItemToObject(json, "wifi", wifi);
    }

    cJSON *alerts = cJSON_CreateObject();
    if (alerts) {
        cJSON_AddBoolToObject(alerts, "enabled", portal_status.alerts.enabled);
        cJSON_AddNumberToObject(alerts, "frequency", portal_status.alerts.frequency);
        cJSON_AddNumberToObject(alerts, "days_bitmap", portal_status.alerts.days_bitmap);
        cJSON_AddNumberToObject(alerts, "hour", portal_status.alerts.hour);
        cJSON_AddNumberToObject(alerts, "minute", portal_status.alerts.minute);
        cJSON_AddNumberToObject(alerts, "tz_offset_minutes", portal_status.alerts.tz_offset_minutes);
        cJSON_AddBoolToObject(alerts, "last_success", portal_status.alerts.last_success);
        cJSON_AddBoolToObject(alerts, "last_attempt_failed", portal_status.alerts.last_attempt_failed);
        cJSON_AddNumberToObject(alerts, "last_attempt_epoch", portal_status.alerts.last_attempt_epoch);
        cJSON_AddNumberToObject(alerts, "last_success_epoch", portal_status.alerts.last_success_epoch);
        cJSON_AddNumberToObject(alerts, "next_run_epoch", portal_status.alerts.next_run_epoch);
        cJSON_AddStringToObject(alerts, "frequency_string",
                                alert_frequency_to_string((alert_frequency_t)portal_status.alerts.frequency));
        cJSON_AddStringToObject(alerts, "webhook_url", portal_status.alerts.webhook_url);
        cJSON_AddStringToObject(alerts, "auth_header", portal_status.alerts.auth_header);
        cJSON_AddStringToObject(alerts, "recipient", portal_status.alerts.recipient);
        cJSON_AddStringToObject(alerts, "last_error", portal_status.alerts.last_error);
        cJSON_AddItemToObject(json, "alerts", alerts);
    }

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

static esp_err_t api_alerts_handler(httpd_req_t *req) {
    char content[MAX_HTTP_REQUEST_SIZE];
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

    alert_config_t cfg;
    alerts_get_config(&cfg);

    cJSON *enabled = cJSON_GetObjectItem(json, "enabled");
    if (cJSON_IsBool(enabled)) {
        cfg.enabled = cJSON_IsTrue(enabled);
    }

    cJSON *frequency = cJSON_GetObjectItem(json, "frequency");
    if (cJSON_IsString(frequency) && frequency->valuestring) {
        if (!strcasecmp(frequency->valuestring, "WEEKLY")) {
            cfg.frequency = ALERT_FREQUENCY_WEEKLY;
        } else if (!strcasecmp(frequency->valuestring, "WEEKDAYS")) {
            cfg.frequency = ALERT_FREQUENCY_WEEKDAYS;
        } else {
            cfg.frequency = ALERT_FREQUENCY_DAILY;
        }
    }

    cJSON *days = cJSON_GetObjectItem(json, "days_bitmap");
    if (cJSON_IsNumber(days)) {
        cfg.days_bitmap = (uint8_t)(days->valueint & 0x7F);
    }

    cJSON *hour = cJSON_GetObjectItem(json, "hour");
    if (cJSON_IsNumber(hour)) {
        cfg.hour = (uint8_t)hour->valueint;
    }

    cJSON *minute = cJSON_GetObjectItem(json, "minute");
    if (cJSON_IsNumber(minute)) {
        cfg.minute = (uint8_t)minute->valueint;
    }

    cJSON *tz = cJSON_GetObjectItem(json, "tz_offset_minutes");
    if (cJSON_IsNumber(tz)) {
        cfg.tz_offset_minutes = (int16_t)tz->valueint;
    }

    cJSON *url = cJSON_GetObjectItem(json, "webhook_url");
    if (cJSON_IsString(url) && url->valuestring) {
        strncpy(cfg.webhook_url, url->valuestring, sizeof(cfg.webhook_url) - 1);
        cfg.webhook_url[sizeof(cfg.webhook_url) - 1] = '\0';
    }

    cJSON *auth = cJSON_GetObjectItem(json, "auth_header");
    if (cJSON_IsString(auth) && auth->valuestring) {
        strncpy(cfg.auth_header, auth->valuestring, sizeof(cfg.auth_header) - 1);
        cfg.auth_header[sizeof(cfg.auth_header) - 1] = '\0';
    }

    cJSON *recipient = cJSON_GetObjectItem(json, "recipient");
    if (cJSON_IsString(recipient) && recipient->valuestring) {
        strncpy(cfg.recipient, recipient->valuestring, sizeof(cfg.recipient) - 1);
        cfg.recipient[sizeof(cfg.recipient) - 1] = '\0';
    }

    cJSON_Delete(json);

    if (!alerts_apply_config(&cfg)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid alert configuration");
        return ESP_FAIL;
    }

    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}

static esp_err_t api_wifi_handler(httpd_req_t *req) {
    char content[MAX_HTTP_REQUEST_SIZE];
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

    portal_wifi_sta_config_t previous = sta_config;
    bool forget = false;
    bool enabled = sta_config.enabled;
    bool ssid_provided = false;
    bool password_provided = false;
    char new_ssid[WIFI_STA_MAX_SSID_LEN + 1] = {0};
    char new_password[WIFI_STA_MAX_PASS_LEN + 1] = {0};

    cJSON *forget_item = cJSON_GetObjectItem(json, "forget");
    if (cJSON_IsBool(forget_item)) {
        forget = cJSON_IsTrue(forget_item);
    }

    cJSON *enabled_item = cJSON_GetObjectItem(json, "enabled");
    if (cJSON_IsBool(enabled_item)) {
        enabled = cJSON_IsTrue(enabled_item);
    }

    cJSON *ssid_item = cJSON_GetObjectItem(json, "ssid");
    if (cJSON_IsString(ssid_item) && ssid_item->valuestring) {
        strncpy(new_ssid, ssid_item->valuestring, sizeof(new_ssid) - 1);
        ssid_provided = true;
    }

    cJSON *password_item = cJSON_GetObjectItem(json, "password");
    if (cJSON_IsString(password_item) && password_item->valuestring) {
        strncpy(new_password, password_item->valuestring, sizeof(new_password) - 1);
        password_provided = true;
    }

    if (forget) {
        enabled = false;
        sta_config.enabled = false;
        sta_config.ssid[0] = '\0';
        sta_config.password[0] = '\0';
    } else {
        if (ssid_provided) {
            bool ssid_changed = strncmp(sta_config.ssid, new_ssid, sizeof(sta_config.ssid)) != 0;
            strncpy(sta_config.ssid, new_ssid, sizeof(sta_config.ssid) - 1);
            sta_config.ssid[sizeof(sta_config.ssid) - 1] = '\0';
            if (ssid_changed && !password_provided) {
                sta_config.password[0] = '\0';
            }
        }
        if (password_provided) {
            strncpy(sta_config.password, new_password, sizeof(sta_config.password) - 1);
            sta_config.password[sizeof(sta_config.password) - 1] = '\0';
        }
        sta_config.enabled = enabled;
    }

    cJSON_Delete(json);

    if (sta_config.enabled && sta_config.ssid[0] == '\0') {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "SSID required when enabling STA");
        sta_config = previous;
        return ESP_FAIL;
    }

    bool changed = memcmp(&previous, &sta_config, sizeof(sta_config)) != 0;
    esp_err_t err = ESP_OK;

    if (changed) {
        wifi_sta_save_config();
        err = wifi_sta_apply_config(true);
    } else if (sta_config.enabled) {
        sta_runtime.connecting = true;
        sta_runtime.connected = false;
        sta_runtime.ip[0] = '\0';
        wifi_sta_set_error(NULL, 0);
        err = esp_wifi_connect();
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to apply STA config: %s", esp_err_to_name(err));
        sta_config = previous;
        wifi_sta_save_config();
        wifi_sta_apply_config(true);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to apply WiFi config");
        return ESP_FAIL;
    }

    httpd_resp_sendstr(req, "OK");
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

    wifi_sta_load_config();

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

    if (!wifi_netif) {
        wifi_netif = esp_netif_create_default_wifi_ap();
    }
    if (!sta_netif) {
        sta_netif = esp_netif_create_default_wifi_sta();
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "WiFi init failed");

    if (!wifi_handlers_registered) {
        ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                       &wifi_event_handler, NULL),
                            TAG, "Failed to register WiFi event handler");
        ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                       &ip_event_handler, NULL),
                            TAG, "Failed to register IP event handler");
        wifi_handlers_registered = true;
    }

    ESP_RETURN_ON_ERROR(wifi_sta_apply_config(false), TAG, "Failed to configure WiFi");

    ESP_LOGI(TAG, "WiFi services started (AP SSID=%s)", WIFI_AP_SSID);

    return ESP_OK;
}

esp_err_t wifi_config_start_server(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = HTTP_SERVER_PORT;
    config.lru_purge_enable = true;
    config.max_uri_handlers = 16;

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
    httpd_register_uri_handler(http_server, &uri_api_alerts);
    httpd_register_uri_handler(http_server, &uri_api_wifi);

    server_running = true;
    ESP_LOGI(TAG, "HTTP server started on port %d", HTTP_SERVER_PORT);

    return ESP_OK;
}

esp_err_t wifi_config_stop_ap(void) {
    if (wifi_handlers_registered) {
        esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler);
        esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, &ip_event_handler);
        wifi_handlers_registered = false;
    }

    esp_err_t ret = esp_wifi_stop();
    if (ret != ESP_OK && ret != ESP_ERR_WIFI_NOT_INIT && ret != ESP_ERR_WIFI_NOT_STARTED) {
        return ret;
    }

    esp_wifi_deinit();

    if (wifi_netif) {
        esp_netif_destroy(wifi_netif);
        wifi_netif = NULL;
    }
    if (sta_netif) {
        esp_netif_destroy(sta_netif);
        sta_netif = NULL;
    }

    return ESP_OK;
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
        *connected_clients = 0;
        wifi_sta_list_t sta_list;
        if (esp_wifi_ap_get_sta_list(&sta_list) == ESP_OK) {
            *connected_clients = sta_list.num;
        }
    }
    if (ap_ip) {
        ap_ip[0] = '\0';
        if (wifi_netif) {
            esp_netif_ip_info_t ip_info;
            if (esp_netif_get_ip_info(wifi_netif, &ip_info) == ESP_OK) {
                snprintf(ap_ip, 16, IPSTR, IP2STR(&ip_info.ip));
            }
        }
        if (ap_ip[0] == '\0') {
            strcpy(ap_ip, "192.168.4.1");
        }
    }
    return ESP_OK;
}

esp_err_t wifi_config_get_sta_status(wifi_sta_status_t *status_out) {
    if (!status_out) {
        return ESP_ERR_INVALID_ARG;
    }

    status_out->enabled = sta_config.enabled;
    status_out->has_credentials = sta_runtime.has_credentials;
    status_out->connecting = sta_runtime.connecting;
    status_out->connected = sta_runtime.connected;
    strncpy(status_out->ssid, sta_config.ssid, sizeof(status_out->ssid) - 1);
    status_out->ssid[sizeof(status_out->ssid) - 1] = '\0';
    strncpy(status_out->ip, sta_runtime.ip, sizeof(status_out->ip) - 1);
    status_out->ip[sizeof(status_out->ip) - 1] = '\0';
    status_out->last_disconnect_reason = sta_runtime.last_disconnect_reason;
    strncpy(status_out->last_error, sta_runtime.last_error, sizeof(status_out->last_error) - 1);
    status_out->last_error[sizeof(status_out->last_error) - 1] = '\0';

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
