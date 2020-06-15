#include <string.h>
#include <osapi.h>
#include <mem.h>

#include "modules/esp-mqtt/mqtt_proto.h"

#define mqtt_header(type, flag_3, flag_2, flag_1, flag_0) \
  (((type) << 4) | ((flag_3) << 3) | ((flag_2) << 2) | ((flag_1) << 1) | (flag_0))

/******************************************************************************
 * Write into MQTT buffer
 *
 *******************************************************************************/
static void ICACHE_FLASH_ATTR
write_buffer(struct mqtt_buffer *buffer, uint8_t *data, int data_len)
{
  uint8_t i = 0;
  for(i = 0; i < data_len; ++i)
    buffer->data[buffer->offset++] = data[i];
}

/******************************************************************************
 * Print MQTT packet
 *
 *******************************************************************************/
static void ICACHE_FLASH_ATTR
print_packet(uint8_t *data, int data_len)
{
  uint8_t i = 0;
  os_printf("\n");
  for(i = 0; i < data_len; ++i)
    os_printf("%02X ", data[i]);
  os_printf("\n");
}

/******************************************************************************
 * Decodes MQTT Multi-Byte Integer
 *
 *******************************************************************************/
static int ICACHE_FLASH_ATTR
decode_mbi(uint8_t *data, uint8_t *offset)
{
  uint8_t numBytes = 0;
  int multiplier = 1;
  int number = 0;
  int digit;

  do
  {
    digit = data[numBytes++];
    number += ((digit & 0x7f) * multiplier);
    multiplier *= 128;
  }
  while ((digit & 0x80) != 0);

  // Update data offset
  *offset = numBytes;

  return number;
}

/******************************************************************************
 * Encodes MQTT Multi-Byte Integer
 *
 *******************************************************************************/
static int ICACHE_FLASH_ATTR
encode_mbi(int number, uint8_t *data)
{
  uint8_t numBytes = 0;
  int digit;

  do
  {
    digit = number % 128;
    number = number >> 7;
    if (number > 0)
      digit |= 0x80; // 0x80 == 128
    data[numBytes++] = digit;
  }
  while ((number > 0) && (numBytes < 4));

  return numBytes;
}

/******************************************************************************
 * Decodes MQTT Short
 *
 *******************************************************************************/
static uint16_t ICACHE_FLASH_ATTR
decode_uint16(uint8_t *buff, uint8_t offset)
{
   return (buff[offset] << 8) | buff[offset + 1];
}

/******************************************************************************
 * Encodes MQTT Short
 *
 *******************************************************************************/
static uint8_t ICACHE_FLASH_ATTR
encode_uint16(uint16_t number, uint8_t *buff, uint8_t offset)
{
    buff[offset++] = number >> 8;      // MSB
    buff[offset++] = number & 0xff;    // LSB
    return offset;
}

/******************************************************************************
 * Encodes MQTT Packet ID
 *
 *******************************************************************************/
static void ICACHE_FLASH_ATTR
encode_packet_id(struct mqtt_connection *conn, uint8_t *data)
{
  encode_uint16(conn->packet_id++, data, 0);
}

/******************************************************************************
 * Encodes MQTT String
 *
 *******************************************************************************/
static void ICACHE_FLASH_ATTR
encode_str(struct mqtt_buffer *buffer, uint8_t *str, int str_len)
{
  // Encode size
  uint8_t len_data[2];
  encode_uint16(str_len, len_data, 0);

  // Write to buffer
  write_buffer(buffer, len_data, sizeof(len_data));
  write_buffer(buffer, str, str_len);
}

/******************************************************************************
 * Decodes MQTT Publish
 *
 *******************************************************************************/
static struct mqtt_message* ICACHE_FLASH_ATTR
decode_publish(struct mqtt_buffer *buffer)
{
  uint8_t buffer_len = buffer->offset;
  struct mqtt_message *msg = NULL;
  msg = (struct mqtt_message *) os_malloc(sizeof(struct mqtt_message));

  // Topic length
  buffer->offset = 0;
  uint16_t topic_len = decode_uint16(buffer->data, 0);
  buffer->offset += 2;

  // Message Topic
  msg->topic = (uint8_t*)os_malloc(sizeof(uint8_t) * topic_len + 1);
  os_memset(msg->topic, 0, topic_len + 1);
  os_memcpy(msg->topic, (buffer->data + buffer->offset), topic_len);
  buffer->offset += topic_len;

  // Message payload
  msg->data_len = (buffer_len - buffer->offset);
  msg->data = (uint8_t*)os_malloc(sizeof(uint8_t) * msg->data_len + 1);
  os_memset(msg->data, 0, msg->data_len + 1);
  os_memcpy(msg->data, (buffer->data + buffer->offset), msg->data_len);

  return msg;
}

/******************************************************************************
 * Encodes MQTT Connect
 *
 *******************************************************************************/
void ICACHE_FLASH_ATTR
mqtt_connect(struct mqtt_connection *conn)
{
  // Buffer
  struct mqtt_buffer buffer = {
    .data = (uint8_t*) os_malloc(MQTT_BUFFER_SIZE)
  };

  // Packet headers
  uint8_t fixed_hd;
  uint8_t remlen_len, remlen[4];
  uint8_t flags = 0b11000010; // user + password + clean session
  uint8_t variable_hd[10] = {0x00, 0x04, 'M', 'Q', 'T', 'T', 4, flags, 0x00, 0x00};

  // Reset packet ids
  conn->packet_id = 1;

  // Lengths
  const int8_t cli_len = os_strlen(conn->client_id);
  const int8_t user_len = os_strlen(conn->username);
  const int8_t pwd_len = os_strlen(conn->password);
  const int8_t strs_len = 3 * 2; // final payload includes 2 bytes for each string

  // Fixed header
  fixed_hd = mqtt_header(MQTT_CONNECT, 0, 0, 0, 0);
  remlen_len = encode_mbi(sizeof(variable_hd) + cli_len + user_len + pwd_len + strs_len, remlen);
  // Variable header
  encode_uint16(conn->kalive, variable_hd, 8);

  // MQTT fixed header
  write_buffer(&buffer, &fixed_hd, 1);
  write_buffer(&buffer, remlen, remlen_len);
  // MQTT variable header
  write_buffer(&buffer, variable_hd, sizeof(variable_hd));
  // MQTT payload
  encode_str(&buffer, conn->client_id, cli_len);
  encode_str(&buffer, conn->username, user_len);
  encode_str(&buffer, conn->password, pwd_len);

  // Send
  conn->send_cb(conn, buffer.data, buffer.offset);
}

/******************************************************************************
 * Encodes MQTT Disconnect
 *
 *******************************************************************************/
void ICACHE_FLASH_ATTR
mqtt_disconnect(struct mqtt_connection *conn)
{
  // Buffer
  struct mqtt_buffer buffer = {
    .data = (uint8_t*) os_malloc(MQTT_BUFFER_SIZE)
  };

  // MQTT packet
  uint8_t packet[] = { mqtt_header(MQTT_DISCONNECT, 0, 0, 0, 0), 0 };
  write_buffer(&buffer, packet, sizeof(packet));

  // Send
  conn->send_cb(conn, buffer.data, buffer.offset);
}

/******************************************************************************
 * Encodes MQTT Subscribe
 * This implementation apply one topic filter on each message
 *
 *******************************************************************************/
void ICACHE_FLASH_ATTR
mqtt_subscribe(struct mqtt_connection *conn, char *topic, enum mqtt_qos qos)
{
  // Buffer
  struct mqtt_buffer buffer = {
    .data = (uint8_t*) os_malloc(MQTT_BUFFER_SIZE)
  };

  // Packet headers
  uint8_t fixed_hd;
  uint8_t remlen_len, remlen[4];
  uint8_t variable_hd[2];

  // Lengths
  const int8_t topic_len = os_strlen(topic);
  const int8_t strs_len = 2;
  const int8_t qos_len = 1;

  // Fixed header
  fixed_hd = mqtt_header(MQTT_SUBSCRIBE, 0, 0, 1, 0);
  remlen_len = encode_mbi(sizeof(variable_hd) + topic_len + strs_len + qos_len, remlen);
  // Variable header
  encode_packet_id(conn, variable_hd);

  // MQTT fixed header
  write_buffer(&buffer, &fixed_hd, 1);
  write_buffer(&buffer, remlen, remlen_len);
  // MQTT variable header
  write_buffer(&buffer, variable_hd, sizeof(variable_hd));
  // MQTT payload
  uint8_t qos_data = qos;
  encode_str(&buffer, topic, topic_len);
  write_buffer(&buffer, &qos_data, qos_len);

  // Send
  conn->send_cb(conn, buffer.data, buffer.offset);
}

/******************************************************************************
 * Encodes MQTT Unsubscribe
 * This implementation apply one topic filter on each message
 *
 *******************************************************************************/
void ICACHE_FLASH_ATTR
mqtt_unsubscribe(struct mqtt_connection *conn, char *topic)
{
  // Buffer
  struct mqtt_buffer buffer = {
    .data = (uint8_t*) os_malloc(MQTT_BUFFER_SIZE)
  };

  // Packet headers
  uint8_t fixed_hd;
  uint8_t remlen_len, remlen[4];
  uint8_t variable_hd[2];

  // Lengths
  const int8_t topic_len = os_strlen(topic);
  const int8_t strs_len = 2;

  // Fixed header
  fixed_hd = mqtt_header(MQTT_UNSUBSCRIBE, 0, 0, 1, 0);
  remlen_len = encode_mbi(sizeof(variable_hd) + topic_len + strs_len, remlen);
  // Variable header
  encode_packet_id(conn, variable_hd);

  // MQTT fixed header
  write_buffer(&buffer, &fixed_hd, 1);
  write_buffer(&buffer, remlen, remlen_len);
  // MQTT variable header
  write_buffer(&buffer, variable_hd, sizeof(variable_hd));
  // MQTT payload
  encode_str(&buffer, topic, topic_len);

  // Send
  conn->send_cb(conn, buffer.data, buffer.offset);
}

/******************************************************************************
 * Encodes MQTT Publish
 * This implementation will always use Qos 0 (at most once) delivery
 *
 *******************************************************************************/
void ICACHE_FLASH_ATTR
mqtt_publish(struct mqtt_connection *conn, char *topic, uint8_t *message)
{
  // Buffer
  struct mqtt_buffer buffer = {
    .data = (uint8_t*) os_malloc(MQTT_BUFFER_SIZE)
  };

  // Packet headers
  uint8_t fixed_hd;
  uint8_t remlen_len, remlen[4];

  // Lengths
  uint16_t topic_len = os_strlen(topic);
  uint16_t message_len = os_strlen(message);
  const int8_t strs_len = 2 * 2; // final payload includes 2 bytes for each string

  // Fixed header
  fixed_hd = mqtt_header(MQTT_PUBLISH, 0, MQTT_QOS_0, MQTT_QOS_0, 0);
  remlen_len = encode_mbi(topic_len + message_len + strs_len, remlen);

  // MQTT fixed header
  write_buffer(&buffer, &fixed_hd, 1);
  write_buffer(&buffer, remlen, remlen_len);
  // MQTT payload
  encode_str(&buffer, topic, topic_len);
  encode_str(&buffer, message, message_len);

  // Send
  conn->send_cb(conn, buffer.data, buffer.offset);
}

/******************************************************************************
 * Encodes MQTT Ping
 *
 *******************************************************************************/
void ICACHE_FLASH_ATTR
mqtt_ping(struct mqtt_connection *conn)
{
  // Buffer
  struct mqtt_buffer buffer = {
    .data = (uint8_t*) os_malloc(MQTT_BUFFER_SIZE)
  };

  // MQTT packet
  uint8_t packet[] = { mqtt_header(MQTT_PINGREQ, 0, 0, 0, 0), 0 };
  write_buffer(&buffer, packet, sizeof(packet));

  // Send
  conn->send_cb(conn, buffer.data, buffer.offset);
}

/******************************************************************************
 * Parses MQTT packet
 * This implementation ignores packets used on QoS 1 (at least once) and
 * QoS 2 (exactly once) message delivery control
 *
 *******************************************************************************/
void ICACHE_FLASH_ATTR
mqtt_parse_packet(struct mqtt_connection *conn, uint8_t *data, int data_len)
{
  struct mqtt_message* message;

  #if MQTT_PRINT_PACKET
  print_packet(data, data_len);
  #endif

  // Buffer
  struct mqtt_buffer buffer = {
    .data = (uint8_t*) os_malloc(MQTT_BUFFER_SIZE)
  };

  // Fixed header
  uint8_t packet_type = data[0] >> 4;
  uint8_t *offset = os_malloc(sizeof(uint8_t));
  uint8_t remlen_len = decode_mbi(&data[1], offset);

  // Variable header + Payload
  write_buffer(&buffer, data + 1 + *offset, remlen_len);

  // Callback
  switch (packet_type)
  {
    case MQTT_CONNACK:
      if (buffer.data[1] == 0x00)
        conn->connect_cb(conn);
      break;

    case MQTT_PUBLISH:
      message = decode_publish(&buffer);
      conn->message_cb(conn, message);
      break;
  }
}