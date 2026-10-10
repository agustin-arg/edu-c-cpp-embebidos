#include "mqtt_manager.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_event.h"
#include "mqtt_client.h"
#include "cJSON.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "wifi_manager.h"

static const char *TAG = "MQTT_MGR";

static esp_mqtt_client_handle_t s_client = NULL;
static bool s_is_connected = false;

static mqtt_command_handler_t s_cmd_handler = NULL;
static mqtt_state_getter_t    s_state_getter = NULL;

static char s_client_id[32] = {0};
static char s_node_id[32] = {0};
static char s_discovery_topic[96] = {0};
static char s_switch_discovery_topic[96] = {0};

// ==============================================================================
// 1. HELPERS DE CONVERSIÓN DE ESTADO A STRINGS HOME ASSISTANT
// ==============================================================================

static const char *ac_mode_to_ha_str(const ac_state_t *state)
{
    if (!state->power) {
        return "off";
    }
    switch (state->mode) {
        case AC_MODE_COOL: return "cool";
        case AC_MODE_HEAT: return "heat";
        case AC_MODE_DRY:  return "dry";
        case AC_MODE_FAN:  return "fan_only";
        case AC_MODE_AUTO: return "auto";
        default:           return "cool";
    }
}

static const char *ac_fan_to_ha_str(ac_fan_t fan)
{
    switch (fan) {
        case AC_FAN_AUTO: return "auto";
        case AC_FAN_LOW:  return "low";
        case AC_FAN_MED:  return "medium";
        case AC_FAN_HIGH: return "high";
        default:          return "auto";
    }
}

// ==============================================================================
// 2. PARSEO DE COMANDOS ENTRANTES
// ==============================================================================

static void trim_whitespace(char *str)
{
    if (!str) return;
    char *end;
    // Trim leading
    while (isspace((unsigned char)*str)) str++;
    if (*str == 0) return;
    // Trim trailing
    end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    end[1] = '\0';
}

static void handle_incoming_command(const char *topic, const char *data)
{
    if (!s_state_getter || !s_cmd_handler) {
        ESP_LOGW(TAG, "Callbacks no configurados en mqtt_manager.");
        return;
    }

    ac_state_t target_state;
    s_state_getter(&target_state);
    ac_cmd_t cmd = AC_CMD_TEMP;
    bool valid = false;

    char data_clean[128];
    strncpy(data_clean, data, sizeof(data_clean) - 1);
    data_clean[sizeof(data_clean) - 1] = '\0';
    trim_whitespace(data_clean);

    ESP_LOGI(TAG, "Mensaje MQTT recibido -> Tópico: '%s' | Payload: '%s'", topic, data_clean);

    // 1. Comando de Modo: aire/living/set/mode
    if (strstr(topic, "/set/mode") != NULL) {
        if (strcasecmp(data_clean, "off") == 0) {
            target_state.power = false;
            cmd = AC_CMD_POWER;
            valid = true;
        } else if (strcasecmp(data_clean, "cool") == 0) {
            target_state.power = true;
            target_state.mode = AC_MODE_COOL;
            cmd = AC_CMD_MODE;
            valid = true;
        } else if (strcasecmp(data_clean, "heat") == 0) {
            target_state.power = true;
            target_state.mode = AC_MODE_HEAT;
            cmd = AC_CMD_MODE;
            valid = true;
        } else if (strcasecmp(data_clean, "dry") == 0) {
            target_state.power = true;
            target_state.mode = AC_MODE_DRY;
            cmd = AC_CMD_MODE;
            valid = true;
        } else if (strcasecmp(data_clean, "fan_only") == 0 || strcasecmp(data_clean, "fan") == 0) {
            target_state.power = true;
            target_state.mode = AC_MODE_FAN;
            cmd = AC_CMD_MODE;
            valid = true;
        } else if (strcasecmp(data_clean, "auto") == 0) {
            target_state.power = true;
            target_state.mode = AC_MODE_AUTO;
            cmd = AC_CMD_MODE;
            valid = true;
        } else {
            ESP_LOGW(TAG, "Modo desconocido: '%s'", data_clean);
        }
    }
    // 2. Comando de Temperatura: aire/living/set/temp
    else if (strstr(topic, "/set/temp") != NULL) {
        int temp = atoi(data_clean);
        if (temp >= DG11J1_TEMP_MIN && temp <= DG11J1_TEMP_MAX) {
            target_state.temp_c = (uint8_t)temp;
            cmd = AC_CMD_TEMP;
            valid = true;
        } else {
            ESP_LOGW(TAG, "Temperatura fuera de rango (16-30): %d", temp);
        }
    }
    // 3. Comando de Velocidad Fan: aire/living/set/fan
    else if (strstr(topic, "/set/fan") != NULL) {
        if (strcasecmp(data_clean, "auto") == 0) {
            target_state.fan = AC_FAN_AUTO;
            cmd = AC_CMD_FAN;
            valid = true;
        } else if (strcasecmp(data_clean, "low") == 0) {
            target_state.fan = AC_FAN_LOW;
            cmd = AC_CMD_FAN;
            valid = true;
        } else if (strcasecmp(data_clean, "medium") == 0 || strcasecmp(data_clean, "med") == 0) {
            target_state.fan = AC_FAN_MED;
            cmd = AC_CMD_FAN;
            valid = true;
        } else if (strcasecmp(data_clean, "high") == 0) {
            target_state.fan = AC_FAN_HIGH;
            cmd = AC_CMD_FAN;
            valid = true;
        } else {
            ESP_LOGW(TAG, "Velocidad fan desconocida: '%s'", data_clean);
        }
    }
    // 4. Comando de Swing: aire/living/set/swing
    else if (strstr(topic, "/set/swing") != NULL) {
        if (strcasecmp(data_clean, "on") == 0 || strcmp(data_clean, "1") == 0) {
            target_state.swing = true;
            cmd = AC_CMD_SWING;
            valid = true;
        } else if (strcasecmp(data_clean, "off") == 0 || strcmp(data_clean, "0") == 0) {
            target_state.swing = false;
            cmd = AC_CMD_SWING;
            valid = true;
        } else {
            ESP_LOGW(TAG, "Comando swing desconocido: '%s'", data_clean);
        }
    }
    // 5. Comando de Encendido/Apagado: aire/living/set/power
    else if (strstr(topic, "/set/power") != NULL) {
        if (strcasecmp(data_clean, "on") == 0 || strcmp(data_clean, "1") == 0) {
            target_state.power = true;
            cmd = AC_CMD_POWER;
            valid = true;
        } else if (strcasecmp(data_clean, "off") == 0 || strcmp(data_clean, "0") == 0) {
            target_state.power = false;
            cmd = AC_CMD_POWER;
            valid = true;
        } else if (strcasecmp(data_clean, "toggle") == 0) {
            target_state.power = !target_state.power;
            cmd = AC_CMD_POWER;
            valid = true;
        }
    }
    // 6. Comando Objeto JSON Consolidado: aire/living/set/json
    else if (strstr(topic, "/set/json") != NULL) {
        cJSON *root = cJSON_Parse(data_clean);
        if (root) {
            cJSON *item = NULL;
            if ((item = cJSON_GetObjectItem(root, "power")) != NULL) {
                if (cJSON_IsBool(item)) target_state.power = cJSON_IsTrue(item);
                else if (cJSON_IsString(item)) target_state.power = (strcasecmp(item->valuestring, "on") == 0);
                cmd = AC_CMD_POWER;
                valid = true;
            }
            if ((item = cJSON_GetObjectItem(root, "mode")) != NULL && cJSON_IsString(item)) {
                if (strcasecmp(item->valuestring, "off") == 0) {
                    target_state.power = false;
                    cmd = AC_CMD_POWER;
                } else {
                    target_state.power = true;
                    if (strcasecmp(item->valuestring, "cool") == 0) target_state.mode = AC_MODE_COOL;
                    else if (strcasecmp(item->valuestring, "heat") == 0) target_state.mode = AC_MODE_HEAT;
                    else if (strcasecmp(item->valuestring, "dry") == 0) target_state.mode = AC_MODE_DRY;
                    else if (strcasecmp(item->valuestring, "fan_only") == 0 || strcasecmp(item->valuestring, "fan") == 0) target_state.mode = AC_MODE_FAN;
                    else if (strcasecmp(item->valuestring, "auto") == 0) target_state.mode = AC_MODE_AUTO;
                    cmd = AC_CMD_MODE;
                }
                valid = true;
            }
            if ((item = cJSON_GetObjectItem(root, "temperature")) != NULL && cJSON_IsNumber(item)) {
                int temp = item->valueint;
                if (temp >= DG11J1_TEMP_MIN && temp <= DG11J1_TEMP_MAX) {
                    target_state.temp_c = (uint8_t)temp;
                    cmd = AC_CMD_TEMP;
                    valid = true;
                }
            }
            if ((item = cJSON_GetObjectItem(root, "fan_mode")) != NULL && cJSON_IsString(item)) {
                if (strcasecmp(item->valuestring, "auto") == 0) target_state.fan = AC_FAN_AUTO;
                else if (strcasecmp(item->valuestring, "low") == 0) target_state.fan = AC_FAN_LOW;
                else if (strcasecmp(item->valuestring, "medium") == 0) target_state.fan = AC_FAN_MED;
                else if (strcasecmp(item->valuestring, "high") == 0) target_state.fan = AC_FAN_HIGH;
                cmd = AC_CMD_FAN;
                valid = true;
            }
            if ((item = cJSON_GetObjectItem(root, "swing_mode")) != NULL && cJSON_IsString(item)) {
                target_state.swing = (strcasecmp(item->valuestring, "on") == 0);
                cmd = AC_CMD_SWING;
                valid = true;
            }
            cJSON_Delete(root);
        } else {
            ESP_LOGE(TAG, "Error parseando payload JSON: '%s'", data_clean);
        }
    }
    // 7. Retransmisión Forzada del Estado Actual: aire/living/set o aire/living/set/send
    else if (strstr(topic, "/set") != NULL) {
        cmd = AC_CMD_TEMP;
        valid = true;
    }

    if (valid) {
        ESP_LOGI(TAG, "Directiva MQTT válida. Despachando a controlador principal.");
        s_cmd_handler(&target_state, cmd);
    }
}

// ==============================================================================
// 3. HANDLER DE EVENTOS DE ESP-MQTT
// ==============================================================================

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            s_is_connected = true;
            ESP_LOGI(TAG, ">>> ¡CONECTADO AL BROKER MOSQUITTO MQTT! (%s) <<<", MQTT_BROKER_URI);

            // 1. Reportar disponibilidad inmediata (online, retain=true)
            esp_mqtt_client_publish(s_client, MQTT_TOPIC_AVAIL, "online", 0, 1, 1);

            // 2. Suscribirse a los tópicos de control
            esp_mqtt_client_subscribe(s_client, MQTT_TOPIC_SET_SUB, 1);
            ESP_LOGI(TAG, "Suscrito a tópico de órdenes: '%s'", MQTT_TOPIC_SET_SUB);

            // 3. Publicar payload de Home Assistant MQTT Discovery (Climate y Switch)
            mqtt_manager_publish_discovery();

            // NOTA: Se evita emitir el estado por defecto al conectar para no reiniciar
            // los valores configurados en Home Assistant ni sobreescribir el estado previo.
            break;

        case MQTT_EVENT_DISCONNECTED:
            s_is_connected = false;
            ESP_LOGW(TAG, "Desconectado del broker Mosquitto.");
            break;

        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGD(TAG, "Suscripción confirmada (msg_id=%d)", event->msg_id);
            break;

        case MQTT_EVENT_DATA: {
            char topic_buf[128];
            char data_buf[256];

            int t_len = event->topic_len < (sizeof(topic_buf) - 1) ? event->topic_len : (sizeof(topic_buf) - 1);
            int d_len = event->data_len < (sizeof(data_buf) - 1) ? event->data_len : (sizeof(data_buf) - 1);

            memcpy(topic_buf, event->topic, t_len);
            topic_buf[t_len] = '\0';

            memcpy(data_buf, event->data, d_len);
            data_buf[d_len] = '\0';

            handle_incoming_command(topic_buf, data_buf);
            break;
        }

        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "Error en cliente MQTT");
            if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
                ESP_LOGE(TAG, "Error en transporte TCP: 0x%x", event->error_handle->esp_tls_last_esp_err);
            }
            break;

        default:
            break;
    }
}

// ==============================================================================
// 4. FUNCIONES PÚBLICAS DE LA API
// ==============================================================================

esp_err_t mqtt_manager_init(mqtt_command_handler_t cmd_handler, mqtt_state_getter_t state_getter)
{
    s_cmd_handler = cmd_handler;
    s_state_getter = state_getter;

    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(s_client_id, sizeof(s_client_id), "marshall_ac_%02X%02X", mac[4], mac[5]);
    snprintf(s_node_id, sizeof(s_node_id), "aire_marshall_%02X%02X", mac[4], mac[5]);
    snprintf(s_discovery_topic, sizeof(s_discovery_topic), "homeassistant/climate/%s/config", s_node_id);
    snprintf(s_switch_discovery_topic, sizeof(s_switch_discovery_topic), "homeassistant/switch/%s_power/config", s_node_id);

    ESP_LOGI(TAG, "Configurando MQTT Client ID: %s | Node ID: %s", s_client_id, s_node_id);

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker = {
            .address = {
                .uri = MQTT_BROKER_URI,
            },
        },
        .credentials = {
            .client_id = s_client_id,
        },
        .session = {
            .last_will = {
                .topic = MQTT_TOPIC_AVAIL,
                .msg = "offline",
                .msg_len = 7,
                .qos = 1,
                .retain = 1,
            },
            .keepalive = 60,
        },
        .network = {
            .reconnect_timeout_ms = 5000,
        },
    };

    s_client = esp_mqtt_client_init(&mqtt_cfg);
    if (!s_client) {
        ESP_LOGE(TAG, "Error inicializando esp_mqtt_client");
        return ESP_FAIL;
    }

    ESP_ERROR_CHECK(esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL));

    ESP_LOGI(TAG, "Módulo MQTT inicializado con éxito.");
    return ESP_OK;
}

esp_err_t mqtt_manager_start(void)
{
    if (!s_client) {
        return ESP_ERR_INVALID_STATE;
    }
    return esp_mqtt_client_start(s_client);
}

esp_err_t mqtt_manager_stop(void)
{
    if (!s_client) {
        return ESP_ERR_INVALID_STATE;
    }
    s_is_connected = false;
    return esp_mqtt_client_stop(s_client);
}

bool mqtt_manager_is_connected(void)
{
    return s_is_connected;
}

esp_err_t mqtt_manager_publish_state(const ac_state_t *state)
{
    if (!s_client || !s_is_connected || !state) {
        return ESP_ERR_INVALID_STATE;
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return ESP_ERR_NO_MEM;
    }

    cJSON_AddStringToObject(root, "power", state->power ? "ON" : "OFF");
    cJSON_AddStringToObject(root, "mode", ac_mode_to_ha_str(state));
    cJSON_AddNumberToObject(root, "temperature", state->temp_c);
    cJSON_AddStringToObject(root, "fan_mode", ac_fan_to_ha_str(state->fan));
    cJSON_AddStringToObject(root, "swing_mode", state->swing ? "on" : "off");
    cJSON_AddStringToObject(root, "light", state->light ? "ON" : "OFF");

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (!json_str) {
        return ESP_ERR_NO_MEM;
    }

    int msg_id = esp_mqtt_client_publish(s_client, MQTT_TOPIC_STATUS, json_str, 0, 1, 1);
    ESP_LOGI(TAG, "[MQTT PUB] Estado reportado a '%s' (msg_id=%d): %s", MQTT_TOPIC_STATUS, msg_id, json_str);
    free(json_str);

    return ESP_OK;
}

esp_err_t mqtt_manager_publish_discovery(void)
{
    if (!s_client || !s_is_connected) {
        return ESP_ERR_INVALID_STATE;
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return ESP_ERR_NO_MEM;
    }

    cJSON_AddStringToObject(root, "name", "Aire Marshall Living");
    cJSON_AddStringToObject(root, "unique_id", s_node_id);

    // Dispositivo
    cJSON *dev = cJSON_CreateObject();
    cJSON *ids = cJSON_CreateArray();
    cJSON_AddItemToArray(ids, cJSON_CreateString(s_node_id));
    cJSON_AddItemToObject(dev, "identifiers", ids);
    cJSON_AddStringToObject(dev, "name", "Controlador Split Marshall");
    cJSON_AddStringToObject(dev, "model", "ESP32-C3 SuperMini (DG11J1-01)");
    cJSON_AddStringToObject(dev, "manufacturer", "Marshall / ESP-IDF");
    cJSON_AddStringToObject(dev, "sw_version", "1.0.0");
    cJSON_AddItemToObject(root, "device", dev);

    // Disponibilidad
    cJSON_AddStringToObject(root, "availability_topic", MQTT_TOPIC_AVAIL);
    cJSON_AddStringToObject(root, "payload_available", "online");
    cJSON_AddStringToObject(root, "payload_not_available", "offline");

    // Modo
    cJSON_AddStringToObject(root, "mode_state_topic", MQTT_TOPIC_STATUS);
    cJSON_AddStringToObject(root, "mode_state_template", "{{ value_json.mode }}");
    cJSON_AddStringToObject(root, "mode_command_topic", MQTT_TOPIC_BASE "/set/mode");

    cJSON *modes = cJSON_CreateArray();
    cJSON_AddItemToArray(modes, cJSON_CreateString("off"));
    cJSON_AddItemToArray(modes, cJSON_CreateString("cool"));
    cJSON_AddItemToArray(modes, cJSON_CreateString("heat"));
    cJSON_AddItemToArray(modes, cJSON_CreateString("dry"));
    cJSON_AddItemToArray(modes, cJSON_CreateString("fan_only"));
    cJSON_AddItemToArray(modes, cJSON_CreateString("auto"));
    cJSON_AddItemToObject(root, "modes", modes);

    // Temperatura
    cJSON_AddStringToObject(root, "temperature_state_topic", MQTT_TOPIC_STATUS);
    cJSON_AddStringToObject(root, "temperature_state_template", "{{ value_json.temperature }}");
    cJSON_AddStringToObject(root, "temperature_command_topic", MQTT_TOPIC_BASE "/set/temp");
    cJSON_AddNumberToObject(root, "min_temp", DG11J1_TEMP_MIN);
    cJSON_AddNumberToObject(root, "max_temp", DG11J1_TEMP_MAX);
    cJSON_AddNumberToObject(root, "temp_step", 1);
    cJSON_AddStringToObject(root, "temperature_unit", "C");

    // Velocidad Fan
    cJSON_AddStringToObject(root, "fan_mode_state_topic", MQTT_TOPIC_STATUS);
    cJSON_AddStringToObject(root, "fan_mode_state_template", "{{ value_json.fan_mode }}");
    cJSON_AddStringToObject(root, "fan_mode_command_topic", MQTT_TOPIC_BASE "/set/fan");

    cJSON *fan_modes = cJSON_CreateArray();
    cJSON_AddItemToArray(fan_modes, cJSON_CreateString("auto"));
    cJSON_AddItemToArray(fan_modes, cJSON_CreateString("low"));
    cJSON_AddItemToArray(fan_modes, cJSON_CreateString("medium"));
    cJSON_AddItemToArray(fan_modes, cJSON_CreateString("high"));
    cJSON_AddItemToObject(root, "fan_modes", fan_modes);

    // Swing
    cJSON_AddStringToObject(root, "swing_mode_state_topic", MQTT_TOPIC_STATUS);
    cJSON_AddStringToObject(root, "swing_mode_state_template", "{{ value_json.swing_mode }}");
    cJSON_AddStringToObject(root, "swing_mode_command_topic", MQTT_TOPIC_BASE "/set/swing");

    cJSON *swing_modes = cJSON_CreateArray();
    cJSON_AddItemToArray(swing_modes, cJSON_CreateString("off"));
    cJSON_AddItemToArray(swing_modes, cJSON_CreateString("on"));
    cJSON_AddItemToObject(root, "swing_modes", swing_modes);

    // Acción activa (cooling, heating, off, etc.)
    cJSON_AddStringToObject(root, "action_topic", MQTT_TOPIC_STATUS);
    cJSON_AddStringToObject(root, "action_template",
        "{{ 'off' if value_json.mode == 'off' else ('cooling' if value_json.mode == 'cool' else ('heating' if value_json.mode == 'heat' else ('drying' if value_json.mode == 'dry' else ('fan' if value_json.mode == 'fan_only' else 'idle')))) }}");

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (!json_str) {
        return ESP_ERR_NO_MEM;
    }

    int msg_id = esp_mqtt_client_publish(s_client, s_discovery_topic, json_str, 0, 1, 1);
    ESP_LOGI(TAG, ">>> [MQTT DISCOVERY] Clima publicado en '%s' (msg_id=%d, len=%d bytes, retain=true) <<<",
             s_discovery_topic, msg_id, (int)strlen(json_str));
    free(json_str);

    // 2. Publicar MQTT Discovery para Switch independiente de Encendido/Apagado
    cJSON *sw_root = cJSON_CreateObject();
    if (sw_root) {
        cJSON_AddStringToObject(sw_root, "name", "Encendido");
        char sw_uid[64];
        snprintf(sw_uid, sizeof(sw_uid), "%s_power", s_node_id);
        cJSON_AddStringToObject(sw_root, "unique_id", sw_uid);

        // Mismo dispositivo para agruparlo en Home Assistant
        cJSON *sw_dev = cJSON_CreateObject();
        cJSON *sw_ids = cJSON_CreateArray();
        cJSON_AddItemToArray(sw_ids, cJSON_CreateString(s_node_id));
        cJSON_AddItemToObject(sw_dev, "identifiers", sw_ids);
        cJSON_AddStringToObject(sw_dev, "name", "Controlador Split Marshall");
        cJSON_AddStringToObject(sw_dev, "model", "ESP32-C3 SuperMini (DG11J1-01)");
        cJSON_AddStringToObject(sw_dev, "manufacturer", "Marshall / ESP-IDF");
        cJSON_AddStringToObject(sw_dev, "sw_version", "1.0.0");
        cJSON_AddItemToObject(sw_root, "device", sw_dev);

        cJSON_AddStringToObject(sw_root, "availability_topic", MQTT_TOPIC_AVAIL);
        cJSON_AddStringToObject(sw_root, "payload_available", "online");
        cJSON_AddStringToObject(sw_root, "payload_not_available", "offline");

        cJSON_AddStringToObject(sw_root, "command_topic", MQTT_TOPIC_BASE "/set/power");
        cJSON_AddStringToObject(sw_root, "state_topic", MQTT_TOPIC_STATUS);
        cJSON_AddStringToObject(sw_root, "value_template", "{{ value_json.power }}");
        cJSON_AddStringToObject(sw_root, "state_on", "ON");
        cJSON_AddStringToObject(sw_root, "state_off", "OFF");
        cJSON_AddStringToObject(sw_root, "payload_on", "ON");
        cJSON_AddStringToObject(sw_root, "payload_off", "OFF");
        cJSON_AddStringToObject(sw_root, "icon", "mdi:power");

        char *sw_json_str = cJSON_PrintUnformatted(sw_root);
        cJSON_Delete(sw_root);

        if (sw_json_str) {
            int sw_msg_id = esp_mqtt_client_publish(s_client, s_switch_discovery_topic, sw_json_str, 0, 1, 1);
            ESP_LOGI(TAG, ">>> [MQTT DISCOVERY] Switch publicado en '%s' (msg_id=%d, len=%d bytes, retain=true) <<<",
                     s_switch_discovery_topic, sw_msg_id, (int)strlen(sw_json_str));
            free(sw_json_str);
        }
    }

    return ESP_OK;
}
