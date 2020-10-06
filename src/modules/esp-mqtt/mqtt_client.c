#include <user_interface.h>
#include <osapi.h>
#include <espconn.h>
#include <mem.h>

#include "modules/esp-mqtt/mqtt_client.h"

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
mqtt_connected(struct mqtt_connection *mqtt_conn)
{
  struct mqtt_client *cli = (struct mqtt_client *) mqtt_conn->reverse;
  // Register ping timer
  cli->ping_timer = (os_timer_t *) os_zalloc(sizeof(os_timer_t));
  os_timer_setfn(cli->ping_timer, ping_timer_cb, cli);
  os_timer_arm(cli->ping_timer, cli->mqtt_conn.kalive * 1000, 1);
  // Call user callback
  if (&cli->user_connected_cb != NULL)
    cli->user_connected_cb(mqtt_conn);
}

/******************************************************************************
 * Callback called to send MQTT messages
 *
 *******************************************************************************/
static void ICACHE_FLASH_ATTR
mqtt_send(struct mqtt_connection *mqtt_conn, uint8_t *data, int data_len)
{
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
mqtt_message(struct mqtt_connection *mqtt_conn, struct mqtt_message *message)
{
  struct mqtt_client *cli = (struct mqtt_client *) mqtt_conn->reverse;
  // Call user callback
  if (&cli->user_message_cb != NULL)
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
  os_printf("MQTT: Disconnected\n");
  #endif
  struct espconn *conn = (struct espconn *) arg;
  struct mqtt_client *cli = (struct mqtt_client *) conn->reverse;
  mqtt_client_connect(cli);
}

/******************************************************************************
 * Callback called when socket error occurs
 *
 *******************************************************************************/
static void ICACHE_FLASH_ATTR
socket_error_cb(void *arg, int8_t err)
{
  #if MQTT_DEBUG
    os_printf("MQTT: Connection error %d\n", err);
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
    os_printf("MQTT: DNS resolve failed!\n");
    #endif
    mqtt_client_connect(cli);
    return;
  }

  // Register internal callbacks
  cli->mqtt_conn.connect_cb = mqtt_connected;
  cli->mqtt_conn.send_cb = mqtt_send;
  cli->mqtt_conn.message_cb = mqtt_message;

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
  os_printf("MQTT: Connecting to "IPSTR"...\n", IP2STR(ip));
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
  os_printf("MQTT: Resolving host\n");
  #endif
  espconn_gethostbyname((struct espconn *)cli, cli->host_name, &cli->host_ip, find_host_cb);
}
