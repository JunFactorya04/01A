/**
 * @file multi_box_protocol.h
 * @brief MULTI BOX wire protocol — native ESP-NOW, one fixed-size packet type
 * @date 2026-09-06
 *
 * All boxes run the same firmware; role (START/FLASH/CENTER) is a config
 * value, not a build variant. CENTER is the coordinator for a shooting
 * session (owns sessionId generation); START/FLASH nodes only ever talk to
 * the single peer they've paired as CENTER, plus receive CENTER's broadcasts.
 */

#pragma once
#include <Arduino.h>

// ============ ROLES ============
enum class MBRole : uint8_t {
    NONE   = 0,
    START  = 1,
    FLASH  = 2,
    CENTER = 3,
};

// Only meaningful when role == START. A START-role node either opens a
// session (EMIT_START, watches while no session is active) or closes one
// (EMIT_END, watches only while a session IS active — the "finish line").
// This is how "multiple START nodes" (start-line + finish-line) stay a
// single role type instead of inventing a 4th role.
enum class MBSignalMode : uint8_t {
    EMIT_START = 0,
    EMIT_END   = 1,
};

// ============ COMMANDS ============
enum class MBCommand : uint8_t {
    HELLO      = 1,   // discovery broadcast (unpaired)
    HELLO_ACK  = 2,   // discovery reply (unpaired)
    READY      = 3,   // CENTER broadcast: session ended, system armed again
    START      = 4,   // START node -> CENTER: begin a shot; CENTER -> all: session began
    START_ACK  = 5,   // CENTER -> initiating START node: acknowledged
    FLASH_FIRE = 6,   // FLASH node -> CENTER: notification only (status/log)
    END_DETECT = 7,   // START(EMIT_END) node -> CENTER: finish-line crossed
    SHOT_DONE  = 8,   // CENTER broadcast: bulb closed, session over
    DISARM     = 9,   // CENTER broadcast (or self-applied): abort/reset, safe everywhere
    PING       = 10,
    PONG       = 11,
};

// ============ PACKET (packed, <= ESP_NOW_MAX_DATA_LEN=250) ============
struct __attribute__((packed)) MBPacket {
    uint8_t  command;      // MBCommand
    uint8_t  nodeId;       // sender's node id
    uint8_t  role;         // sender's configured MBRole, at send time
    uint16_t sessionId;    // 0 = not session-scoped (HELLO/HELLO_ACK/PING/PONG)
    uint16_t sequence;     // per-sender monotonically increasing (wraps); dup/replay guard
    uint32_t timestamp;    // sender's millis() at send time — informational only,
                            // clocks are not synchronized across boxes, so staleness
                            // is enforced locally at the receiver using ITS OWN
                            // receive-time, not this field (see MultiBoxController)
    uint8_t  payload[4];   // reserved for future use (e.g. HELLO_ACK peer-count hint)
};

#define MB_MAX_NAME_LEN 16
