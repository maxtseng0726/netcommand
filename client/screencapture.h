#pragma once
// ─────────────────────────────────────────────
//  screencapture.h  —  cross-platform screen grab
//  Returns a raw BGRA/RGBA pixel buffer; caller compresses to JPEG.
//
//  Platform backends:
//    Windows : GDI  (BitBlt into a DIBSection)
//    macOS   : CoreGraphics (CGDisplayCreateImageForRect)
//    Linux   : X11 XGetImage  (or XShm for speed)
// ─────────────────────────────────────────────
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint8_t* data;      // raw pixel bytes (BGRA on Win/Linux, RGBA on macOS)
    int      width;
    int      height;
    int      stride;    // bytes per row
    int      bpp;       // bytes per pixel (always 4)
} NC_Frame;

// Initialise capture backend; returns false on failure
bool nc_capture_init(void);

// Grab one full-screen frame; fills *f.
// Caller must call nc_capture_free() when done.
bool nc_capture_grab(NC_Frame* f);

// Free resources allocated by nc_capture_grab()
void nc_capture_free(NC_Frame* f);

// Cleanup backend (call on exit)
void nc_capture_shutdown(void);

// ─────────────────────────────────────────────
//  Inline JPEG encode using stb_image_write
//  (header-only, included once from screencapture.cpp)
// ─────────────────────────────────────────────
// Encodes *f into a heap-allocated JPEG buffer.
// *out_data must be free()'d by the caller.
bool nc_frame_to_jpeg(const NC_Frame* f, int quality,
                       uint8_t** out_data, int* out_size);
