#include "net_mqtt.h"
#include "mqtt_client.h"
#include "esp_log.h"
#include "string.h"
#include "stdlib.h"
#include "stdio.h"

// TODO: 修改成你的 MQTT Broker IP 地址
#define MQTT_BROKER_URL     "mqtt://192.168.1.100"
#define DEVICE_ID           "esp32_01"
#define TOPIC_SENSORS       "device/esp32_01/sensors"
#define TOPIC_COMMANDS      "device/esp32_01/commands"
#define TOPIC_HEARTBEAT     "device/esp32_01/heartbeat"

static const char *TAG = "NET_MQTT";
static esp_mqtt_client_handle_t s_client = NULL;
static net_mqtt_cmd_cb_t s_cmd_cb = NULL;

/**
 * @brief Lightweight JSON angle parser.
 *        Searches for the first occurrence of "angle" followed by a number.
 */
static int parse_angle_from_json(const char *data, int len)
{
    char buf[256];
    if (len >= sizeof(buf)) len = sizeof(buf) - 1;
    memcpy(buf, data, len);
    buf[len] = '\0';

    char *p = strstr(buf, "\"angle\"");
    if (!p) return -1;

    p += 7; // skip "angle"
    while (*p && (*p != '-') && (*p < '0' || *p > '9')) p++;
    if (!*p) return -1;

    return atoi(p);
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;
    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
            esp_mqtt_client_subscribe(s_client, TOPIC_COMMANDS, 1);
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
            break;
        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_PUBLISHED:
            ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_DATA: {
            ESP_LOGI(TAG, "MQTT_EVENT_DATA");
            ESP_LOGI(TAG, "TOPIC=%.*s", event->topic_len, event->topic);
            ESP_LOGI(TAG, "DATA=%.*s", event->data_len, event->data);

            if (strncmp(event->topic, TOPIC_COMMANDS, event->topic_len) == 0) {
                int angle = parse_angle_from_json(event->data, event->data_len);
                if (angle >= 0 && angle <= 180 && s_cmd_cb) {
                    s_cmd_cb(angle);
                    ESP_LOGI(TAG, "Command parsed: angle=%d", angle);
                }
            }
            break;
        }
        case MQTT_EVENT_ERROR:
            ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
            break;
        default:
            break;
    }
}

hal_err_t net_mqtt_init(void)
{
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = MQTT_BROKER_URL,
        .credentials.client_id = DEVICE_ID,
    };

    s_client = esp_mqtt_client_init(&mqtt_cfg);
    if (!s_client) {
        return HAL_ERR;
    }
    esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(s_client);
    return HAL_OK;
}

hal_err_t net_mqtt_register_cmd_callback(net_mqtt_cmd_cb_t cb)
{
    s_cmd_cb = cb;
    return HAL_OK;
}

hal_err_t net_mqtt_publish_sensor(int angle, uint8_t r, uint8_t g, uint8_t b)
{
    if (!s_client) return HAL_ERR_NOT_INIT;

    char payload[128];
    snprintf(payload, sizeof(payload),
             "{\"device_id\":\"%s\",\"angle\":%d,\"r\":%u,\"g\":%u,\"b\":%u}",
             DEVICE_ID, angle, r, g, b);

    esp_mqtt_client_publish(s_client, TOPIC_SENSORS, payload, 0, 0, 0);
    return HAL_OK;
}

hal_err_t net_mqtt_publish_heartbeat(void)
{
    if (!s_client) return HAL_ERR_NOT_INIT;

    char payload[64];
    snprintf(payload, sizeof(payload),
             "{\"device_id\":\"%s\",\"status\":\"online\"}",
             DEVICE_ID);

    esp_mqtt_client_publish(s_client, TOPIC_HEARTBEAT, payload, 0, 0, 0);
    return HAL_OK;
}
