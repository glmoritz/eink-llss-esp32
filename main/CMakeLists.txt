# LLSS e-Ink Client - Main Component CMakeLists.txt

set(SOURCES
    "main.cc"
    "llss_application.cc"
    "llss_client.cc"
    "llss_state.cc"
    "epd_driver.cc"
    "mcp23017.cc"
    "input_manager.cc"
    "pmic_axp2101.cc"
    "wifi_provisioning.cc"
    "settings.cc"
)

set(INCLUDE_DIRS ".")

idf_component_register(SRCS ${SOURCES}
                    INCLUDE_DIRS ${INCLUDE_DIRS}
                    WHOLE_ARCHIVE
                    PRIV_REQUIRES
                        esp_psram
                        esp_netif
                        esp_driver_gpio
                        esp_driver_spi
                        esp_driver_i2c
                        esp_app_format
                        app_update
                        spi_flash
                        esp_wifi
                        esp_http_client
                        esp_http_server
                        esp_https_ota
                        esp_event
                        nvs_flash
                        json
                        esp_tls
                    )

target_compile_definitions(${COMPONENT_LIB}
                    PRIVATE BOARD_TYPE=\"waveshare-esp32s3-epaper-3.97\"
                    PRIVATE BOARD_NAME=\"LLSS-ePaper-3.97\"
                    )
