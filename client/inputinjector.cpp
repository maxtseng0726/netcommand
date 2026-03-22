// inputinjector.cpp — cross-platform synthetic input
#include "inputinjector.h"
#include <stdint.h>

// ── USB HID → platform keycode translation (small portable table) ──────────
// Only the most common keys are listed; extend as needed.
// Source: USB HID Usage Tables 1.3, page 53 (Keyboard/Keypad page 0x07)

// ════════════════════════════════════════════════════════
//  WINDOWS — SendInput
// ════════════════════════════════════════════════════════
#if defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

bool nc_input_init(void)  { return true; }
void nc_input_shutdown(void) {}

// Map USB HID keycode → Windows Virtual Key code (partial table)
static WORD hid_to_vk(uint32_t hid)
{
    // Direct VK mappings for alphanumeric + common keys
    if (hid >= 0x04 && hid <= 0x1D) return 'A' + (hid - 0x04);      // A-Z
    if (hid >= 0x1E && hid <= 0x27) return (hid == 0x27) ? '0' : ('1' + (hid - 0x1E)); // 1-0
    static const WORD tab[] = {
        VK_RETURN, VK_ESCAPE, VK_BACK, VK_TAB, VK_SPACE,
        VK_OEM_MINUS, VK_OEM_PLUS, VK_OEM_4, VK_OEM_6, VK_OEM_5,
        0, VK_OEM_1, VK_OEM_7, VK_OEM_3, VK_OEM_COMMA, VK_OEM_PERIOD, VK_OEM_2,
        VK_CAPITAL,
        VK_F1,VK_F2,VK_F3,VK_F4,VK_F5,VK_F6,
        VK_F7,VK_F8,VK_F9,VK_F10,VK_F11,VK_F12,
        VK_SNAPSHOT, VK_SCROLL, VK_PAUSE,
        VK_INSERT, VK_HOME, VK_PRIOR, VK_DELETE, VK_END, VK_NEXT,
        VK_RIGHT, VK_LEFT, VK_DOWN, VK_UP
    };
    if (hid >= 0x28 && hid < 0x28 + (int)(sizeof(tab)/sizeof(tab[0])))
        return tab[hid - 0x28];
    if (hid == 0xE0) return VK_LCONTROL;
    if (hid == 0xE1) return VK_LSHIFT;
    if (hid == 0xE2) return VK_LMENU;
    if (hid == 0xE3) return VK_LWIN;
    if (hid == 0xE4) return VK_RCONTROL;
    if (hid == 0xE5) return VK_RSHIFT;
    if (hid == 0xE6) return VK_RMENU;
    if (hid == 0xE7) return VK_RWIN;
    return 0;
}

void nc_inject_mouse(const NC_MouseEvent* ev, int sw, int sh)
{
    INPUT in = {};
    in.type = INPUT_MOUSE;

    // Normalise to absolute coordinates (0-65535)
    in.mi.dx      = (LONG)((ev->x / 65535.0f) * 65535);
    in.mi.dy      = (LONG)((ev->y / 65535.0f) * 65535);
    in.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;

    if (ev->action == 1) { // down
        if (ev->button == 1) in.mi.dwFlags |= MOUSEEVENTF_LEFTDOWN;
        if (ev->button == 2) in.mi.dwFlags |= MOUSEEVENTF_RIGHTDOWN;
        if (ev->button == 3) in.mi.dwFlags |= MOUSEEVENTF_MIDDLEDOWN;
    } else if (ev->action == 2) { // up
        if (ev->button == 1) in.mi.dwFlags |= MOUSEEVENTF_LEFTUP;
        if (ev->button == 2) in.mi.dwFlags |= MOUSEEVENTF_RIGHTUP;
        if (ev->button == 3) in.mi.dwFlags |= MOUSEEVENTF_MIDDLEUP;
    } else if (ev->action == 3) { // double-click
        INPUT dbl[2] = {in, in};
        dbl[0].mi.dwFlags |= MOUSEEVENTF_LEFTDOWN;
        dbl[1].mi.dwFlags |= MOUSEEVENTF_LEFTUP;
        SendInput(2, dbl, sizeof(INPUT));
        SendInput(2, dbl, sizeof(INPUT));
        return;
    }
    SendInput(1, &in, sizeof(INPUT));
}

void nc_inject_key(const NC_KeyEvent* ev)
{
    INPUT in = {};
    in.type       = INPUT_KEYBOARD;
    in.ki.wVk     = hid_to_vk(ev->keycode);
    in.ki.dwFlags = (ev->action == 1) ? KEYEVENTF_KEYUP : 0;
    if (!in.ki.wVk) return;
    SendInput(1, &in, sizeof(INPUT));
}

// ════════════════════════════════════════════════════════
//  macOS — CoreGraphics CGEvent
// ════════════════════════════════════════════════════════
#elif defined(__APPLE__)

#include <CoreGraphics/CoreGraphics.h>
#include <ApplicationServices/ApplicationServices.h>

bool nc_input_init(void)  { return true; }
void nc_input_shutdown(void) {}

// HID → macOS CGKeyCode (partial)
static CGKeyCode hid_to_cgkey(uint32_t hid)
{
    if (hid >= 0x04 && hid <= 0x1D) {
        // A-Z: macOS keycodes are layout-dependent; use a small table
        static const CGKeyCode alpha[] = {
            0,11,8,2,14,3,5,4,34,38,40,37,46,45,31,35,12,15,1,17,32,9,13,7,16,6
        };
        return alpha[hid - 0x04];
    }
    switch (hid) {
        case 0x28: return 36;  // Return
        case 0x29: return 53;  // Escape
        case 0x2A: return 51;  // Backspace
        case 0x2B: return 48;  // Tab
        case 0x2C: return 49;  // Space
        case 0x4F: return 124; // Right
        case 0x50: return 123; // Left
        case 0x51: return 125; // Down
        case 0x52: return 126; // Up
        default:   return 0xFF;
    }
}

void nc_inject_mouse(const NC_MouseEvent* ev, int sw, int sh)
{
    CGFloat x = (ev->x / 65535.0f) * sw;
    CGFloat y = (ev->y / 65535.0f) * sh;
    CGPoint pt = CGPointMake(x, y);

    CGEventType type = kCGEventMouseMoved;
    CGMouseButton btn = kCGMouseButtonLeft;

    if (ev->action == 1) {
        if (ev->button == 1) type = kCGEventLeftMouseDown;
        if (ev->button == 2) { type = kCGEventRightMouseDown; btn = kCGMouseButtonRight; }
    } else if (ev->action == 2) {
        if (ev->button == 1) type = kCGEventLeftMouseUp;
        if (ev->button == 2) { type = kCGEventRightMouseUp; btn = kCGMouseButtonRight; }
    }

    CGEventRef e = CGEventCreateMouseEvent(NULL, type, pt, btn);
    if (ev->action == 3) CGEventSetIntegerValueField(e, kCGMouseEventClickState, 2);
    CGEventPost(kCGHIDEventTap, e);
    CFRelease(e);
}

void nc_inject_key(const NC_KeyEvent* ev)
{
    CGKeyCode kc = hid_to_cgkey(ev->keycode);
    if (kc == 0xFF) return;
    bool down = (ev->action == 0);
    CGEventRef e = CGEventCreateKeyboardEvent(NULL, kc, down);

    CGEventFlags flags = 0;
    if (ev->modifiers & 0x01) flags |= kCGEventFlagMaskShift;
    if (ev->modifiers & 0x02) flags |= kCGEventFlagMaskControl;
    if (ev->modifiers & 0x04) flags |= kCGEventFlagMaskAlternate;
    if (ev->modifiers & 0x08) flags |= kCGEventFlagMaskCommand;
    CGEventSetFlags(e, flags);

    CGEventPost(kCGHIDEventTap, e);
    CFRelease(e);
}

// ════════════════════════════════════════════════════════
//  Linux — X11 XSendEvent
// ════════════════════════════════════════════════════════
#else

#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/extensions/XTest.h>  // XTestFakeMotionEvent / XTestFakeKeyEvent

static Display* g_disp = nullptr;
static int      g_sw   = 0, g_sh = 0;

bool nc_input_init(void)
{
    g_disp = XOpenDisplay(nullptr);
    if (!g_disp) return false;
    g_sw = DisplayWidth(g_disp, DefaultScreen(g_disp));
    g_sh = DisplayHeight(g_disp, DefaultScreen(g_disp));
    return true;
}

void nc_input_shutdown(void)
{
    if (g_disp) { XCloseDisplay(g_disp); g_disp = nullptr; }
}

// HID → X11 KeySym (partial)
static KeySym hid_to_keysym(uint32_t hid)
{
    if (hid >= 0x04 && hid <= 0x1D) return XK_a + (hid - 0x04);
    if (hid >= 0x1E && hid <= 0x26) return XK_1 + (hid - 0x1E);
    if (hid == 0x27) return XK_0;
    switch (hid) {
        case 0x28: return XK_Return;
        case 0x29: return XK_Escape;
        case 0x2A: return XK_BackSpace;
        case 0x2B: return XK_Tab;
        case 0x2C: return XK_space;
        case 0x4F: return XK_Right;
        case 0x50: return XK_Left;
        case 0x51: return XK_Down;
        case 0x52: return XK_Up;
        default:   return NoSymbol;
    }
}

void nc_inject_mouse(const NC_MouseEvent* ev, int sw, int sh)
{
    if (!g_disp) return;
    int x = (int)((ev->x / 65535.0f) * sw);
    int y = (int)((ev->y / 65535.0f) * sh);
    XTestFakeMotionEvent(g_disp, -1, x, y, CurrentTime);

    if (ev->action == 1 || ev->action == 3) {
        unsigned int btn = ev->button ? ev->button : 1;
        XTestFakeButtonEvent(g_disp, btn, True, CurrentTime);
        if (ev->action == 3) {
            XSync(g_disp, False);
            XTestFakeButtonEvent(g_disp, btn, False, CurrentTime);
            XTestFakeButtonEvent(g_disp, btn, True,  CurrentTime);
        }
    } else if (ev->action == 2) {
        XTestFakeButtonEvent(g_disp, ev->button ? ev->button : 1, False, CurrentTime);
    }
    XFlush(g_disp);
}

void nc_inject_key(const NC_KeyEvent* ev)
{
    if (!g_disp) return;
    KeySym ks = hid_to_keysym(ev->keycode);
    if (ks == NoSymbol) return;
    KeyCode kc = XKeysymToKeycode(g_disp, ks);
    XTestFakeKeyEvent(g_disp, kc, ev->action == 0, CurrentTime);
    XFlush(g_disp);
}

#endif
