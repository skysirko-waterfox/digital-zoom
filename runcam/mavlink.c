#include "mavlink.h"

#include <errno.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* write wrapper that retries on EINTR so callers don't need to care */
static bool write_all(int fd, const void *data, size_t len) {
    const uint8_t *ptr = data;
    while (len > 0) {
        ssize_t written = write(fd, ptr, len);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("write");
            return false;
        }
        ptr += (size_t)written;
        len -= (size_t)written;
    }
    return true;
}

static bool lookup_crc_extra(uint32_t msgid, uint8_t *extra) {
    switch (msgid) {
        case MAVLINK_MSG_ID_HEARTBEAT:
            *extra = MAVLINK_CRC_EXTRA_HEARTBEAT;
            return true;
        case MAVLINK_MSG_ID_STATUSTEXT:
            *extra = MAVLINK_CRC_EXTRA_STATUSTEXT;
            return true;
        default:
            return false;
    }
}

void mavlink_parser_reset(struct mavlink_parser *parser) {
    parser->state = STATE_WAIT_STX;
    parser->header_pos = 0;
    parser->payload_pos = 0;
    parser->payload_len = 0;
    parser->signature_pos = 0;
    parser->signed_frame = false;
    parser->mavlink2 = false;
}

bool mavlink_parser_feed(struct mavlink_parser *parser, uint8_t byte, struct mavlink_message *msg) {
    switch (parser->state) {
        case STATE_WAIT_STX:
            if (byte == MAVLINK_V2_STX) {
                parser->mavlink2 = true;
                parser->header_len_expected = 9;
                parser->header_pos = 0;
                parser->state = STATE_HEADER;
            } else if (byte == MAVLINK_V1_STX) {
                parser->mavlink2 = false;
                parser->header_len_expected = 5;
                parser->header_pos = 0;
                parser->state = STATE_HEADER;
            }
            break;
        case STATE_HEADER:
            parser->header[parser->header_pos++] = byte;
            if (parser->header_pos == parser->header_len_expected) {
                parser->payload_len = parser->header[0];
                parser->payload_pos = 0;
                if (parser->payload_len > sizeof(parser->payload)) {
                    mavlink_parser_reset(parser);
                    break;
                }
                if (parser->mavlink2) {
                    parser->incompat_flags = parser->header[1];
                    parser->signed_frame = (parser->incompat_flags & 0x01U) != 0;
                } else {
                    parser->signed_frame = false;
                }
                parser->state = parser->payload_len ? STATE_PAYLOAD : STATE_CRC1;
            }
            break;
        case STATE_PAYLOAD:
            parser->payload[parser->payload_pos++] = byte;
            if (parser->payload_pos == parser->payload_len) {
                parser->state = STATE_CRC1;
            }
            break;
        case STATE_CRC1:
            parser->crc_received = byte;
            parser->state = STATE_CRC2;
            break;
        case STATE_CRC2:
            parser->crc_received |= (uint16_t)byte << 8;
            if (parser->mavlink2 && parser->signed_frame) {
                parser->signature_pos = 0;
                parser->state = STATE_SIGNATURE;
            } else {
                parser->state = STATE_WAIT_STX;
                goto finalize;
            }
            break;
        case STATE_SIGNATURE:
            parser->signature_pos++;
            if (parser->signature_pos == MAVLINK_SIGNATURE_LEN) {
                parser->state = STATE_WAIT_STX;
                goto finalize;
            }
            break;
    }
    return false;

finalize: {
        msg->mavlink2 = parser->mavlink2;
        msg->payload_len = parser->payload_len;
        memcpy(msg->payload, parser->payload, parser->payload_len);
        uint8_t bytes_header[10];
        size_t count = parser->mavlink2 ? 9 : 5;
        memcpy(bytes_header, parser->header, count);

        uint16_t crc = 0xFFFF;
        crc = mavlink_crc_accumulate_buffer(bytes_header, count, crc);
        crc = mavlink_crc_accumulate_buffer(parser->payload, parser->payload_len, crc);

        if (parser->mavlink2) {
            msg->sysid = parser->header[4];
            msg->compid = parser->header[5];
            msg->msgid = (uint32_t)parser->header[6] |
                         ((uint32_t)parser->header[7] << 8) |
                         ((uint32_t)parser->header[8] << 16);
        } else {
            msg->sysid = parser->header[2];
            msg->compid = parser->header[3];
            msg->msgid = parser->header[4];
        }

        uint8_t extra = 0;
        if (!lookup_crc_extra(msg->msgid, &extra)) {
            return false;
        }
        crc = mavlink_crc_accumulate_buffer(&extra, 1, crc);
        if (crc == parser->crc_received) {
            return true;
        }
        return false;
    }
}

/*
 * Straight port of the MAVLink CRC routine; matches the upstream code so our
 * checksums are accepted by autopilots. MAVLink (the drone control protocol)
 * protects each frame with a 16-bit X25 CRC (polynomial 0x1021). Instead of
 * using a lookup table, the reference algorithm applies three XOR/shift
 * operations per byte. Reproducing the same bit fiddling keeps us in sync with
 * the rest of the MAVLink ecosystem. CRC stands for Cyclic Redundancy Check,
 * the standard “parity on steroids” technique for spotting corrupted data.
 */
uint16_t mavlink_crc_accumulate_buffer(const uint8_t *buf, size_t len, uint16_t crc) {
    for (size_t i = 0; i < len; ++i) {
        uint8_t tmp = buf[i] ^ (uint8_t)(crc & 0xFF);
        tmp ^= (tmp << 4);
        crc = (crc >> 8) ^ (tmp << 8) ^ (tmp << 3) ^ (tmp >> 4);
    }
    return crc;
}

/* Serialize and send a heartbeat frame for the provided system/component IDs. */
bool mavlink_send_heartbeat(int fd,
                            uint8_t seq,
                            uint8_t system_id,
                            uint8_t component_id) {
    uint8_t payload[9];
    uint32_t custom_mode = 0;
    memcpy(payload, &custom_mode, sizeof(custom_mode));
    payload[4] = 18; /* MAV_TYPE_ONBOARD_CONTROLLER */
    payload[5] = 8;  /* MAV_AUTOPILOT_INVALID */
    payload[6] = 0;  /* base mode */
    payload[7] = 0;  /* system status */
    payload[8] = 3;  /* MAV_STATE_STANDBY */

    uint8_t frame[32];
    size_t offset = 0;
    uint16_t crc = 0xFFFF;

    /* MAVLink v2 header layout */
    frame[offset++] = MAVLINK_V2_STX;
    frame[offset++] = sizeof(payload);
    frame[offset++] = 0;  /* incompat */
    frame[offset++] = 0;  /* compat */
    frame[offset++] = seq;
    frame[offset++] = system_id;
    frame[offset++] = component_id;
    frame[offset++] = 0;
    frame[offset++] = 0;
    frame[offset++] = 0;
    memcpy(&frame[offset], payload, sizeof(payload));
    offset += sizeof(payload);

    crc = mavlink_crc_accumulate_buffer(&frame[1], 9, crc);

    crc = mavlink_crc_accumulate_buffer(payload, sizeof(payload), crc);
    uint8_t extra = MAVLINK_CRC_EXTRA_HEARTBEAT; /* MAVLink heartbeat CRC extra byte */
    crc = mavlink_crc_accumulate_buffer(&extra, 1, crc);
    frame[offset++] = (uint8_t)(crc & 0xFF);
    frame[offset++] = (uint8_t)(crc >> 8);

    return write_all(fd, frame, offset);
}

int mavlink_read_message_by_id(int fd,
                               struct mavlink_parser *parser,
                               uint32_t target_msgid,
                               int timeout_ms,
                               struct mavlink_message *out_msg) {
    struct pollfd pfd = {.fd = fd, .events = POLLIN};
    int events = poll(&pfd, 1, timeout_ms);
    if (events <= 0 || !(pfd.revents & POLLIN)) {
        return 0;
    }

    uint8_t buffer[256];
    ssize_t read_len = read(fd, buffer, sizeof(buffer));
    if (read_len > 0) {
        for (ssize_t i = 0; i < read_len; ++i) {
            struct mavlink_message msg;
            if (mavlink_parser_feed(parser, buffer[i], &msg) && msg.msgid == target_msgid) {
                if (out_msg) {
                    *out_msg = msg;
                }
                return 1;
            }
        }
        return 0;
    }
    if (read_len == 0 || errno == EAGAIN || errno == EINTR) {
        return 0;
    }
    perror("read");
    return -1;
}
