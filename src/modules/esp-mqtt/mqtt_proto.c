#include <string.h>
#include <osapi.h>
#include <mem.h>

#include "modules/esp-mqtt/mqtt_proto.h"

// IO buffers
static struct mqtt_buffer w_buffer = {};

#define mqtt_header(type, flag_3, flag_2, flag_1, flag_0) \
  (((type) << 4) | ((flag_3) << 3) | ((flag_2) << 2) | ((flag_1) << 1) | (flag_0))


//
// MQTT MESSAGE BUFFER
//

/******************************************************************************
 * Reset/Initialize buffer
 *
 *******************************************************************************/
static void ICACHE_FLASH_ATTR
reset_buffer(struct mqtt_buffer *buffer)
{
  // Init if not
  if (buffer->data == NULL)
    buffer->data = (uint8_t*) os_malloc(MQTT_BUFFER_SIZE);
  buffer->offset = 0;
}

/******************************************************************************
 * Write into MQTT buffer
 *
 *******************************************************************************/
static void ICACHE_FLASH_ATTR
write_buffer(struct mqtt_buffer *buffer, uint8_t *data, int data_len)
{
  os_memcpy(buffer->data + buffer->offset, data, data_len);
  buffer->offset += data_len;
}

/******************************************************************************
 * Send MQTT buffer
 *
 *******************************************************************************/
static void ICACHE_FLASH_ATTR
send_buffer(struct mqtt_buffer *buffer, struct mqtt_connection *conn)
{
  conn->send_cb(conn, buffer->data, buffer->offset);
}

//
// MQTT STANDARD FORMATS
//

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
decode_uint16(uint8_t *buff, uint16_t offset)
{
   return (buff[offset] << 8) | buff[offset + 1];
}

/******************************************************************************
 * Encodes MQTT Short
 *
 *******************************************************************************/
static uint8_t ICACHE_FLASH_ATTR
encode_uint16(uint16_t number, uint8_t *buff, uint16_t offset)
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

//
// MQTT PACKETS DECODERS
//

/******************************************************************************
 * Decodes MQTT PUBLISH
 *
 *******************************************************************************/
static uint16_t ICACHE_FLASH_ATTR
mqtt_publish_decode(struct mqtt_buffer *buffer, struct mqtt_message *message, enum mqtt_qos qos)
{
  uint16_t packet_id = 0;
  uint16_t buffer_len = buffer->offset;

  // Topic length
  buffer->offset = 0;
  uint16_t topic_len = decode_uint16(buffer->data, 0);
  buffer->offset += 2;

  // Message Topic
  message->topic = (uint8_t*)os_zalloc(sizeof(uint8_t) * topic_len + 1);
  os_memcpy(message->topic, (buffer->data + buffer->offset), topic_len);
  buffer->offset += topic_len;

  // Check packet id
  if(qos != MQTT_QOS_0)
  {
    packet_id = decode_uint16(buffer->data, buffer->offset);
    buffer->offset += 2;
  }

  // Message payload
  message->data_len = (buffer_len - buffer->offset);
  message->data = (uint8_t*)os_zalloc(sizeof(uint8_t) * message->data_len + 1);
  os_memcpy(message->data, (buffer->data + buffer->offset), message->data_len);

  return packet_id;
}

//
// MQTT PACKETS ENCODERS
//

/******************************************************************************
 * Encodes MQTT PUBACK
 *
 *******************************************************************************/
static void ICACHE_FLASH_ATTR
mqtt_puback(struct mqtt_connection *conn, uint16_t packet_id)
{
  reset_buffer(&w_buffer);

  // Packet headers
  uint8_t fixed_hd;
  uint8_t remlen_len, remlen[4];
  uint8_t variable_hd[2];

  // Fixed header
  fixed_hd = mqtt_header(MQTT_PUBACK, 0, 0, 0, 0);
  remlen_len = encode_mbi(sizeof(variable_hd), remlen);
  // Variable header
  encode_uint16(packet_id, variable_hd, 0);

  // Write fixed header
  write_buffer(&w_buffer, &fixed_hd, 1);
  write_buffer(&w_buffer, remlen, remlen_len);
  // Write variable header
  write_buffer(&w_buffer, variable_hd, sizeof(variable_hd));

  // Send (no payload)
  send_buffer(&w_buffer, conn);
}

/******************************************************************************
 * Encodes MQTT CONNECT
 *
 *******************************************************************************/
void ICACHE_FLASH_ATTR
mqtt_connect(struct mqtt_connection *conn)
{
  reset_buffer(&w_buffer);

  // Packet headers
  uint8_t fixed_hd;
  uint8_t remlen_len, remlen[4];

  // Flags (user and password always set)
  /*
      7   |     6    |     5      |   4    3   |    2    |     1     |    0
     user |    pwd   |   wretain  |    wqos    |  wflag  |  session  | reserved
  */
  uint8_t flags = 0b11000000;
  if(conn->clean_session)
    flags |= 0x02;
  if(conn->last_will.topic != NULL)
  {
    flags |= 0x04;
    flags |= (conn->last_will.qos << 3) | (conn->last_will.retain << 5);
  }

  uint8_t variable_hd[10] = {0x00, 0x04, 'M', 'Q', 'T', 'T', 4, flags, 0x00, 0x00};

  // Reset packet ids
  conn->packet_id = 1;

  // String lengths
  uint8_t strs_cnt = 3;
  const int8_t cli_len = os_strlen(conn->client_id);
  const int8_t user_len = os_strlen(conn->username);
  const int8_t pwd_len = os_strlen(conn->password);
  int8_t lw_topic_len = 0;
  int8_t lw_data_len = 0;
  if(conn->last_will.topic != NULL)
  {
    lw_topic_len = os_strlen(conn->last_will.topic);
    ++strs_cnt;
  }
  if(conn->last_will.data != NULL)
  {
    lw_data_len = os_strlen(conn->last_will.data);
    ++strs_cnt;
  }

  // Extra bytes (2 for each string encoded using "encode_str")
  const int8_t strs_len_bytes = strs_cnt * 2;

  // Fixed header
  fixed_hd = mqtt_header(MQTT_CONNECT, 0, 0, 0, 0);
  remlen_len = encode_mbi(sizeof(variable_hd) + cli_len + user_len + pwd_len + lw_topic_len + lw_data_len + strs_len_bytes, remlen);
  // Variable header
  encode_uint16(conn->kalive, variable_hd, 8);

  // Write fixed header
  write_buffer(&w_buffer, &fixed_hd, 1);
  write_buffer(&w_buffer, remlen, remlen_len);
  // Write variable header
  write_buffer(&w_buffer, variable_hd, sizeof(variable_hd));
  // Write payload (must use this order)
  encode_str(&w_buffer, conn->client_id, cli_len);
  if(lw_topic_len > 0)
    encode_str(&w_buffer, conn->last_will.topic, lw_topic_len);
  if(lw_data_len > 0)
    encode_str(&w_buffer, conn->last_will.data, lw_data_len);
  encode_str(&w_buffer, conn->username, user_len);
  encode_str(&w_buffer, conn->password, pwd_len);

  // Send packet
  send_buffer(&w_buffer, conn);
}

/******************************************************************************
 * Encodes MQTT DISCONNECT
 *
 *******************************************************************************/
void ICACHE_FLASH_ATTR
mqtt_disconnect(struct mqtt_connection *conn)
{
  reset_buffer(&w_buffer);

  // Write full packet (no variable header, no payload)
  uint8_t packet[] = { mqtt_header(MQTT_DISCONNECT, 0, 0, 0, 0), 0 };
  write_buffer(&w_buffer, packet, sizeof(packet));

  // Send packet
  send_buffer(&w_buffer, conn);
}

/******************************************************************************
 * Encodes MQTT SUBSCRIBE
 *
 * This implementation apply one topic filter on each message
 *
 *******************************************************************************/
void ICACHE_FLASH_ATTR
mqtt_subscribe(struct mqtt_connection *conn, char *topic, enum mqtt_qos qos)
{
  reset_buffer(&w_buffer);

  // Packet headers
  uint8_t fixed_hd;
  uint8_t remlen_len, remlen[4];
  uint8_t variable_hd[2];

  // Lengths
  const int8_t topic_len = os_strlen(topic);
  const int8_t qos_len = 1;
  // Extra bytes (2 for each string encoded using "encode_str")
  const int8_t strs_len_bytes = 2;

  // Fixed header
  fixed_hd = mqtt_header(MQTT_SUBSCRIBE, 0, 0, 1, 0);
  remlen_len = encode_mbi(sizeof(variable_hd) + topic_len + qos_len + strs_len_bytes, remlen);
  // Variable header
  encode_packet_id(conn, variable_hd);

  // Write fixed header
  write_buffer(&w_buffer, &fixed_hd, 1);
  write_buffer(&w_buffer, remlen, remlen_len);
  // Write variable header
  write_buffer(&w_buffer, variable_hd, sizeof(variable_hd));
  // Write payload
  uint8_t qos_data = qos;
  encode_str(&w_buffer, topic, topic_len);
  write_buffer(&w_buffer, &qos_data, qos_len);

  // Send packet
  send_buffer(&w_buffer, conn);
}

/******************************************************************************
 * Encodes MQTT UNSUBSCRIBE
 *
 * This implementation apply one topic filter on each message
 *
 *******************************************************************************/
void ICACHE_FLASH_ATTR
mqtt_unsubscribe(struct mqtt_connection *conn, char *topic)
{
  reset_buffer(&w_buffer);

  // Packet headers
  uint8_t fixed_hd;
  uint8_t remlen_len, remlen[4];
  uint8_t variable_hd[2];

  // Lengths
  const int8_t topic_len = os_strlen(topic);
  // Extra bytes (2 for each string encoded using "encode_str")
  const int8_t strs_len_bytes = 2;

  // Fixed header
  fixed_hd = mqtt_header(MQTT_UNSUBSCRIBE, 0, 0, 1, 0);
  remlen_len = encode_mbi(sizeof(variable_hd) + topic_len + strs_len_bytes, remlen);
  // Variable header
  encode_packet_id(conn, variable_hd);

  // Write fixed header
  write_buffer(&w_buffer, &fixed_hd, 1);
  write_buffer(&w_buffer, remlen, remlen_len);
  // Write variable header
  write_buffer(&w_buffer, variable_hd, sizeof(variable_hd));
  // Write payload
  encode_str(&w_buffer, topic, topic_len);

  // Send packet
  send_buffer(&w_buffer, conn);
}

/******************************************************************************
 * Encodes MQTT PUBLISH
 *
 * This implementation doesn't support Qos 2 (exactly once) delivery
 *
 *******************************************************************************/
void ICACHE_FLASH_ATTR
mqtt_publish(struct mqtt_connection *conn, char *topic, uint8_t *message, enum mqtt_qos qos, bool retain)
{
  if(qos == MQTT_QOS_2)
    return;

  reset_buffer(&w_buffer);

  // Packet headers
  uint8_t fixed_hd;
  uint8_t remlen_len, remlen[4];

  // Lengths
  uint16_t topic_len = os_strlen(topic);
  uint16_t message_len = os_strlen(message);
  // Extra bytes (2 for each string encoded using "encode_str")
  const int8_t strs_len_bytes = 2;

  // Fixed header (dup always 0)
  const uint8_t dup = 0;
  fixed_hd = mqtt_header(MQTT_PUBLISH, dup, qos, qos, (retain ? 1 : 0));
  remlen_len = encode_mbi(topic_len + message_len + strs_len_bytes, remlen);

  // Write fixed header
  write_buffer(&w_buffer, &fixed_hd, 1);
  write_buffer(&w_buffer, remlen, remlen_len);
  // Write payload
  encode_str(&w_buffer, topic, topic_len);
  write_buffer(&w_buffer, message, message_len);

  // Send packet
  send_buffer(&w_buffer, conn);
}

/******************************************************************************
 * Encodes MQTT PINGREQ
 *
 *******************************************************************************/
void ICACHE_FLASH_ATTR
mqtt_ping(struct mqtt_connection *conn)
{
  reset_buffer(&w_buffer);

  // Write full packet (no variable header, no payload)
  uint8_t packet[] = { mqtt_header(MQTT_PINGREQ, 0, 0, 0, 0), 0 };
  write_buffer(&w_buffer, packet, sizeof(packet));

  // Send packet
  send_buffer(&w_buffer, conn);
}

//
// MQTT CALLBACK HANDLERS
//


/******************************************************************************
 * Handle MQTT CONNACK
 *
 *******************************************************************************/
static void ICACHE_FLASH_ATTR
handle_connack(struct mqtt_connection *conn, struct mqtt_buffer *buffer)
{
    // Return code (2nd byte Variable header)
    enum mqtt_connack_status status = buffer->data[1];
    conn->connect_cb(conn, status);
}

/******************************************************************************
 * Handle MQTT PUBLISH
 *
 *******************************************************************************/
static void ICACHE_FLASH_ATTR
handle_publish(struct mqtt_connection *conn, struct mqtt_buffer *buffer)
{
    // QoS (2nd bit on 1st byte Variable header)
    enum mqtt_qos qos = (buffer->data[0] & 0x06);
    struct mqtt_message message = {};
    uint16_t packet_id = 0;

    packet_id = mqtt_publish_decode(buffer, &message, qos);
    conn->message_cb(conn, &message);
    os_free(message.topic);
    os_free(message.data);

    // Reply with PUBACK
    if(packet_id > 0 && qos == MQTT_QOS_1)
      mqtt_puback(conn, packet_id);
}

/******************************************************************************
 * Handle MQTT SUBACK
 *
 *******************************************************************************/
static void ICACHE_FLASH_ATTR
handle_suback(struct mqtt_connection *conn, struct mqtt_buffer *buffer)
{
  // Read packet id
  uint16_t packet_id = decode_uint16(buffer->data, 0);

  // Read return code
  buffer->offset += 2;
  enum mqtt_suback_status status = buffer->data[buffer->offset];

  // Callback
  conn->subscribe_cb(conn, status, packet_id);
}

/******************************************************************************
 * Parses MQTT packets
 *
 * This implementation ignores packets used on QoS 2 (exactly once)
 *
 *******************************************************************************/
void ICACHE_FLASH_ATTR
mqtt_parse_packet(struct mqtt_connection *conn, uint8_t *data, int data_len)
{
  struct mqtt_buffer r_buffer = {};
  r_buffer.data = (uint8_t*) os_malloc(MQTT_BUFFER_SIZE);

  // Packet type (upper nibble on 1st byte)
  enum mqtt_packet_type packet_type = data[0] >> 4;

  // Remaining lenght (2nd to 5th byte)
  uint8_t *remlen_offset = os_malloc(sizeof(uint8_t));
  int remlen_len = decode_mbi(&data[1], remlen_offset);

  // Buffer Variable header + Payload
  write_buffer(&r_buffer, data + 1 + *remlen_offset, remlen_len);

  // Callback
  switch (packet_type)
  {
    case MQTT_CONNACK:
      handle_connack(conn, &r_buffer);
      break;

    case MQTT_PUBLISH:
      handle_publish(conn, &r_buffer);
      break;

    case MQTT_SUBACK:
      handle_suback(conn, &r_buffer);
      break;

    case MQTT_UNSUBACK:
    case MQTT_PINGRESP:
      // No action required
      break;
  }

  os_free(remlen_offset);
  os_free(r_buffer.data);
}
