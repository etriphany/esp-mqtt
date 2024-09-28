#ifndef ESP_MQTT_PROTO_H
#define ESP_MQTT_PROTO_H

#include <stdint.h>

#define MQTT_BUFFER_SIZE    512
#define MQTT_SSL_SIZE       1024 * 5 // rule of thumb (PUBLIC_KEY_SIZE / 2) * 5

// MQTT packet types (QoS 2 packets not supported)
enum mqtt_packet_type {
  MQTT_CONNECT       = 1,
  MQTT_CONNACK       = 2,
  MQTT_PUBLISH       = 3,
  MQTT_PUBACK        = 4,
  MQTT_PUBREC        = 5, // not supported
  MQTT_PUBREL        = 6, // not supported
  MQTT_PUBCOMP       = 7, // not supported
  MQTT_SUBSCRIBE     = 8,
  MQTT_SUBACK        = 9,
  MQTT_UNSUBSCRIBE   = 10,
  MQTT_UNSUBACK      = 11,
  MQTT_PINGREQ       = 12,
  MQTT_PINGRESP      = 13,
  MQTT_DISCONNECT    = 14
};

enum mqtt_qos {
  MQTT_QOS_0,   // at most once
  MQTT_QOS_1,   // at least once
  MQTT_QOS_2    // exactly once
};

enum mqtt_connack_status {
  MQTT_CONNACK_SUCCESS                   = 0x00,
  MQTT_CONNACK_FAIL_MQTT_VERSION         = 0x01,
  MQTT_CONNACK_FAIL_IDENTIFIER           = 0x02,
  MQTT_CONNACK_FAIL_SERVER_UNAVAILABLE   = 0x03,
  MQTT_CONNACK_FAIL_BAD_CREDENTIALS      = 0x04,
  MQTT_CONNACK_FAIL_NOT_AUTHORIZED       = 0x05
};

enum mqtt_suback_status {
  MQTT_SUBACK_SUCCESS_QOS_0      = 0x00,
  MQTT_SUBACK_SUCCESS_QOS_1      = 0x01,
  MQTT_SUBACK_SUCCESS_QOS_2      = 0x02,
  MQTT_SUBACK_FAIL               = 0x80
};

struct mqtt_buffer {
  uint8_t *data;
  uint16_t offset;
};

struct mqtt_message {
  uint8_t *topic;
  uint8_t *data;
  uint16_t data_len;
};

struct mqtt_last_will {
  uint8_t *topic;
  uint8_t *data;
  bool retain;
  enum mqtt_qos qos;
};

struct mqtt_connection {
  uint16_t kalive;
  bool clean_session;
  char *client_id;
  char *username;
  char *password;
  int packet_id;
  struct mqtt_last_will last_will;
  void *reverse;
  void (*connect_cb)(struct mqtt_connection *, enum mqtt_connack_status);
  void (*subscribe_cb)(struct mqtt_connection *, enum mqtt_suback_status, const uint16_t);
  void (*send_cb)(struct mqtt_connection *, uint8_t *, int);
  void (*message_cb)(struct mqtt_connection *, struct mqtt_message *);
};

// MQTT client methods
void mqtt_connect(struct mqtt_connection *conn);
void mqtt_disconnect(struct mqtt_connection *conn);
void mqtt_subscribe(struct mqtt_connection *conn, char *topic, enum mqtt_qos qos);
void mqtt_unsubscribe(struct mqtt_connection *conn, char *topic);
void mqtt_publish(struct mqtt_connection *conn, char *topic, uint8_t *message, enum mqtt_qos qos, bool retain);
void mqtt_ping(struct mqtt_connection *conn);
void mqtt_parse_packet(struct mqtt_connection *conn, uint8_t *data, int data_len);

#endif
