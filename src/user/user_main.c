#include <c_types.h>
#include <mem.h>
#include <ets_sys.h>
#include <osapi.h>
#include <gpio.h>
#include <user_interface.h>

#include "modules/esp-mqtt/mqtt_client.h"
#include "user_config.h"

// https://docs.solace.com/MQTT-311-Prtl-Conformance-Spec/MQTT_311_Prtl_Conformance_Spec.htm#OASIS_MQTT_Opening
// https://openlabpro.com/guide/mqtt-packet-format/
// https://docs.oasis-open.org/mqtt/mqtt/v3.1.1/os/mqtt-v3.1.1-os.html

static const partition_item_t at_partition_table[] = {
    { SYSTEM_PARTITION_BOOTLOADER,                      0x0,                                                0x1000},
    { SYSTEM_PARTITION_OTA_1,                           0x1000,                                             SYSTEM_PARTITION_OTA_SIZE},
    { SYSTEM_PARTITION_OTA_2,                           SYSTEM_PARTITION_OTA_2_ADDR,                        SYSTEM_PARTITION_OTA_SIZE},
    { SYSTEM_PARTITION_RF_CAL,                          SYSTEM_PARTITION_RF_CAL_ADDR,                       0x1000},
    { SYSTEM_PARTITION_PHY_DATA,                        SYSTEM_PARTITION_PHY_DATA_ADDR,                     0x1000},
    { SYSTEM_PARTITION_SYSTEM_PARAMETER,                SYSTEM_PARTITION_SYSTEM_PARAMETER_ADDR,             0x3000},
    { SYSTEM_PARTITION_CUSTOMER_PRIV_PARAM,             SYSTEM_PARTITION_CUSTOMER_PRIV_PARAM_ADDR,          0x1000},
};

#define SSID         "CHANGE_ME"
#define PASSWORD	   "CHANGE_ME"

void ICACHE_FLASH_ATTR
on_message(struct mqtt_connection *conn, struct mqtt_message *message)
{
  os_printf("MQTT: msg: %s %s\n", message->topic, (char *)message->data);
}

void ICACHE_FLASH_ATTR
on_connected(struct mqtt_connection *conn)
{
  os_printf("MQTT: Client connect\n");
  mqtt_subscribe(conn, "commands", MQTT_QOS_0);
}

// Client config
struct mqtt_client client = {
  .secure = TRUE,
  .host_name = "cloudmqtt.com",
  .host_port = 27558,
  .user_connected_cb = on_connected,
  .user_message_cb = on_message,
  .mqtt_conn = {
    .client_id = "CLIENT_ID",
    .username = "CLIENT_ID",
    .password = "PASSWORD",
    .kalive = 60
  }
};

void ICACHE_FLASH_ATTR
on_wifi_event(System_Event_t *event)
{
  switch (event->event) {
  case EVENT_STAMODE_GOT_IP:
    mqtt_client_connect(&client);
    break;
  }
}

/******************************************************************************
 * The default method provided. Users can add functions like
 * firmware initialization, network parameters setting,
 * and timer initialization within user_init
 *******************************************************************************/
void ICACHE_FLASH_ATTR
user_init(void)
{
  uart_init(115200, 115200);
  wifi_set_event_handler_cb(on_wifi_event);

  struct station_config config = {
    .ssid = SSID,
    .password = PASSWORD,
  };
  wifi_set_opmode(STATION_MODE);
  wifi_station_set_config(&config);
  wifi_station_connect();
}


/******************************************************************************
 * Need to be added to 'user_main.c' from ESP8266_NONOS_SDK_V3.0.0 onwards.
 *******************************************************************************/
void ICACHE_FLASH_ATTR
user_pre_init(void)
{
   uint32_t partition = sizeof(at_partition_table) / sizeof(at_partition_table[0]);
   if(!system_partition_table_regist(at_partition_table, partition, SPI_FLASH_SIZE_MAP))
   {
        os_printf("Init failed: Partition table registry\r\n");
        while(1);
   }
}

/******************************************************************************
 * From  ESP8266_NONOS_SDK_V2.1.0 onwards, when the DIO-to-QIO
 * flash is not used, users can add an empty function
 * 'void user_spi_flash_dio_to_qio_pre_init(void)' on
 * the application side to reduce iRAM usage.
 *******************************************************************************/
void ICACHE_FLASH_ATTR
user_spi_flash_dio_to_qio_pre_init(void)
{
}

