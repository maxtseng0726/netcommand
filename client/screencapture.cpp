// screencapture.cpp — platform implementations
#include "screencapture.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// ── stb_image_write (JPEG encode, header-only) ──────────────────
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBIW_ASSERT(x)
#include "stb_image_write.h"

// JPEG write callback: appends to a growable buffer
struct JpegBuf { uint8_t* data; int size; };
static void jpeg_write_cb(void* ctx, void* data, int size)
{
    JpegBuf* jb = (JpegBuf*)ctx;
    jb->data = (uint8_t*)realloc(jb->data, jb->size + size);
    memcpy(jb->data + jb->size, data, size);
    jb->size += size;
}

bool nc_frame_to_jpeg(const NC_Frame* f, int quality,
                       uint8_t** out_data, int* out_size)
{
    JpegBuf jb = {nullptr, 0};
    // stbi wants RGB or RGBA; on Windows/Linux data is BGRA → swap to RGBA
#if defined(_WIN32) || defined(__linux__)
    // Convert BGRA → RGBA inline (in a temporary copy)
    int npix = f->width * f->height;
    uint8_t* rgba = (uint8_t*)malloc(npix * 4);
    if (!rgba) return false;
    const uint8_t* src = f->data;
    for (int i = 0; i < npix; i++) {
        rgba[i*4+0] = src[i*4+2]; // R ← B
        rgba[i*4+1] = src[i*4+1]; // G
        rgba[i*4+2] = src[i*4+0]; // B ← R
        rgba[i*4+3] = src[i*4+3]; // A
    }
    stbi_write_jpg_to_func(jpeg_write_cb, &jb, f->width, f->height, 4, rgba, quality);
    free(rgba);
#else
    // macOS: CGImage gives RGBA already
    stbi_write_jpg_to_func(jpeg_write_cb, &jb, f->width, f->height, 4, f->data, quality);
#endif
    if (!jb.data || jb.size == 0) return false;
    *out_data = jb.data;
    *out_size = jb.size;
    return true;
}

// ════════════════════════════════════════════════════════
//  WINDOWS — GDI backend
// ════════════════════════════════════════════════════════
#if defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static HDC     g_screen_dc  = NULL;
static HDC     g_mem_dc     = NULL;
static HBITMAP g_bitmap     = NULL;
static int     g_width      = 0;
static int     g_height     = 0;
static uint8_t* g_pixels    = nullptr;

bool nc_capture_init(void)
{
    g_screen_dc = GetDC(NULL);
    if (!g_screen_dc) return false;

    g_width  = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    g_height = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    g_mem_dc = CreateCompatibleDC(g_screen_dc);

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth       = g_width;
    bmi.bmiHeader.biHeight      = -g_height;   // top-down
    bmi.bmiHeader.biPlanes      = 1;
    bmi.bmiHeader.biBitCount    = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    g_bitmap = CreateDIBSection(g_screen_dc, &bmi, DIB_RGB_COLORS,
                                 (void**)&g_pixels, NULL, 0);
    if (!g_bitmap) return false;

    SelectObject(g_mem_dc, g_bitmap);
    return true;
}

bool nc_capture_grab(NC_Frame* f)
{
    if (!BitBlt(g_mem_dc, 0, 0, g_width, g_height,
                g_screen_dc, 0, 0, SRCCOPY | CAPTUREBLT))
        return false;
    GdiFlush();

    f->data   = g_pixels;
    f->width  = g_width;
    f->height = g_height;
    f->stride = g_width * 4;
    f->bpp    = 4;
    return true;
}

void nc_capture_free(NC_Frame*) { /* static buffer — nothing to free */ }

void nc_capture_shutdown(void)
{
    if (g_bitmap)    { DeleteObject(g_bitmap);   g_bitmap   = NULL; }
    if (g_mem_dc)    { DeleteDC(g_mem_dc);       g_mem_dc   = NULL; }
    if (g_screen_dc) { ReleaseDC(NULL,g_screen_dc); g_screen_dc = NULL; }
}

// ════════════════════════════════════════════════════════
//  macOS — CoreGraphics backend
// ════════════════════════════════════════════════════════
#elif defined(__APPLE__)

#include <CoreGraphics/CoreGraphics.h>
#include <CoreFoundation/CoreFoundation.h>

bool nc_capture_init(void) { return true; }

bool nc_capture_grab(NC_Frame* f)
{
    CGDirectDisplayID disp = CGMainDisplayID();
    CGImageRef img = CGDisplayCreateImage(disp);
    if (!img) return false;

    size_t w = CGImageGetWidth(img);
    size_t h = CGImageGetHeight(img);
    size_t stride = w * 4;

    uint8_t* buf = (uint8_t*)malloc(stride * h);
    if (!buf) { CGImageRelease(img); return false; }

    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    CGContextRef ctx = CGBitmapContextCreate(
        buf, w, h, 8, stride, cs,
        kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
    CGColorSpaceRelease(cs);

    CGContextDrawImage(ctx, CGRectMake(0, 0, w, h), img);
    CGContextRelease(ctx);
    CGImageRelease(img);

    f->data   = buf;
    f->width  = (int)w;
    f->height = (int)h;
    f->stride = (int)stride;
    f->bpp    = 4;
    return true;
}

void nc_capture_free(NC_Frame* f) { free(f->data); f->data = nullptr; }
void nc_capture_shutdown(void) {}

// ════════════════════════════════════════════════════════
//  Linux — X11 backend
// ════════════════════════════════════════════════════════
#else

#include <X11/Xlib.h>
#include <X11/Xutil.h>

static Display* g_display = nullptr;
static Window   g_root    = 0;
static int      g_width   = 0;
static int      g_height  = 0;

bool nc_capture_init(void)
{
    g_display = XOpenDisplay(nullptr);
    if (!g_display) return false;
    g_root = DefaultRootWindow(g_display);
    XWindowAttributes attr;
    XGetWindowAttributes(g_display, g_root, &attr);
    g_width  = attr.width;
    g_height = attr.height;
    return true;
}

bool nc_capture_grab(NC_Frame* f)
{
    XImage* img = XGetImage(g_display, g_root,
                             0, 0, g_width, g_height,
                             AllPlanes, ZPixmap);
    if (!img) return false;

    // XImage pixel format varies; most modern setups are 32-bpp BGRA
    int npix = g_width * g_height;
    uint8_t* buf = (uint8_t*)malloc(npix * 4);
    if (!buf) { XDestroyImage(img); return false; }

    // Copy; handle potential 24-bpp (no alpha byte)
    for (int i = 0; i < npix; i++) {
        int x = i % g_width, y = i / g_width;
        unsigned long px = XGetPixel(img, x, y);
        buf[i*4+0] = (px >> 16) & 0xFF;  // B (stored as R in XImage on most systems)
        buf[i*4+1] = (px >>  8) & 0xFF;  // G
        buf[i*4+2] = (px      ) & 0xFF;  // R (stored as B)
        buf[i*4+3] = 0xFF;
    }
    XDestroyImage(img);

    f->data   = buf;
    f->width  = g_width;
    f->height = g_height;
    f->stride = g_width * 4;
    f->bpp    = 4;
    return true;
}

void nc_capture_free(NC_Frame* f) { free(f->data); f->data = nullptr; }

void nc_capture_shutdown(void)
{
    if (g_display) { XCloseDisplay(g_display); g_display = nullptr; }
}

#endif
