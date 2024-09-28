#include <user_interface.h>
#include <osapi.h>
#include <espconn.h>
#include <mem.h>

#include "modules/utils/hashtable.h"
#include "modules/utils/string.h"
#include "modules/esp-mqtt/mqtt_client.h"

#define MAX_MQTT_CALLBACKS 10

// Features
static hash_t *sub_hash;

/******************************************************************************
 * Remove old subscription callback
 *
 *******************************************************************************/
static void ICACHE_FLASH_ATTR
remove_subscription_callback(char *pattern)
{
  if(sub_hash != NULL)
      hash_delete(sub_hash, pattern);
}

/******************************************************************************
 * Add new subscription callback
 *
 *******************************************************************************/
static void ICACHE_FLASH_ATTR
add_subscription_callback(char *pattern, void (*cb)(struct mqtt_connection *, struct mqtt_message *))
{
   // Create internal hashtable if required
  if(sub_hash == NULL)
    sub_hash = hash_create(MAX_MQTT_CALLBACKS);

  // Defines specific callback?
  if(cb != NULL)
  {
    remove_subscription_callback(pattern);
    hash_insert(sub_hash, pattern, cb);
  }
}

/******************************************************************************
 * Print MQTT packet
 *
 *******************************************************************************/
static void ICACHE_FLASH_ATTR
print_packet(uint8_t *data, int data_len)
{
  int i = 0, lst = (data_len - 1);
  LOGGER("MQTT PACKET: [ ");
  for(i = 0; i < data_len; ++i)
    LOGGER("0x%02X%s", data[i], (i == lst ? " " : ", "));
  LOGGER("]\n");
}

/******************************************************************************
 * Timer callback for MQTT pings
 *
 *******************************************************************************/
static void ICACHE_FLASH_ATTR
ping_timer_cb(void *arg)
{
  struct mqtt_client *cli = (struct mqtt_client *) arg;
  mqtt_ping(&cli->mqtt_conn);
}

/******************************************************************************
 * Callback called on MQTT connection
 *
 *******************************************************************************/
static void ICACHE_FLASH_ATTR
mqtt_connected_handler(struct mqtt_connection *mqtt_conn, enum mqtt_connack_status status)
{
  #if MQTT_DEBUG
  LOGGER("MQTT: Connection status = %d\n", status);
  #endif

  if(status != MQTT_CONNACK_SUCCESS)
    return;

  struct mqtt_client *cli = (struct mqtt_client *) mqtt_conn->reverse;
  // Register ping timer
  cli->ping_timer = NULL;
  cli->ping_timer = (os_timer_t *) os_zalloc(sizeof(os_timer_t));
  os_timer_setfn(cli->ping_timer, ping_timer_cb, cli);
  os_timer_arm(cli->ping_timer, cli->mqtt_conn.kalive * 1000, 1);
  // Call user callback
  if (*cli->user_connect_cb)
    cli->user_connect_cb(mqtt_conn);
}

/******************************************************************************
 * Callback called on MQTT subscribe
 *
 *******************************************************************************/
static void ICACHE_FLASH_ATTR
mqtt_subscribe_handler(struct mqtt_connection *mqtt_conn, enum mqtt_suback_status status, const uint16_t packet_id)
{
  #if MQTT_DEBUG
  LOGGER("MQTT: Subscribe status = %d\n", status);
  #endif

  if(status == MQTT_SUBACK_FAIL)
    return;

  struct mqtt_client *cli = (struct mqtt_client *) mqtt_conn->reverse;
  if (*cli->user_subscribe_cb)
    cli->user_subscribe_cb(mqtt_conn, packet_id);
}

/******************************************************************************
 * Callback called to send MQTT messages
 *
 *******************************************************************************/
static void ICACHE_FLASH_ATTR
mqtt_send_handler(struct mqtt_connection *mqtt_conn, uint8_t *data, int data_len)
{
  #if MQTT_DEBUG_PACKET
  print_packet(data, data_len);
  #endif

  struct mqtt_client *cli = (struct mqtt_client *) mqtt_conn->reverse;
  if(cli->secure)
    espconn_secure_send(cli->tcp_conn, data, data_len);
  else
    espconn_send(cli->tcp_conn, data, data_len);
}

/******************************************************************************
 * Callback called to handle MQTT messages
 *
 *******************************************************************************/
static void ICACHE_FLASH_ATTR
mqtt_message_handler(struct mqtt_connection *mqtt_conn, struct mqtt_message *message)
{
  struct mqtt_client *cli = (struct mqtt_client *) mqtt_conn->reverse;
  uint8_t matches = 0;
  uint8_t i = 0;

  // Check subscription hashmap
  if(sub_hash != NULL)
  {
    for(i = 0; i < sub_hash->size; ++i)
    {
      char* sub_pattern = sub_hash->keys[i];
      void (*cb)(struct mqtt_connection *, struct mqtt_message *) = sub_hash->values[i];
      if(sub_pattern == NULL || cb == NULL)
        continue;

      // Match multi-level?
      if(ends_with(sub_pattern, "/#"))
      {
        // Compare subscription and message topic
        char* parts[2];
        split(sub_pattern, "#", parts);
        if(starts_with(message->topic, parts[0]))
        {
          cb(mqtt_conn, message);
          ++matches;
          continue;
        }
      }

      // Match single-level? (supports just one level)
      if(os_strstr(sub_pattern, "/+"))
      {
          // Compare subscription and message topic
          char* parts[2];
          split(sub_pattern, "+", parts);
          //if(starts_with(message->topic, parts[0]) && ends_with(message->topic, part[1]))
          if(starts_with(message->topic, parts[0]))
          {
            cb(mqtt_conn, message);
            ++matches;
            continue;
          }
      }

      // Exact match?
      const uint16_t topic_len = os_strlen(message->topic);
      if (os_strncmp(sub_pattern, message->topic, topic_len) == 0)
      {
        cb(mqtt_conn, message);
        ++matches;
        continue;
      }

    }
  }

  // If nothing matches call global callback
  if (matches == 0 && *cli->user_message_cb)
    cli->user_message_cb(mqtt_conn, message);
}

/******************************************************************************
 * Callback called when socket connected
 *
 *******************************************************************************/
static void ICACHE_FLASH_ATTR
socket_connected_cb(void *arg)
{
  struct espconn *conn = (struct espconn *) arg;
  struct mqtt_client *cli = (struct mqtt_client *) conn->reverse;
  mqtt_connect(&cli->mqtt_conn);
}

/******************************************************************************
 * Callback called when socket receives packets
 *
 *******************************************************************************/
static void ICACHE_FLASH_ATTR
socket_recv_cb(void *arg, char *pdata, unsigned short len)
{
  #if MQTT_DEBUG_PACKET
  print_packet(pdata, (int) len);
  #endif

  struct espconn *conn = (struct espconn *) arg;
  struct mqtt_client *cli = (struct mqtt_client *) conn->reverse;
  mqtt_parse_packet(&cli->mqtt_conn, (uint8_t *) pdata, (int) len);
}

/******************************************************************************
 * Callback called when socket disconnects
 *
 *******************************************************************************/
static void ICACHE_FLASH_ATTR
socket_disconnected_cb(void *arg)
{
  #if MQTT_DEBUG
  LOGGER("MQTT: Disconnected\n");
  #endif
  struct espconn *conn = (struct espconn *) arg;
  struct mqtt_client *cli = (struct mqtt_client *) conn->reverse;
  // Call user callback
  if (*cli->user_disconnet_cb)
    cli->user_disconnet_cb(&cli->mqtt_conn);
}

/******************************************************************************
 * Callback called when socket error occurs
 *
 * (check espconn.h for error values)
 *
 *******************************************************************************/
static void ICACHE_FLASH_ATTR
socket_error_cb(void *arg, int8_t err)
{
  #if MQTT_DEBUG
    LOGGER("MQTT: Connection error %d\n", err);
  #endif
}

/******************************************************************************
 * Callback called after MQTT broker host resolution
 *
 *******************************************************************************/
static void ICACHE_FLASH_ATTR
find_host_cb(const char *name, ip_addr_t *ip, void *arg)
{
  struct mqtt_client *cli = (struct mqtt_client *) arg;
  if (ip == NULL) {
    #if MQTT_DEBUG
    LOGGER("MQTT: DNS resolve failed!\n");
    #endif
    mqtt_client_connect(cli);
    return;
  }

  // Register internal callbacks
  cli->mqtt_conn.connect_cb = mqtt_connected_handler;
  cli->mqtt_conn.subscribe_cb = mqtt_subscribe_handler;
  cli->mqtt_conn.send_cb = mqtt_send_handler;
  cli->mqtt_conn.message_cb = mqtt_message_handler;

  // TCP socket setup
  cli->tcp_conn = NULL;
  cli->tcp_conn = (struct espconn *) os_zalloc(sizeof(struct espconn));
  cli->tcp_conn->type = ESPCONN_TCP;
  cli->tcp_conn->state = ESPCONN_NONE;
  cli->tcp_conn->proto.tcp = (esp_tcp *) os_zalloc(sizeof(esp_tcp));
  cli->tcp_conn->proto.tcp->local_port = espconn_port();
  cli->tcp_conn->proto.tcp->remote_port = cli->host_port;
  espconn_regist_connectcb(cli->tcp_conn, socket_connected_cb);
  espconn_regist_recvcb(cli->tcp_conn, socket_recv_cb);
  espconn_regist_disconcb(cli->tcp_conn, socket_disconnected_cb);
  espconn_regist_reconcb(cli->tcp_conn, socket_error_cb);

  // Reversables
  cli->mqtt_conn.reverse = cli;
  cli->tcp_conn->reverse = cli;

  // Connect to host
  #if MQTT_DEBUG
  LOGGER("MQTT: Connecting to "IPSTR"...\n", IP2STR(ip));
  #endif
  os_memcpy(cli->tcp_conn->proto.tcp->remote_ip, &ip->addr, 4);
  if(cli->secure)
  {
    espconn_secure_set_size(ESPCONN_CLIENT, MQTT_SSL_SIZE);
    espconn_secure_connect(cli->tcp_conn);
  }
  else
    espconn_connect(cli->tcp_conn);
}

/******************************************************************************
 * Connect client to MQTT broker
 *
 *******************************************************************************/
void ICACHE_FLASH_ATTR
mqtt_client_connect(struct mqtt_client *cli)
{
  #if MQTT_DEBUG
  LOGGER("MQTT: Resolving host\n");
  #endif
  espconn_gethostbyname((struct espconn *)cli, cli->host_name, &cli->host_ip, find_host_cb);
}

/******************************************************************************
 * Publish to MQTT topic
 *
 *******************************************************************************/
void ICACHE_FLASH_ATTR
mqtt_client_publish(struct mqtt_connection *conn, char *topic, uint8_t *message, enum mqtt_qos qos, bool retain)
{
  mqtt_publish(conn, topic, message, qos, retain);
}

/******************************************************************************
 * Subscribe to MQTT topic
 *
 *******************************************************************************/
void ICACHE_FLASH_ATTR
mqtt_client_subscribe(struct mqtt_connection *conn, char *topic, enum mqtt_qos qos,
                          void (*cb)(struct mqtt_connection *, struct mqtt_message *))
{
  add_subscription_callback(topic, cb);
  mqtt_subscribe(conn, topic, qos);
}

/******************************************************************************
 * Unsubscribe to MQTT topic
 *
 *******************************************************************************/
void ICACHE_FLASH_ATTR
mqtt_client_unsubscribe(struct mqtt_connection *conn, char *topic)
{
  remove_subscription_callback(topic);
  mqtt_unsubscribe(conn, topic);
}
