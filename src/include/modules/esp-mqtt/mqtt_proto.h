#ifndef ESP_MQTT_PROTO_H
#define ESP_MQTT_PROTO_H

#include <stdint.h>

#define MQTT_BUFFER_SIZE    256
#define MQTT_SSL_SIZE       1024 * 5
#define MQTT_PRINT_PACKET   0

// MQTT packet types (QoS 1,2 packets not supported)
enum mqtt_packet_type {
  MQTT_CONNECT       = 1,
  MQTT_CONNACK       = 2,
  MQTT_PUBLISH       = 3,
// MQTT_PUBACK       = 4,
// MQTT_PUBREC       = 5,
// MQTT_PUBREL       = 6,
// MQTT_PUBCOMP      = 7,
  MQTT_SUBSCRIBE     = 8,
  MQTT_SUBACK        = 9,
  MQTT_UNSUBSCRIBE   = 10,
  MQTT_UNSUBACK      = 11,
  MQTT_PINGREQ       = 12,
  MQTT_PINGRESP      = 13,
  MQTT_DISCONNECT    = 14,
};

enum mqtt_qos {
  MQTT_QOS_0,   // at most once
  MQTT_QOS_1,   // at least once
  MQTT_QOS_2    // exactly once
};

struct mqtt_buffer {
  uint8_t *data;
  uint8_t offset;
};

struct mqtt_message {
  uint8_t *topic;
  uint8_t *data;
  uint8_t data_len;
};

struct mqtt_connection {
  uint16_t kalive;
  char *client_id;
  char *username;
  char *password;
  int packet_id;
  void *reverse;
  void (*connect_cb)(struct mqtt_connection *conn);
  void (*send_cb)(struct mqtt_connection *conn, uint8_t *data, int data_len);
  void (*message_cb)(struct mqtt_connection *conn, struct mqtt_message *message);
};

// MQTT client methods
void mqtt_connect(struct mqtt_connection *conn);
void mqtt_disconnect(struct mqtt_connection *conn);
void mqtt_subscribe(struct mqtt_connection *conn, char *topic, enum mqtt_qos qos);
void mqtt_unsubscribe(struct mqtt_connection *conn, char *topic);
void mqtt_publish(struct mqtt_connection *conn, char *topic, uint8_t *message);
void mqtt_ping(struct mqtt_connection *conn);
void mqtt_parse_packet(struct mqtt_connection *conn, uint8_t *data, int data_len);

#endif
