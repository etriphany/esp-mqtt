#ifndef ESP_MQTT_CLIENT_H
#define ESP_MQTT_CLIENT_H

#include "mqtt_proto.h"

#define MQTT_DEBUG      1

struct mqtt_client {
  bool secure;
  char *host_name;
  uint16_t host_port;
  struct ip_addr host_ip;
  struct mqtt_connection mqtt_conn;
  struct espconn *tcp_conn;
  os_timer_t *ping_timer;
  void (*user_connected_cb)(struct mqtt_connection *conn);
  void (*user_message_cb)(struct mqtt_connection *conn, struct mqtt_message *message);
};

void mqtt_client_connect(struct mqtt_client *cfg);

#endif
