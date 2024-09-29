#ifndef ESP_MQTT_CLIENT_H
#define ESP_MQTT_CLIENT_H

#include <user_interface.h>
#include <espconn.h>
#include <osapi.h>
#include "mqtt_proto.h"

#define MQTT_DEBUG         1
#define MQTT_DEBUG_PACKET  1

struct mqtt_client {
  bool secure;
  char *host_name;
  uint16_t host_port;
  struct ip_addr host_ip;
  struct mqtt_connection mqtt_conn;
  struct espconn *tcp_conn;
  os_timer_t *ping_timer;
  void (*user_connect_cb)(struct mqtt_connection *);
  void (*user_subscribe_cb)(struct mqtt_connection *, const uint16_t);
  void (*user_message_cb)(struct mqtt_connection *, struct mqtt_message *);
  void (*user_disconnet_cb)(struct mqtt_connection *);
};

// Client operations
void mqtt_client_connect(struct mqtt_client *cfg);
void mqtt_client_publish(struct mqtt_connection *conn, char *topic, uint8_t *message, enum mqtt_qos qos, bool retain);
void mqtt_client_subscribe(struct mqtt_connection *conn, char *topic, enum mqtt_qos qos,
                          void (*cb)(struct mqtt_connection *, struct mqtt_message *));
void mqtt_client_unsubscribe(struct mqtt_connection *conn, char *topic);

#endif
