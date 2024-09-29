#ifndef _USER_CONFIG_H
#define _USER_CONFIG_H

// ESP12E Flash Addresses
// ---------------------------
#define SPI_FLASH_SIZE_MAP                            4
#define SYSTEM_PARTITION_OTA_SIZE                     0x6A000
#define SYSTEM_PARTITION_OTA_2_ADDR                   0x81000
#define SYSTEM_PARTITION_RF_CAL_ADDR                  0x3fb000
#define SYSTEM_PARTITION_PHY_DATA_ADDR                0x3fc000
#define SYSTEM_PARTITION_SYSTEM_PARAMETER_ADDR        0x3fd000
#define SYSTEM_PARTITION_CUSTOMER_PRIV_PARAM_ADDR     0x7c000
#define SYSTEM_PARTITION_CUSTOMER_PRIV_PARAM          SYSTEM_PARTITION_CUSTOMER_BEGIN

// SDK
// ---------------------------
#define USE_OPTIMIZE_PRINTF         1
#define MEMLEAK_DEBUG               0
#define CONFIG_ENABLE_IRAM_MEMORY   0

// Logger
// ---------------------------
#define LOGGER                                        os_printf

// WIFI
// ---------------------------
#define WIFI_SSID                                     "WIFI_SSID"
#define WIFI_PASSWORD	                              "WIFI_PASSWORD"

// MQTT
// ---------------------------
#define MQTT_HOST                                     "cloudmqtt.com"
#define MQTT_PORT                                      27558
#define MQTT_CLIENT_ID                                "MQTT_USER"
#define MQTT_PASSWORD                                 "MQTT_PASSWORD"

#endif