#pragma once
#include <stdint.h>
#include <stddef.h>

// ─────────────────────────────────────────────
//  NetCommand Protocol  (version 1)
//  All multi-byte integers are big-endian (network byte order).
// ─────────────────────────────────────────────

#define NC_MAGIC        0x4E43  // "NC"
#define NC_VERSION      1
#define NC_MAX_PAYLOAD  (4 * 1024 * 1024)   // 4 MB cap per packet

// ── Command types ────────────────────────────
typedef enum {
    // Client → Server
    CMD_HELLO       = 0x01,   // Initial handshake (sends hostname)
    CMD_PONG        = 0x02,   // Heartbeat reply
    CMD_SCREEN_FRAME= 0x03,   // JPEG-compressed screen frame
    CMD_ACK         = 0x04,   // Generic acknowledgement

    // Server → Client
    CMD_PING        = 0x10,   // Heartbeat probe
    CMD_BROADCAST   = 0x11,   // Text message to display
    CMD_REQ_SCREEN  = 0x12,   // Start / stop screen streaming
    CMD_MOUSE_EVENT = 0x13,   // Synthetic mouse move/click
    CMD_KEY_EVENT   = 0x14,   // Synthetic keyboard event
    CMD_DISCONNECT  = 0x1F,   // Graceful disconnect
} NC_Command;

// ── Packet header (12 bytes, fixed) ──────────
// ┌──────────┬─────────┬────────┬─────────────┐
// │ magic 2B │ ver  1B │ cmd 1B │ length   4B │
// │  seq  4B │                                │
// └──────────┴─────────┴────────┴─────────────┘
#pragma pack(push, 1)
typedef struct {
    uint16_t magic;       // NC_MAGIC
    uint8_t  version;     // NC_VERSION
    uint8_t  command;     // NC_Command
    uint32_t length;      // payload byte count (may be 0)
    uint32_t seq;         // sequence number (wraps)
} NC_Header;
#pragma pack(pop)

#define NC_HEADER_SIZE  sizeof(NC_Header)   // 12

// ── CMD_HELLO payload ────────────────────────
#pragma pack(push, 1)
typedef struct {
    char     hostname[64];   // null-terminated
    uint16_t screen_w;
    uint16_t screen_h;
    uint8_t  platform;       // 0=Windows 1=macOS 2=Linux
} NC_Hello;
#pragma pack(pop)

// ── CMD_SCREEN_FRAME payload ─────────────────
// Header is followed immediately by `length` bytes of JPEG data.
// The header below precedes that JPEG blob.
#pragma pack(push, 1)
typedef struct {
    uint16_t width;
    uint16_t height;
    uint8_t  quality;    // JPEG quality 1-100
    uint8_t  _pad[3];
    uint32_t frame_id;
} NC_ScreenFrameHeader;
#pragma pack(pop)

// ── CMD_REQ_SCREEN payload ───────────────────
#pragma pack(push, 1)
typedef struct {
    uint8_t  enable;     // 1=start streaming, 0=stop
    uint8_t  fps;        // requested FPS (1-30)
    uint8_t  quality;    // JPEG quality (10-95)
    uint8_t  _pad;
} NC_ReqScreen;
#pragma pack(pop)

// ── CMD_MOUSE_EVENT payload ──────────────────
#pragma pack(push, 1)
typedef struct {
    uint16_t x;          // normalised 0-65535 (maps to screen width)
    uint16_t y;          // normalised 0-65535 (maps to screen height)
    uint8_t  button;     // 0=none/move, 1=left, 2=right, 3=middle
    uint8_t  action;     // 0=move, 1=down, 2=up, 3=double-click
    uint8_t  _pad[2];
} NC_MouseEvent;
#pragma pack(pop)

// ── CMD_KEY_EVENT payload ────────────────────
#pragma pack(push, 1)
typedef struct {
    uint32_t keycode;    // platform-independent keycode (USB HID usage)
    uint8_t  action;     // 0=down, 1=up
    uint8_t  modifiers;  // bit0=Shift bit1=Ctrl bit2=Alt bit3=Super
    uint8_t  _pad[2];
} NC_KeyEvent;
#pragma pack(pop)

// ── CMD_BROADCAST payload ────────────────────
// Payload is raw UTF-8 text, length bytes, no null terminator required.

// ─────────────────────────────────────────────
//  Helper: build a header in-place
// ─────────────────────────────────────────────
#ifdef __cplusplus
#include <cstring>
#include <arpa/inet.h>  // htons / htonl  (POSIX); Winsock provides these too
static inline void nc_fill_header(NC_Header* h, NC_Command cmd,
                                   uint32_t payload_len, uint32_t seq)
{
    h->magic   = htons(NC_MAGIC);
    h->version = NC_VERSION;
    h->command = (uint8_t)cmd;
    h->length  = htonl(payload_len);
    h->seq     = htonl(seq);
}
static inline bool nc_check_header(const NC_Header* h)
{
    return ntohs(h->magic) == NC_MAGIC && h->version == NC_VERSION;
}
#endif
