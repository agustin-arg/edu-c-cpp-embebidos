idf_component_register(SRCS "controlador-aire-acondicionado.c" "dg11j1_protocol.c" "wifi_manager.c" "time_manager.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp_driver_rmt esp_driver_gpio esp_wifi esp_netif esp_http_server nvs_flash driver wifi_provisioning bt protocomm esp_coex)
