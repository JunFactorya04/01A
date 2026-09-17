/**
 * @file multi_box_protocol.h
 * @brief MULTI BOX wire protocol — native ESP-NOW, one fixed-size packet type
 * @date 2026-09-06
 *
 * All boxes run the same firmware; role (START/FLASH/MAIN) is a config
 * value, not a build variant. MAIN is the coordinator for a shooting
 * session (owns sessionId generation); START/FLASH nodes only ever talk to
 * the single peer they've paired as MAIN, plus receive MAIN's broadcasts.
 */

#pragma once
#include <Arduino.h>

// ============ ROLES ============
enum class MBRole : uint8_t {
    NONE  = 0,
    START = 1,
    FLASH = 2,
    // Numeric value deliberately unchanged from when this was called MAIN,
    // so boxes that already persisted a role keep it across the rename.
    MAIN  = 3,
};

// ============ COMMANDS ============
enum class MBCommand : uint8_t {
    HELLO      = 1,   // discovery broadcast (unpaired)
    HELLO_ACK  = 2,   // discovery reply (unpaired)
    READY      = 3,   // MAIN broadcast: session ended, system armed again
    START      = 4,   // START node -> MAIN: begin a shot; MAIN -> all: session began
    START_ACK  = 5,   // MAIN -> initiating START node: acknowledged
    FLASH_FIRE = 6,   // FLASH node -> MAIN: notification only (status/log)
    END_DETECT = 7,   // remote finish-line node -> MAIN: crossing detected.
                      //   Nothing in this firmware SENDS this any more -- MAIN
                      //   detects the finish line with its own TF-Luna. The
                      //   handler is kept so a finish line can be put on a
                      //   separate box later without a protocol change.
    SHOT_DONE  = 8,   // MAIN broadcast: bulb closed, session over
    DISARM     = 9,   // MAIN broadcast (or self-applied): abort/reset, safe everywhere
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
