#pragma once
// ─────────────────────────────────────────────
//  inputinjector.h  —  synthetic mouse + keyboard events
//
//  Platform backends:
//    Windows : SendInput (WinAPI)
//    macOS   : CoreGraphics CGEvent
//    Linux   : XSendEvent via X11 (or uinput for Wayland)
// ─────────────────────────────────────────────
#include <stdint.h>
#include <stdbool.h>
#include "../common/protocol.h"

// Call once at startup
bool nc_input_init(void);

// Inject a mouse event (coordinates are normalised 0-65535)
void nc_inject_mouse(const NC_MouseEvent* ev, int screen_w, int screen_h);

// Inject a keyboard event (keycode is USB HID usage page 0x07)
void nc_inject_key(const NC_KeyEvent* ev);

// Call on exit
void nc_input_shutdown(void);
