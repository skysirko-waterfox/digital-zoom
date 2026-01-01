#ifndef MAVLINK_H
#define MAVLINK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MAVLINK_V2_STX ((uint8_t)0xFD)
#define MAVLINK_V2_HEADER_LEN 9
#define MAVLINK_SIGNATURE_LEN 13  /* bytes appended when a MAVLink v2 frame is signed */

enum {
    MAVLINK_MSG_ID_HEARTBEAT = 0,
    MAVLINK_MSG_ID_STATUSTEXT = 253,
};

enum {
    MAVLINK_CRC_EXTRA_HEARTBEAT = 50,
    MAVLINK_CRC_EXTRA_STATUSTEXT = 83,
};

struct mavlink_message {
    uint32_t msgid;
    uint8_t sysid;
    uint8_t compid;
    uint8_t payload_len;
    uint8_t payload[255];
};

struct mavlink_parser {
    enum {
        STATE_WAIT_STX,
        STATE_HEADER,
        STATE_PAYLOAD,
        STATE_CRC1,
        STATE_CRC2,
        STATE_SIGNATURE
    } state;
    uint8_t header[10];
    size_t header_len_expected;
    size_t header_pos;
    uint8_t payload[255];
    uint8_t payload_len;
    uint8_t payload_pos;
    uint8_t incompat_flags;
    bool signed_frame;
    uint8_t signature_pos;
    uint16_t crc_received;
};

void mavlink_parser_reset(struct mavlink_parser *parser);
bool mavlink_parser_feed(struct mavlink_parser *parser, uint8_t byte, struct mavlink_message *msg);

/* Accumulates MAVLink CRC over an arbitrary buffer */
uint16_t mavlink_crc_accumulate_buffer(const uint8_t *buf, size_t len, uint16_t crc);
/* Emits a heartbeat frame using MAVLink v2 framing */
bool mavlink_send_heartbeat(int fd,
                            uint8_t seq,
                            uint8_t system_id,
                            uint8_t component_id);

/* Polls, consumes bytes, and returns when target msgid is seen.
 * Returns 1 when message found, 0 on timeout/no match, -1 on fatal read error.
 */
int mavlink_read_message_by_id(int fd,
                               struct mavlink_parser *parser,
                               uint32_t target_msgid,
                               int timeout_ms,
                               struct mavlink_message *out_msg);

#endif /* MAVLINK_H */
