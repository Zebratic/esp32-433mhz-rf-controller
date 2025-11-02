#include <string.h>
#include <stdlib.h>
#include <sys/param.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_system.h"
#include "esp_random.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "cJSON.h"
#include "rc_switch.h"
#include "config.h"
static const char *TAG = "433MHZ_CONTROLLER";
#define MAX_SIGNALS 50
#define MAX_TRACKED_SIGNALS 10

typedef struct {
    char name[64];
    uint32_t code;
    uint8_t bit_length;
    uint8_t protocol;
    uint16_t pulse_length;
} rf_signal_t;

typedef struct {
    uint32_t code;
    uint8_t bit_length;
    uint8_t protocol;
    uint16_t pulse_length;
    uint32_t count;
    int64_t first_seen;
    int64_t last_seen;
} tracked_signal_t;

static rc_receiver_t receiver;
static rc_transmitter_t transmitter;
static rf_signal_t saved_signals[MAX_SIGNALS];
static int signal_count = 0;
static int retry_num = 0;
static tracked_signal_t tracked_signals[MAX_TRACKED_SIGNALS];
static int tracked_count = 0;
static SemaphoreHandle_t tracked_signals_mutex = NULL;
static rf_signal_t last_signal = {0};
static bool new_signal_received = false;
static uint32_t last_signal_count = 0;
static uint32_t last_valid_code = 0;
static int64_t last_valid_time = 0;
static httpd_handle_t server = NULL;
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data);
static void load_signals_from_nvs(void);
static void save_signals_to_nvs(void);
static void track_signal(uint32_t code, uint8_t bit_length, uint8_t protocol, uint16_t pulse_length);
static void cleanup_old_tracked_signals(void);
static esp_err_t index_handler(httpd_req_t *req);
static esp_err_t api_info_handler(httpd_req_t *req);
static esp_err_t api_signals_get_handler(httpd_req_t *req);
static esp_err_t api_signals_post_handler(httpd_req_t *req);
static esp_err_t api_signals_delete_handler(httpd_req_t *req);
static esp_err_t api_transmit_index_handler(httpd_req_t *req);
static esp_err_t api_transmit_name_handler(httpd_req_t *req);
static esp_err_t api_transmit_direct_handler(httpd_req_t *req);
static esp_err_t api_signal_history_handler(httpd_req_t *req);
static esp_err_t api_clear_tracking_handler(httpd_req_t *req);
static esp_err_t api_settings_handler(httpd_req_t *req);

static void wifi_init_sta(void)
{
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi initialization finished. Connecting to %s...", WIFI_SSID);
}

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (retry_num < MAX_RETRY) {
            esp_wifi_connect();
            retry_num++;
            ESP_LOGI(TAG, "Retry connecting to WiFi...");
        } else {
            ESP_LOGE(TAG, "Failed to connect to WiFi");
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP address: " IPSTR, IP2STR(&event->ip_info.ip));
        retry_num = 0;
    }
}

static void load_signals_from_nvs(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open("storage", NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "No saved signals found");
        return;
    }

    size_t required_size = 0;
    err = nvs_get_blob(handle, "signals", NULL, &required_size);
    if (err == ESP_OK && required_size > 0) {
        nvs_get_blob(handle, "signals", saved_signals, &required_size);
        signal_count = required_size / sizeof(rf_signal_t);
        ESP_LOGI(TAG, "Loaded %d signals from NVS", signal_count);
    }

    nvs_close(handle);
}

static void save_signals_to_nvs(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open("storage", NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error opening NVS handle");
        return;
    }

    err = nvs_set_blob(handle, "signals", saved_signals, signal_count * sizeof(rf_signal_t));
    if (err == ESP_OK) {
        nvs_commit(handle);
        ESP_LOGI(TAG, "Saved %d signals to NVS", signal_count);
    }

    nvs_close(handle);
}

static void track_signal(uint32_t code, uint8_t bit_length, uint8_t protocol, uint16_t pulse_length)
{
    if (tracked_signals_mutex == NULL) return;
    
    xSemaphoreTake(tracked_signals_mutex, portMAX_DELAY);
    
    int64_t now = esp_timer_get_time();
    bool found = false;
    
    for (int i = 0; i < tracked_count; i++) {
        if (tracked_signals[i].code == code && 
            tracked_signals[i].bit_length == bit_length &&
            tracked_signals[i].protocol == protocol) {
            tracked_signals[i].count++;
            tracked_signals[i].last_seen = now;
            last_signal_count = tracked_signals[i].count;
            found = true;
            break;
        }
    }
    
    if (!found) {
        if (tracked_count < MAX_TRACKED_SIGNALS) {
            tracked_signals[tracked_count].code = code;
            tracked_signals[tracked_count].bit_length = bit_length;
            tracked_signals[tracked_count].protocol = protocol;
            tracked_signals[tracked_count].pulse_length = pulse_length;
            tracked_signals[tracked_count].count = 1;
            tracked_signals[tracked_count].first_seen = now;
            tracked_signals[tracked_count].last_seen = now;
            last_signal_count = 1;
            tracked_count++;
        } else {
            int oldest_idx = 0;
            int64_t oldest_time = tracked_signals[0].last_seen;
            for (int i = 1; i < tracked_count; i++) {
                if (tracked_signals[i].last_seen < oldest_time) {
                    oldest_time = tracked_signals[i].last_seen;
                    oldest_idx = i;
                }
            }
            tracked_signals[oldest_idx].code = code;
            tracked_signals[oldest_idx].bit_length = bit_length;
            tracked_signals[oldest_idx].protocol = protocol;
            tracked_signals[oldest_idx].pulse_length = pulse_length;
            tracked_signals[oldest_idx].count = 1;
            tracked_signals[oldest_idx].first_seen = now;
            tracked_signals[oldest_idx].last_seen = now;
            last_signal_count = 1;
        }
    }
    
    xSemaphoreGive(tracked_signals_mutex);
}

static void cleanup_old_tracked_signals(void)
{
    if (tracked_signals_mutex == NULL) return;
    
    xSemaphoreTake(tracked_signals_mutex, portMAX_DELAY);
    
    int64_t now = esp_timer_get_time();
    int64_t threshold = 50000000;
    
    for (int i = tracked_count - 1; i >= 0; i--) {
        if ((now - tracked_signals[i].last_seen) > threshold) {
            for (int j = i; j < tracked_count - 1; j++) {
                tracked_signals[j] = tracked_signals[j + 1];
            }
            tracked_count--;
            ESP_LOGI(TAG, "Cleaned up old tracked signal (%d remaining)", tracked_count);
        }
    }
    
    xSemaphoreGive(tracked_signals_mutex);
}

// we clean up the tracked signals every 50 seconds to prevent the memory from filling up (this was a previous problem i had after running for a few days)
static void cleanup_task(void *arg)
{
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(50000));
        cleanup_old_tracked_signals();
    }
}


// For reference, this code was written on too much caffeine and too little sleep, so i have no clue if its actually working as i intended it to be...
static bool is_likely_noise(uint32_t code, uint8_t bitlen, uint32_t last_code, uint8_t last_bitlen, int64_t time_since_last)
{
    if (time_since_last > 1000000) return false;
    
    if (last_bitlen == 24 && time_since_last < 500000) {
        if (bitlen != 24) return true;
    }
    
    if (bitlen < last_bitlen) {
        uint32_t mask = (1UL << bitlen) - 1;
        uint32_t last_suffix = last_code & mask;
        if (code == last_suffix) return true;
        
        uint32_t xor_diff = code ^ (last_code & mask);
        int diff_bits = __builtin_popcount(xor_diff);
        if (diff_bits <= (bitlen / 10 + 1)) return true;
    }
    
    if (code == last_code && abs(bitlen - last_bitlen) > 0) return true;
    
    return false;
}

extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[]   asm("_binary_index_html_end");

static esp_err_t index_handler(httpd_req_t *req)
{
    const size_t index_html_size = (index_html_end - index_html_start);
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, (const char *)index_html_start, index_html_size);
    return ESP_OK;
}

static esp_err_t api_info_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "device", "ESP32 433MHz Controller");

    esp_netif_ip_info_t ip_info;
    esp_netif_get_ip_info(esp_netif_get_handle_from_ifkey("WIFI_STA_DEF"), &ip_info);
    char ip_str[16];
    sprintf(ip_str, IPSTR, IP2STR(&ip_info.ip));
    cJSON_AddStringToObject(root, "ip", ip_str);

    cJSON_AddNumberToObject(root, "signalCount", signal_count);
    cJSON_AddNumberToObject(root, "receiverPin", RF_RECEIVER_PIN);
    cJSON_AddNumberToObject(root, "transmitterPin", RF_TRANSMITTER_PIN);

    const char *resp = cJSON_Print(root);
    httpd_resp_sendstr(req, resp);
    free((void *)resp);
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t api_signals_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");

    cJSON *root = cJSON_CreateObject();
    cJSON *signals_array = cJSON_CreateArray();

    for (int i = 0; i < signal_count; i++) {
        cJSON *signal = cJSON_CreateObject();
        cJSON_AddStringToObject(signal, "name", saved_signals[i].name);
        cJSON_AddNumberToObject(signal, "code", saved_signals[i].code);
        cJSON_AddNumberToObject(signal, "bitLength", saved_signals[i].bit_length);
        cJSON_AddNumberToObject(signal, "protocol", saved_signals[i].protocol);
        cJSON_AddNumberToObject(signal, "pulseLength", saved_signals[i].pulse_length);
        cJSON_AddItemToArray(signals_array, signal);
    }

    cJSON_AddItemToObject(root, "signals", signals_array);

    const char *resp = cJSON_Print(root);
    httpd_resp_sendstr(req, resp);
    free((void *)resp);
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t api_signal_history_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");

    int64_t server_time = esp_timer_get_time(); // Current server time in microseconds
    
    cJSON *root = cJSON_CreateObject();
    cJSON *signals_array = cJSON_CreateArray();
    
    // Get all tracked signals
    if (tracked_signals_mutex != NULL) {
        xSemaphoreTake(tracked_signals_mutex, portMAX_DELAY);
        
        for (int i = 0; i < tracked_count; i++) {
            cJSON *signal = cJSON_CreateObject();
            cJSON_AddNumberToObject(signal, "code", tracked_signals[i].code);
            cJSON_AddNumberToObject(signal, "bitLength", tracked_signals[i].bit_length);
            cJSON_AddNumberToObject(signal, "protocol", tracked_signals[i].protocol);
            cJSON_AddNumberToObject(signal, "pulseLength", tracked_signals[i].pulse_length);
            cJSON_AddNumberToObject(signal, "count", tracked_signals[i].count);
            cJSON_AddNumberToObject(signal, "firstSeen", tracked_signals[i].first_seen);
            cJSON_AddNumberToObject(signal, "lastSeen", tracked_signals[i].last_seen);
            cJSON_AddItemToArray(signals_array, signal);
        }
        
        xSemaphoreGive(tracked_signals_mutex);
    }
    
    cJSON_AddItemToObject(root, "signals", signals_array);
    cJSON_AddNumberToObject(root, "serverTime", server_time); // Send server's current time
    
    // Add latest signal if available
    cJSON *latest = NULL;
    if (new_signal_received && last_signal.code != 0) {
        latest = cJSON_CreateObject();
        cJSON_AddNumberToObject(latest, "code", last_signal.code);
        cJSON_AddNumberToObject(latest, "bitLength", last_signal.bit_length);
        cJSON_AddNumberToObject(latest, "protocol", last_signal.protocol);
        cJSON_AddNumberToObject(latest, "pulseLength", last_signal.pulse_length);
        cJSON_AddNumberToObject(latest, "count", last_signal_count);
        cJSON_AddBoolToObject(latest, "new", true);
        cJSON_AddItemToObject(root, "latest", latest);
    } else {
        latest = cJSON_CreateObject();
        cJSON_AddBoolToObject(latest, "new", false);
        cJSON_AddItemToObject(root, "latest", latest);
    }

    const char *resp = cJSON_Print(root);
    httpd_resp_sendstr(req, resp);
    free((void *)resp);
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t api_signals_post_handler(httpd_req_t *req)
{
    char buf[512];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    cJSON *json = cJSON_Parse(buf);
    if (json == NULL) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"error\":\"Invalid JSON\"}");
        return ESP_FAIL;
    }

    // Validate required fields
    cJSON *name = cJSON_GetObjectItem(json, "name");
    cJSON *code = cJSON_GetObjectItem(json, "code");
    cJSON *bitLength = cJSON_GetObjectItem(json, "bitLength");
    cJSON *protocol = cJSON_GetObjectItem(json, "protocol");
    cJSON *pulseLength = cJSON_GetObjectItem(json, "pulseLength");

    // Validate field types and values
    if (!cJSON_IsString(name) || name->valuestring == NULL || strlen(name->valuestring) == 0) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"error\":\"Invalid or missing name\"}");
        cJSON_Delete(json);
        return ESP_FAIL;
    }

    if (!cJSON_IsNumber(code) || !cJSON_IsNumber(bitLength) ||
        !cJSON_IsNumber(protocol) || !cJSON_IsNumber(pulseLength)) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"error\":\"Invalid numeric fields\"}");
        cJSON_Delete(json);
        return ESP_FAIL;
    }

    // Validate ranges
    if (bitLength->valueint < 8 || bitLength->valueint > 64 ||
        protocol->valueint < 1 || protocol->valueint > 7) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"error\":\"Invalid bit length or protocol\"}");
        cJSON_Delete(json);
        return ESP_FAIL;
    }

    // Check for existing signals with the same name or code
    for (int i = 0; i < signal_count; i++) {
        if (strcasecmp(saved_signals[i].name, name->valuestring) == 0 ||
            saved_signals[i].code == code->valueint) {
            httpd_resp_set_type(req, "application/json");
            httpd_resp_sendstr(req, "{\"error\":\"Signal with this name or code already exists\"}");
            cJSON_Delete(json);
            return ESP_FAIL;
        }
    }

    // Check signal count limit
    if (signal_count >= MAX_SIGNALS) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"error\":\"Maximum number of signals reached\"}");
        cJSON_Delete(json);
        return ESP_FAIL;
    }

    // Save the signal
    rf_signal_t *sig = &saved_signals[signal_count];
    strncpy(sig->name, name->valuestring, sizeof(sig->name) - 1);
    sig->code = code->valueint;
    sig->bit_length = bitLength->valueint;
    sig->protocol = protocol->valueint;
    sig->pulse_length = pulseLength->valueint;
    signal_count++;

    // Persist to non-volatile storage
    save_signals_to_nvs();

    // Respond with success
    char index_str[16];
    snprintf(index_str, sizeof(index_str), "%d", signal_count - 1);
    char resp_str[128];
    snprintf(resp_str, sizeof(resp_str), "{\"success\":true,\"signalIndex\":%s}", index_str);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, resp_str);

    cJSON_Delete(json);
    return ESP_OK;
}

static esp_err_t api_signals_put_handler(httpd_req_t *req)
{
    // Extract index from URI
    char *uri = (char *)req->uri;
    char *index_str = strrchr(uri, '/');
    if (!index_str) {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }
    
    int index = atoi(index_str + 1);
    if (index < 0 || index >= signal_count) {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    char buf[512];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    cJSON *json = cJSON_Parse(buf);
    if (json == NULL) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    cJSON *name = cJSON_GetObjectItem(json, "name");
    cJSON *code = cJSON_GetObjectItem(json, "code");
    cJSON *bitLength = cJSON_GetObjectItem(json, "bitLength");
    cJSON *protocol = cJSON_GetObjectItem(json, "protocol");
    cJSON *pulseLength = cJSON_GetObjectItem(json, "pulseLength");

    if (name && code && bitLength && protocol && pulseLength) {
        strncpy(saved_signals[index].name, name->valuestring, 63);
        saved_signals[index].code = code->valueint;
        saved_signals[index].bit_length = bitLength->valueint;
        saved_signals[index].protocol = protocol->valueint;
        saved_signals[index].pulse_length = pulseLength->valueint;

        save_signals_to_nvs();

        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"success\":true}");
    } else {
        httpd_resp_send_500(req);
    }

    cJSON_Delete(json);
    return ESP_OK;
}

static esp_err_t api_signals_delete_handler(httpd_req_t *req)
{
    char *uri = (char *)req->uri;
    char *index_str = strrchr(uri, '/');
    if (index_str) {
        int index = atoi(index_str + 1);
        if (index >= 0 && index < signal_count) {
            for (int i = index; i < signal_count - 1; i++) {
                saved_signals[i] = saved_signals[i + 1];
            }
            signal_count--;
            save_signals_to_nvs();

            httpd_resp_set_type(req, "application/json");
            httpd_resp_sendstr(req, "{\"success\":true}");
            return ESP_OK;
        }
    }

    httpd_resp_send_404(req);
    return ESP_FAIL;
}

static esp_err_t api_transmit_index_handler(httpd_req_t *req)
{
    char *uri = (char *)req->uri;
    char *index_str = strrchr(uri, '/');
    if (index_str) {
        int index = atoi(index_str + 1);
        if (index >= 0 && index < signal_count) {
            rf_signal_t *sig = &saved_signals[index];

            rc_transmitter_set_protocol(&transmitter, sig->protocol - 1);
            rc_transmitter_set_pulse_length(&transmitter, sig->pulse_length);
            rc_transmitter_send(&transmitter, sig->code, sig->bit_length);

            ESP_LOGI(TAG, "Transmitted: %s (Code: %lu)", sig->name, sig->code);

            httpd_resp_set_type(req, "application/json");
            httpd_resp_sendstr(req, "{\"success\":true}");
            return ESP_OK;
        }
    }

    httpd_resp_send_404(req);
    return ESP_FAIL;
}

static esp_err_t api_transmit_name_handler(httpd_req_t *req)
{
    char *uri = (char *)req->uri;
    char *name_start = strstr(uri, "/api/transmit/name/");
    if (name_start) {
        name_start += 19;
        char decoded_name[64];
        int j = 0;
        for (int i = 0; name_start[i] && j < 63; i++) {
            if (name_start[i] == '%' && name_start[i+1] == '2' && name_start[i+2] == '0') {
                decoded_name[j++] = ' ';
                i += 2;
            } else {
                decoded_name[j++] = name_start[i];
            }
        }
        decoded_name[j] = '\0';

        for (int i = 0; i < signal_count; i++) {
            if (strcasecmp(saved_signals[i].name, decoded_name) == 0) {
                rf_signal_t *sig = &saved_signals[i];

                rc_transmitter_set_protocol(&transmitter, sig->protocol - 1);
                rc_transmitter_set_pulse_length(&transmitter, sig->pulse_length);
                rc_transmitter_send(&transmitter, sig->code, sig->bit_length);

                ESP_LOGI(TAG, "Transmitted: %s (Code: %lu)", sig->name, sig->code);

                httpd_resp_set_type(req, "application/json");
                httpd_resp_sendstr(req, "{\"success\":true}");
                return ESP_OK;
            }
        }
    }

    httpd_resp_send_404(req);
    return ESP_FAIL;
}

static esp_err_t api_transmit_direct_handler(httpd_req_t *req)
{
    char buf[512];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    cJSON *json = cJSON_Parse(buf);
    if (json == NULL) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"error\":\"Invalid JSON\"}");
        return ESP_FAIL;
    }

    // Validate required fields
    cJSON *code = cJSON_GetObjectItem(json, "code");
    cJSON *bitLength = cJSON_GetObjectItem(json, "bitLength");
    cJSON *protocol = cJSON_GetObjectItem(json, "protocol");
    cJSON *pulseLength = cJSON_GetObjectItem(json, "pulseLength");

    if (!cJSON_IsNumber(code) || !cJSON_IsNumber(bitLength) ||
        !cJSON_IsNumber(protocol) || !cJSON_IsNumber(pulseLength)) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"error\":\"Invalid numeric fields\"}");
        cJSON_Delete(json);
        return ESP_FAIL;
    }

    // Validate ranges
    if (bitLength->valueint < 8 || bitLength->valueint > 64 ||
        protocol->valueint < 1 || protocol->valueint > 7) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"error\":\"Invalid bit length or protocol\"}");
        cJSON_Delete(json);
        return ESP_FAIL;
    }

    // Transmit directly without saving
    rc_transmitter_set_protocol(&transmitter, protocol->valueint - 1);
    rc_transmitter_set_pulse_length(&transmitter, pulseLength->valueint);
    rc_transmitter_send(&transmitter, code->valueint, bitLength->valueint);

    ESP_LOGI(TAG, "Transmitted direct: Code: %lu, Protocol: %d, Bits: %d", 
             code->valueint, protocol->valueint, bitLength->valueint);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"success\":true}");

    cJSON_Delete(json);
    return ESP_OK;
}

static esp_err_t api_clear_tracking_handler(httpd_req_t *req)
{
    if (tracked_signals_mutex) {
        xSemaphoreTake(tracked_signals_mutex, portMAX_DELAY);
        tracked_count = 0;
        xSemaphoreGive(tracked_signals_mutex);
    }
    
    last_signal.code = 0;
    last_signal_count = 0;
    new_signal_received = false;
    last_valid_code = 0;
    last_valid_time = 0;
    
    ESP_LOGI(TAG, "Cleared all tracked signals and reset filter state");

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"success\":true}");
    return ESP_OK;
}

static esp_err_t api_settings_handler(httpd_req_t *req)
{
    char buf[512];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    cJSON *json = cJSON_Parse(buf);
    if (json == NULL) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"error\":\"Invalid JSON\"}");
        return ESP_FAIL;
    }

    cJSON_Delete(json);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"success\":true}");
    return ESP_OK;
}

// Middleware for serving static files for the web UI
static esp_err_t static_file_handler(httpd_req_t *req)
{
    extern const uint8_t index_html_start[] asm("_binary_index_html_start");
    extern const uint8_t index_html_end[] asm("_binary_index_html_end");

    // CSS files
    extern const uint8_t _binary_base_css_start[];
    extern const uint8_t _binary_base_css_end[];
    extern const uint8_t _binary_signals_css_start[];
    extern const uint8_t _binary_signals_css_end[];
    extern const uint8_t _binary_tabs_css_start[];
    extern const uint8_t _binary_tabs_css_end[];

    // JS files
    extern const uint8_t _binary_app_js_start[];
    extern const uint8_t _binary_app_js_end[];
    extern const uint8_t _binary_api_js_start[];
    extern const uint8_t _binary_api_js_end[];
    extern const uint8_t _binary_signals_js_start[];
    extern const uint8_t _binary_signals_js_end[];
    extern const uint8_t _binary_settings_js_start[];
    extern const uint8_t _binary_settings_js_end[];
    extern const uint8_t _binary_api_docs_js_start[];
    extern const uint8_t _binary_api_docs_js_end[];

    // Length helper function
    size_t get_binary_length(const uint8_t* start, const uint8_t* end) {
        return (start && end) ? (end - start) : 0;
    }

    // Favicon (swap out the bytes down below, if you really care about this...)
    extern const uint8_t _binary_favicon_ico_start[] __attribute__((weak));
    extern const uint8_t _binary_favicon_ico_end[] __attribute__((weak));

    // Tab files
    extern const uint8_t monitor_html_start[] asm("_binary_monitor_html_start");
    extern const uint8_t monitor_html_end[] asm("_binary_monitor_html_end");
    extern const uint8_t signals_html_start[] asm("_binary_signals_html_start");
    extern const uint8_t signals_html_end[] asm("_binary_signals_html_end");
    extern const uint8_t manual_html_start[] asm("_binary_manual_html_start");
    extern const uint8_t manual_html_end[] asm("_binary_manual_html_end");
    extern const uint8_t settings_html_start[] asm("_binary_settings_html_start");
    extern const uint8_t settings_html_end[] asm("_binary_settings_html_end");
    extern const uint8_t api_html_start[] asm("_binary_api_html_start");
    extern const uint8_t api_html_end[] asm("_binary_api_html_end");

    // Default values for the file to serve based on the URI
    const char *uri = req->uri;
    const uint8_t *start = NULL;
    const uint8_t *end = NULL;
    const char *mime_type = "text/plain";

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // Dont even comment about my huge ahhhh if else if else if else if else. It works and i dont want to touch it... //
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    if (strcmp(uri, "/") == 0 || strcmp(uri, "/index.html") == 0) {
        start = index_html_start;
        end = index_html_end;
        mime_type = "text/html";
    }
    else if (strcmp(uri, "/css/base.css") == 0) {
        size_t length = get_binary_length(_binary_base_css_start, _binary_base_css_end);
        if (length > 0) {
            start = _binary_base_css_start;
            end = _binary_base_css_end;
            mime_type = "text/css";
        }
    }
    else if (strcmp(uri, "/css/signals.css") == 0) {
        size_t length = get_binary_length(_binary_signals_css_start, _binary_signals_css_end);
        if (length > 0) {
            start = _binary_signals_css_start;
            end = _binary_signals_css_end;
            mime_type = "text/css";
        }
    }
    else if (strcmp(uri, "/css/tabs.css") == 0) {
        size_t length = get_binary_length(_binary_tabs_css_start, _binary_tabs_css_end);
        if (length > 0) {
            start = _binary_tabs_css_start;
            end = _binary_tabs_css_end;
            mime_type = "text/css";
        }
    }
    // JS files
    else if (strcmp(uri, "/js/app.js") == 0) {
        size_t length = get_binary_length(_binary_app_js_start, _binary_app_js_end);
        if (length > 0) {
            start = _binary_app_js_start;
            end = _binary_app_js_end;
            mime_type = "application/javascript";
        }
    }
    else if (strcmp(uri, "/js/api.js") == 0) {
        size_t length = get_binary_length(_binary_api_js_start, _binary_api_js_end);
        if (length > 0) {
            start = _binary_api_js_start;
            end = _binary_api_js_end;
            mime_type = "application/javascript";
        }
    }
    else if (strcmp(uri, "/js/signals.js") == 0) {
        size_t length = get_binary_length(_binary_signals_js_start, _binary_signals_js_end);
        if (length > 0) {
            start = _binary_signals_js_start;
            end = _binary_signals_js_end;
            mime_type = "application/javascript";
        }
    }
    else if (strcmp(uri, "/js/settings.js") == 0) {
        size_t length = get_binary_length(_binary_settings_js_start, _binary_settings_js_end);
        if (length > 0) {
            start = _binary_settings_js_start;
            end = _binary_settings_js_end;
            mime_type = "application/javascript";
        }
    }
    else if (strcmp(uri, "/js/api-docs.js") == 0) {
        size_t length = get_binary_length(_binary_api_docs_js_start, _binary_api_docs_js_end);
        if (length > 0) {
            start = _binary_api_docs_js_start;
            end = _binary_api_docs_js_end;
            mime_type = "application/javascript";
        }
    }
    // Tab files
    else if (strcmp(uri, "/tabs/monitor.html") == 0) {
        size_t length = get_binary_length(monitor_html_start, monitor_html_end);
        if (length > 0) {
            start = monitor_html_start;
            end = monitor_html_end;
            mime_type = "text/html";
        }
    }
    else if (strcmp(uri, "/tabs/signals.html") == 0) {
        size_t length = get_binary_length(signals_html_start, signals_html_end);
        if (length > 0) {
            start = signals_html_start;
            end = signals_html_end;
            mime_type = "text/html";
        }
    }
    else if (strcmp(uri, "/tabs/manual.html") == 0) {
        size_t length = get_binary_length(manual_html_start, manual_html_end);
        if (length > 0) {
            start = manual_html_start;
            end = manual_html_end;
            mime_type = "text/html";
        }
    }
    else if (strcmp(uri, "/tabs/settings.html") == 0) {
        size_t length = get_binary_length(settings_html_start, settings_html_end);
        if (length > 0) {
            start = settings_html_start;
            end = settings_html_end;
            mime_type = "text/html";
        }
    }
    else if (strcmp(uri, "/tabs/api.html") == 0) {
        size_t length = get_binary_length(api_html_start, api_html_end);
        if (length > 0) {
            start = api_html_start;
            end = api_html_end;
            mime_type = "text/html";
        }
    }
    // Favicon handling
    else if (strcmp(uri, "/favicon.ico") == 0) {
        // Go ahead and slap in a nice icon for the favicon if you want to :shrug:
        static const char favicon_data[] = {
            0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x10, 0x10, 0x00, 0x00, 0x01, 0x00,
            0x20, 0x00, 0x68, 0x05, 0x00, 0x00, 0x16, 0x00, 0x00, 0x00
        };
        httpd_resp_set_type(req, "image/x-icon");
        httpd_resp_send(req, favicon_data, sizeof(favicon_data));
        return ESP_OK;
    }
    else {
        httpd_resp_set_status(req, "404 Not Found");
        httpd_resp_set_type(req, "text/plain");
        httpd_resp_send(req, "File not found", 14);
        return ESP_OK;
    }

    // Validate start and end pointers before serving
    if (!start || !end) {
        ESP_LOGE(TAG, "Failed to serve resource: %s - start or end pointer is NULL :( i guess its the end of the world now...", uri);
        httpd_resp_set_status(req, "404 Not Found");
        httpd_resp_set_type(req, "text/plain");
        httpd_resp_send(req, "Resource not found", 18);
        return ESP_FAIL;
    }

    // Calculate length and validate
    size_t resource_length = end - start;
    if (resource_length == 0) {
        ESP_LOGE(TAG, "Failed to serve resource: %s - zero-length content", uri);
        httpd_resp_set_status(req, "404 Not Found");
        httpd_resp_set_type(req, "text/plain");
        httpd_resp_send(req, "Empty resource", 14);
        return ESP_FAIL;
    }

    // Send the file
    httpd_resp_set_type(req, mime_type);
    httpd_resp_send(req, (const char*)start, resource_length);

    ESP_LOGI(TAG, "Successfully served resource: %s (length: %zu bytes)", uri, resource_length);
    return ESP_OK;
}


// Starts the HTTP server and registers the API handlers
static httpd_handle_t start_webserver(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 32;
    config.uri_match_fn = httpd_uri_match_wildcard;

    ESP_LOGI(TAG, "Starting HTTP server");
    if (httpd_start(&server, &config) == ESP_OK) {
        // Register API handlers FIRST (before wildcard static handler)
        httpd_uri_t api_info_uri = {
            .uri       = "/api/info",
            .method    = HTTP_GET,
            .handler   = api_info_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &api_info_uri);

        httpd_uri_t api_signals_get_uri = {
            .uri       = "/api/signals",
            .method    = HTTP_GET,
            .handler   = api_signals_get_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &api_signals_get_uri);

        httpd_uri_t api_signal_history_uri = {
            .uri       = "/api/signal-history",
            .method    = HTTP_GET,
            .handler   = api_signal_history_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &api_signal_history_uri);

        httpd_uri_t api_signals_post_uri = {
            .uri       = "/api/signals",
            .method    = HTTP_POST,
            .handler   = api_signals_post_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &api_signals_post_uri);

        httpd_uri_t api_signals_put_uri = {
            .uri       = "/api/signals/*",
            .method    = HTTP_PUT,
            .handler   = api_signals_put_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &api_signals_put_uri);

        httpd_uri_t api_signals_delete_uri = {
            .uri       = "/api/signals/*",
            .method    = HTTP_DELETE,
            .handler   = api_signals_delete_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &api_signals_delete_uri);

        httpd_uri_t api_transmit_index_uri = {
            .uri       = "/api/transmit/*",
            .method    = HTTP_POST,
            .handler   = api_transmit_index_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &api_transmit_index_uri);

        httpd_uri_t api_transmit_name_uri = {
            .uri       = "/api/transmit/name/*",
            .method    = HTTP_POST,
            .handler   = api_transmit_name_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &api_transmit_name_uri);

        httpd_uri_t api_transmit_direct_uri = {
            .uri       = "/api/transmit",
            .method    = HTTP_POST,
            .handler   = api_transmit_direct_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &api_transmit_direct_uri);

        httpd_uri_t api_clear_tracking_uri = {
            .uri       = "/api/clear-tracking",
            .method    = HTTP_POST,
            .handler   = api_clear_tracking_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &api_clear_tracking_uri);

        httpd_uri_t api_settings_uri = {
            .uri       = "/api/settings",
            .method    = HTTP_POST,
            .handler   = api_settings_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &api_settings_uri);

        // Well i took too long to debug this, it just so happened to be because i didnt register the static file handler last, so now its here...
        // -1 hour debugging session saved for future reference...
        httpd_uri_t static_file_uri = {
            .uri       = "/*",
            .method    = HTTP_GET,
            .handler   = static_file_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &static_file_uri);

        return server;
    }

    ESP_LOGE(TAG, "Failed to start HTTP server");
    return NULL;
}

static void rf_monitor_task(void *arg)
{
    ESP_LOGI(TAG, "RF monitor task started");
    ESP_LOGI(TAG, "Starting continuous RF monitoring with verbose logging...");
    ESP_LOGI(TAG, "Will report ANY signal activity detected on GPIO%d", RF_RECEIVER_PIN);
    ESP_LOGI(TAG, "Press any button on your 433MHz remote to test...");
    ESP_LOGI(TAG, "");
    
    uint32_t heartbeat_counter = 0;
    uint32_t last_isr_count = 0;

    while (1) {
        if (heartbeat_counter % 1000 == 0) {
            uint32_t current_isr_count = rc_receiver_get_isr_count();
            uint32_t isr_delta = current_isr_count - last_isr_count;
            
            ESP_LOGI(TAG, "[HEARTBEAT] RF Monitor active | ISR triggers: %lu total (%lu in last 10s)", 
                     current_isr_count, isr_delta);
            
            if (isr_delta == 0 && heartbeat_counter > 0) {
                ESP_LOGW(TAG, "⚠ WARNING: No ISR triggers detected! (You might be cooked?)");
                ESP_LOGW(TAG, "  This means the receiver pin is NOT changing state at all.");
                ESP_LOGW(TAG, "  Possible issues:");
                ESP_LOGW(TAG, "    1. Wrong DATA pin connected (try the other data pin)");
                ESP_LOGW(TAG, "    2. Receiver not powered (check VCC/GND connections)");
                ESP_LOGW(TAG, "    3. Faulty receiver module (try a different one)");
                ESP_LOGW(TAG, "    4. No RF signals in range (try pressing a remote button)");
            }
            
            last_isr_count = current_isr_count;
        }
        heartbeat_counter++;

        if (rc_receiver_available(&receiver)) {
            uint32_t code = rc_receiver_get_value(&receiver);
            uint8_t bitlen = rc_receiver_get_bitlength(&receiver);
            uint8_t protocol = rc_receiver_get_protocol(&receiver);
            uint16_t pulse = rc_receiver_get_delay(&receiver);

            if (code != 0 && bitlen >= 12) {
                int64_t now = esp_timer_get_time();
                int64_t time_since_last = now - last_valid_time;
                bool is_noise = is_likely_noise(code, bitlen, last_valid_code, last_signal.bit_length, time_since_last);
                
                if (!is_noise) {
                    track_signal(code, bitlen, protocol, pulse);
                    ESP_LOGI(TAG, "RF: Code=%lu (0x%lX) | Bits=%d | Proto=%d | Pulse=%dµs | Count=%lu",
                             code, code, bitlen, protocol, pulse, last_signal_count);

                    last_signal.code = code;
                    last_signal.bit_length = bitlen;
                    last_signal.protocol = protocol;
                    last_signal.pulse_length = pulse;
                    new_signal_received = true;
                    last_valid_code = code;
                    last_valid_time = now;
                } else {
                    ESP_LOGD(TAG, "Filtered noise: Code=%lu (0x%lX) | Bits=%d (partial of 0x%lX)",
                             code, code, bitlen, last_valid_code);
                }
            }

            rc_receiver_reset(&receiver);
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== ESP32 433MHz Controller ===");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    tracked_signals_mutex = xSemaphoreCreateMutex();
    if (tracked_signals_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init_sta();
    vTaskDelay(pdMS_TO_TICKS(5000));

    ESP_LOGI(TAG, "Initializing RF modules...");
    rc_transmitter_init(&transmitter, RF_TRANSMITTER_PIN);
    rc_transmitter_set_repeat(&transmitter, 5);
    rc_receiver_init(&receiver, RF_RECEIVER_PIN);
    ESP_LOGI(TAG, "RF modules initialized");

    load_signals_from_nvs();
    server = start_webserver();

    xTaskCreate(rf_monitor_task, "rf_monitor", 4096, NULL, 5, NULL);
    xTaskCreate(cleanup_task, "cleanup", 2048, NULL, 3, NULL);

    ESP_LOGI(TAG, "===============================================================");
    esp_netif_ip_info_t ip_info;
    esp_err_t ip_ret = esp_netif_get_ip_info(esp_netif_get_handle_from_ifkey("WIFI_STA_DEF"), &ip_info);
    ESP_LOGI(TAG, "Setup complete! RF monitor is active.");
    if (ip_ret == ESP_OK) {
        ESP_LOGI(TAG, "Web UI is now available! Access at http://" IPSTR, IP2STR(&ip_info.ip));
    } else {
        ESP_LOGI(TAG, "Web UI is now available, but failed to get device IP address.");
    }
    ESP_LOGI(TAG, "===============================================================");
}
