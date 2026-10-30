#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"

static const char *TAG = "BLE_DIAG";
static uint8_t s_own_addr_type;
static int s_device_count = 0;

static int ble_scan_event(struct ble_gap_event *event, void *arg)
{
    struct ble_hs_adv_fields fields;
    int rc;

    switch (event->type) {
    case BLE_GAP_EVENT_DISC:
        rc = ble_hs_adv_parse_fields(&fields, event->disc.data, event->disc.length_data);
        if (rc != 0) {
            return 0;
        }

        s_device_count++;
        char name[32] = {0};
        if (fields.name != NULL && fields.name_len > 0) {
            size_t len = fields.name_len < 31 ? fields.name_len : 31;
            memcpy(name, fields.name, len);
            name[len] = '\0';
        } else {
            strcpy(name, "<Sin Nombre>");
        }

        const uint8_t *addr = event->disc.addr.val;
        const char *calif = (event->disc.rssi >= -65) ? "CERCANO (Fuerte)" :
                            (event->disc.rssi >= -80) ? "MEDIO" : "LEJANO (Debil)";

        ESP_LOGI(TAG, "[BLE #%3d] MAC: %02X:%02X:%02X:%02X:%02X:%02X | RSSI: %3d dBm (%s) | %s",
                 s_device_count,
                 addr[5], addr[4], addr[3], addr[2], addr[1], addr[0],
                 event->disc.rssi,
                 calif,
                 name);
        return 0;

    default:
        return 0;
    }
}

static void ble_start_scan(void)
{
    struct ble_gap_disc_params disc_params = {
        .filter_duplicates = 1,
        .passive = 0,
        .itvl = 0x60,
        .window = 0x30,
        .filter_policy = 0,
        .limited = 0
    };

    ESP_LOGI(TAG, "--------------------------------------------------");
    ESP_LOGI(TAG, ">> ESCANEANDO DISPOSITIVOS BLUETOOTH (BLE) <<");
    ESP_LOGI(TAG, "   (Comprobando recepcion y antena 2.4 GHz)");
    ESP_LOGI(TAG, "--------------------------------------------------");

    int rc = ble_gap_disc(s_own_addr_type, BLE_HS_FOREVER, &disc_params, ble_scan_event, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "Error iniciando escaneo BLE: %d", rc);
    }
}

static void ble_on_sync(void)
{
    int rc = ble_hs_util_ensure_addr(0);
    assert(rc == 0);

    rc = ble_hs_id_infer_auto(0, &s_own_addr_type);
    assert(rc == 0);

    uint8_t addr_val[6] = {0};
    rc = ble_hs_id_copy_addr(s_own_addr_type, addr_val, NULL);
    ESP_LOGI(TAG, "Direccion MAC BLE propia: %02X:%02X:%02X:%02X:%02X:%02X",
             addr_val[5], addr_val[4], addr_val[3], addr_val[2], addr_val[1], addr_val[0]);

    ble_start_scan();
}

static void ble_on_reset(int reason)
{
    ESP_LOGE(TAG, "Reset en NimBLE: razon %d", reason);
}

static void ble_host_task(void *param)
{
    ESP_LOGI(TAG, "Pila NimBLE BLE iniciada.");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "==================================================");
    ESP_LOGI(TAG, "     ESP32-C3 Supermini - Test de Bluetooth BLE   ");
    ESP_LOGI(TAG, "==================================================");

    ESP_ERROR_CHECK(nimble_port_init());

    ble_hs_cfg.reset_cb = ble_on_reset;
    ble_hs_cfg.sync_cb = ble_on_sync;

    nimble_port_freertos_init(ble_host_task);
}
