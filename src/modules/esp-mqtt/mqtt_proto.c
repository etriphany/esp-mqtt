#include <string.h>
#include <osapi.h>
#include <mem.h>

#include "modules/esp-mqtt/mqtt_proto.h"

// IO buffers
static struct mqtt_buffer w_buffer = {}, r_buffer = {};

#define mqtt_header(type, flag_3, flag_2, flag_1, flag_0) \
  (((type) << 4) | ((flag_3) << 3) | ((flag_2) << 2) | ((flag_1) << 1) | (flag_0))


/******************************************************************************
 * Reset/Initialize buffers
 *
 *******************************************************************************/
static void ICACHE_FLASH_ATTR
reset_buffer(bool write, bool read)
{
  if(write)
  {
     // Init if not
     if (w_buffer.data == NULL)
        w_buffer.data = (uint8_t*) os_malloc(MQTT_BUFFER_SIZE);
      w_buffer.offset = 0;
  }

  if(read)
  {
    // Init if not
    if (r_buffer.data == NULL)
      r_buffer.data = (uint8_t*) os_malloc(MQTT_BUFFER_SIZE * 2);
    r_buffer.offset = 0;
  }
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

/******************************************************************************
 * Decodes MQTT Publish
 *
 *******************************************************************************/
static struct mqtt_message* ICACHE_FLASH_ATTR
decode_publish(struct mqtt_buffer *buffer)
{
  uint16_t buffer_len = buffer->offset;
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
 * Encodes MQTT Pub Ack
 *
 *******************************************************************************/
static void ICACHE_FLASH_ATTR
mqtt_puback(struct mqtt_connection *conn)
{
  // Reset Write Buffer
  reset_buffer(true, false);

  // MQTT packet
  uint8_t packet[] = { mqtt_header(MQTT_PUBACK, 0, 0, 0, 0), 0 };
  write_buffer(&w_buffer, packet, sizeof(packet));

  // Send
  conn->send_cb(conn, w_buffer.data, w_buffer.offset);
}


/******************************************************************************
 * Encodes MQTT Connect
 *
 *******************************************************************************/
void ICACHE_FLASH_ATTR
mqtt_connect(struct mqtt_connection *conn)
{
  // Reset Write Buffer
  reset_buffer(true, false);

  // Packet headers
  uint8_t fixed_hd;
  uint8_t remlen_len, remlen[4];
  /*
      7   |     6    |     5      |   4    3   |    2    |     1     |    0
     user |    pwd   |   wretain  |    wqos    |  wflag  |  session  | reserved
  */
  uint8_t flags = 0b11000000;
  if(conn->clearSession)
    flags |= 0x02;

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
  write_buffer(&w_buffer, &fixed_hd, 1);
  write_buffer(&w_buffer, remlen, remlen_len);
  // MQTT variable header
  write_buffer(&w_buffer, variable_hd, sizeof(variable_hd));
  // MQTT payload
  encode_str(&w_buffer, conn->client_id, cli_len);
  encode_str(&w_buffer, conn->username, user_len);
  encode_str(&w_buffer, conn->password, pwd_len);

  // Send
  conn->send_cb(conn, w_buffer.data, w_buffer.offset);
}

/******************************************************************************
 * Encodes MQTT Disconnect
 *
 *******************************************************************************/
void ICACHE_FLASH_ATTR
mqtt_disconnect(struct mqtt_connection *conn)
{
  // Reset Write Buffer
  reset_buffer(true, false);

  // MQTT packet
  uint8_t packet[] = { mqtt_header(MQTT_DISCONNECT, 0, 0, 0, 0), 0 };
  write_buffer(&w_buffer, packet, sizeof(packet));

  // Send
  conn->send_cb(conn, w_buffer.data, w_buffer.offset);
}

/******************************************************************************
 * Encodes MQTT Subscribe
 * This implementation apply one topic filter on each message
 *
 *******************************************************************************/
void ICACHE_FLASH_ATTR
mqtt_subscribe(struct mqtt_connection *conn, char *topic, enum mqtt_qos qos)
{
  // Reset Write Buffer
  reset_buffer(true, false);

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
  write_buffer(&w_buffer, &fixed_hd, 1);
  write_buffer(&w_buffer, remlen, remlen_len);
  // MQTT variable header
  write_buffer(&w_buffer, variable_hd, sizeof(variable_hd));
  // MQTT payload
  uint8_t qos_data = qos;
  encode_str(&w_buffer, topic, topic_len);
  write_buffer(&w_buffer, &qos_data, qos_len);

  // Send
  conn->send_cb(conn, w_buffer.data, w_buffer.offset);
}

/******************************************************************************
 * Encodes MQTT Unsubscribe
 * This implementation apply one topic filter on each message
 *
 *******************************************************************************/
void ICACHE_FLASH_ATTR
mqtt_unsubscribe(struct mqtt_connection *conn, char *topic)
{
  // Reset Write Buffer
  reset_buffer(true, false);

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
  write_buffer(&w_buffer, &fixed_hd, 1);
  write_buffer(&w_buffer, remlen, remlen_len);
  // MQTT variable header
  write_buffer(&w_buffer, variable_hd, sizeof(variable_hd));
  // MQTT payload
  encode_str(&w_buffer, topic, topic_len);

  // Send
  conn->send_cb(conn, w_buffer.data, w_buffer.offset);
}

/******************************************************************************
 * Encodes MQTT Publish
 * This implementation doesn't support Qos 2 (exactly once) delivery
 *
 *******************************************************************************/
void ICACHE_FLASH_ATTR
mqtt_publish(struct mqtt_connection *conn, char *topic, uint8_t *message, enum mqtt_qos qos)
{
  if(qos == MQTT_QOS_2)
    return;

  // Reset Write Buffer
  reset_buffer(true, false);

  // Packet headers
  uint8_t fixed_hd;
  uint8_t remlen_len, remlen[4];

  // Lengths
  uint16_t topic_len = os_strlen(topic);
  uint16_t message_len = os_strlen(message);
  const int8_t strs_len = 2 * 2; // final payload includes 2 bytes for each string

  // Fixed header (dup = 0, retain = 0)
  fixed_hd = mqtt_header(MQTT_PUBLISH, 0, qos, qos, 0);
  remlen_len = encode_mbi(topic_len + message_len + strs_len, remlen);

  // MQTT fixed header
  write_buffer(&w_buffer, &fixed_hd, 1);
  write_buffer(&w_buffer, remlen, remlen_len);
  // MQTT payload
  encode_str(&w_buffer, topic, topic_len);
  encode_str(&w_buffer, message, message_len);

  // Send
  conn->send_cb(conn, w_buffer.data, w_buffer.offset);
}

/******************************************************************************
 * Encodes MQTT Ping
 *
 *******************************************************************************/
void ICACHE_FLASH_ATTR
mqtt_ping(struct mqtt_connection *conn)
{
  // Reset Write Buffer
  reset_buffer(true, false);

  // MQTT packet
  uint8_t packet[] = { mqtt_header(MQTT_PINGREQ, 0, 0, 0, 0), 0 };
  write_buffer(&w_buffer, packet, sizeof(packet));

  // Send
  conn->send_cb(conn, w_buffer.data, w_buffer.offset);
}

/******************************************************************************
 * Parses MQTT packet
 * This implementation ignores packets used on QoS 2 (exactly once)
 *
 *******************************************************************************/
void ICACHE_FLASH_ATTR
mqtt_parse_packet(struct mqtt_connection *conn, uint8_t *data, int data_len)
{
  struct mqtt_message* message;

  #if MQTT_PRINT_PACKET
  print_packet(data, data_len);
  #endif

  // Reset Read Buffer
  reset_buffer(false, true);

  // Packet type (upper nibble on 1st byte)
  uint8_t packet_type = data[0] >> 4;

  // Remaining lenght (2nd to 5th byte)
  uint8_t *remlen_offset = os_malloc(sizeof(uint8_t));
  int remlen_len = decode_mbi(&data[1], remlen_offset);

  // Buffer Variable header + Payload
  write_buffer(&r_buffer, data + 1 + *remlen_offset, remlen_len);

  // Callback
  switch (packet_type)
  {
    case MQTT_CONNACK:
      // Return code (2nd byte Variable header)
      if (r_buffer.data[1] == 0x00)
        conn->connect_cb(conn);
      break;

    case MQTT_PUBLISH:
      message = decode_publish(&r_buffer);
      conn->message_cb(conn, message);

      // QoS to PUBACK (2nd bit on 1st byte Variable header)
      enum mqtt_qos qos = (r_buffer.data[0] & 0x06);
      if(qos == MQTT_QOS_1)
        mqtt_puback(conn);
      break;

    // Server packets (no client action required)
    case MQTT_SUBACK:
    case MQTT_UNSUBACK:
    case MQTT_PINGRESP:
      break;
  }
}
