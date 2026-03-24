// client/main.cpp — NetCommand Client Daemon
// Runs silently in background; connects to admin server,
// streams screen frames, and injects received input events.
//
// Build: see Makefile
// Run  : netcommand-client <server-ip> [port]          (direct/debug)
//        netcommand-client --install <server-ip> [port] (install as service)
//        netcommand-client --uninstall                  (remove service)

// ── Standard library — must come before platform headers ─
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>

// ── Platform headers ──────────────────────────────────────
#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  pragma comment(lib, "ws2_32.lib")
#  include "win_service.h"
   static void daemonize() { /* handled by Windows Service */ }
   static void show_message(const char* msg) {
       MessageBoxA(NULL, msg, "NetCommand", MB_OK | MB_ICONINFORMATION | MB_TOPMOST);
   }
#else
#  include <unistd.h>
#  include <signal.h>
#  include <sys/stat.h>    // chmod
   static void daemonize() {
       pid_t pid = fork();
       if (pid < 0) exit(EXIT_FAILURE);
       if (pid > 0) exit(EXIT_SUCCESS);
       setsid();
       (void)freopen("/dev/null", "r", stdin);
       (void)freopen("/dev/null", "w", stdout);
       (void)freopen("/dev/null", "w", stderr);
   }
#  if defined(__APPLE__)
#    include <CoreFoundation/CoreFoundation.h>
#    include <CoreGraphics/CoreGraphics.h>    // CGDirectDisplayID, CGMainDisplayID
#    include <mach-o/dyld.h>                 // _NSGetExecutablePath
     static void show_message(const char* msg) {
         char cmd[512];
         snprintf(cmd, sizeof(cmd),
             "osascript -e 'display notification \"%s\" with title \"NetCommand\"'", msg);
         (void)system(cmd);
     }
#  else
     static void show_message(const char* msg) {
         char cmd[512];
         snprintf(cmd, sizeof(cmd), "notify-send 'NetCommand' '%s' &", msg);
         (void)system(cmd);
     }
#  endif
#endif

#include "../common/nc_socket.h"
#include "../common/protocol.h"
#include "screencapture.h"
#include "inputinjector.h"

// ── Config ────────────────────────────────────────────
static const int    DEFAULT_PORT        = 7890;
static const int    RECONNECT_DELAY_MS  = 5000;
static const int    HEARTBEAT_TIMEOUT_MS= 15000;

// ── Global state ──────────────────────────────────────
static std::atomic<bool> g_running{true};
static std::atomic<bool> g_streaming{false};
static std::atomic<int>  g_fps{12};
static std::atomic<int>  g_quality{70};
static char              g_server_ip[64];
static int               g_server_port = DEFAULT_PORT;

// ── Utility ───────────────────────────────────────────
static uint32_t g_seq = 0;
static inline uint32_t next_seq() { return ++g_seq; }

// Get hostname for HELLO packet
static void get_hostname(char* out, int maxlen)
{
#ifdef _WIN32
    DWORD len = maxlen;
    GetComputerNameA(out, &len);
#else
    gethostname(out, maxlen);
    out[maxlen-1] = '\0';
#endif
}

// Get screen dimensions
static void get_screen_size(uint16_t* w, uint16_t* h)
{
#ifdef _WIN32
    *w = (uint16_t)GetSystemMetrics(SM_CXVIRTUALSCREEN);
    *h = (uint16_t)GetSystemMetrics(SM_CYVIRTUALSCREEN);
#elif defined(__APPLE__)
    CGDirectDisplayID d = CGMainDisplayID();
    *w = (uint16_t)CGDisplayPixelsWide(d);
    *h = (uint16_t)CGDisplayPixelsHigh(d);
#else
    // X11: read in screencapture init
    *w = 1920; *h = 1080; // fallback
#endif
}

// ── Screen streaming thread ───────────────────────────
static void stream_thread(nc_sock_t sock)
{
    using namespace std::chrono;

    while (g_running) {
        if (!g_streaming) {
            std::this_thread::sleep_for(milliseconds(100));
            continue;
        }

        auto t0 = steady_clock::now();

        NC_Frame frame;
        if (nc_capture_grab(&frame)) {
            uint8_t* jpeg_data = nullptr;
            int      jpeg_size = 0;

            if (nc_frame_to_jpeg(&frame, g_quality, &jpeg_data, &jpeg_size)) {
                // Build payload: NC_ScreenFrameHeader + JPEG bytes
                NC_ScreenFrameHeader sfh;
                sfh.width    = (uint16_t)frame.width;
                sfh.height   = (uint16_t)frame.height;
                sfh.quality  = (uint8_t)g_quality;
                sfh._pad[0]  = sfh._pad[1] = sfh._pad[2] = 0;
                sfh.frame_id = next_seq();

                static thread_local std::vector<uint8_t> pkt;
                pkt.resize(sizeof(sfh) + jpeg_size);
                memcpy(pkt.data(), &sfh, sizeof(sfh));
                memcpy(pkt.data() + sizeof(sfh), jpeg_data, jpeg_size);
                free(jpeg_data);

                nc_send_packet(sock, CMD_SCREEN_FRAME,
                               pkt.data(), (uint32_t)pkt.size(), next_seq());
            }
            nc_capture_free(&frame);
        }

        // Throttle to target FPS
        int frame_ms = 1000 / g_fps;
        auto elapsed = duration_cast<milliseconds>(steady_clock::now() - t0).count();
        if (elapsed < frame_ms)
            std::this_thread::sleep_for(milliseconds(frame_ms - elapsed));
    }
}

// ── Main command receive loop ─────────────────────────
static void command_loop(nc_sock_t sock)
{
    nc_set_timeout(sock, HEARTBEAT_TIMEOUT_MS);
    uint16_t sw = 1920, sh = 1080;
    get_screen_size(&sw, &sh);

    // Send HELLO
    NC_Hello hello;
    memset(&hello, 0, sizeof(hello));
    get_hostname(hello.hostname, sizeof(hello.hostname));
    hello.screen_w = htons(sw);
    hello.screen_h = htons(sh);
#ifdef _WIN32
    hello.platform = 0;
#elif defined(__APPLE__)
    hello.platform = 1;
#else
    hello.platform = 2;
#endif
    nc_send_packet(sock, CMD_HELLO, &hello, sizeof(hello), next_seq());

    // Start screen streaming thread
    std::thread st(stream_thread, sock);

    // Receive commands from admin
    while (g_running) {
        NC_Header hdr;
        if (!nc_recv_header(sock, &hdr)) {
            fprintf(stderr, "[client] connection lost\n");
            break;
        }

        // Read payload if present
        std::vector<uint8_t> payload(hdr.length);
        if (hdr.length > 0) {
            if (nc_recv_all(sock, payload.data(), (int)hdr.length) < 0) break;
        }

        switch ((NC_Command)hdr.command) {

        case CMD_PING:
            nc_send_packet(sock, CMD_PONG, nullptr, 0, next_seq());
            break;

        case CMD_BROADCAST:
            if (!payload.empty()) {
                std::string msg(payload.begin(), payload.end());
                show_message(msg.c_str());
            }
            break;

        case CMD_REQ_SCREEN:
            if (payload.size() >= sizeof(NC_ReqScreen)) {
                NC_ReqScreen* rs = (NC_ReqScreen*)payload.data();
                g_streaming = rs->enable != 0;
                if (rs->fps > 0 && rs->fps <= 30)  g_fps     = rs->fps;
                if (rs->quality > 0)               g_quality = rs->quality;
            }
            break;

        case CMD_MOUSE_EVENT:
            if (payload.size() >= sizeof(NC_MouseEvent)) {
                NC_MouseEvent* me = (NC_MouseEvent*)payload.data();
                me->x = ntohs(me->x);
                me->y = ntohs(me->y);
                nc_inject_mouse(me, sw, sh);
            }
            break;

        case CMD_KEY_EVENT:
            if (payload.size() >= sizeof(NC_KeyEvent)) {
                NC_KeyEvent* ke = (NC_KeyEvent*)payload.data();
                ke->keycode = ntohl(ke->keycode);
                nc_inject_key(ke);
            }
            break;

        case CMD_DISCONNECT:
            fprintf(stderr, "[client] graceful disconnect from server\n");
            g_running = false;
            break;

        default:
            break;
        }
    }

    g_streaming = false;
    g_running   = false;
    st.join();
}

// ── Core client run function (used by both direct and service mode) ──────────
static void run_client(const std::string& server_ip, int port)
{
    strncpy(g_server_ip, server_ip.c_str(), sizeof(g_server_ip)-1);
    g_server_port = port;

    nc_net_init();
    nc_capture_init();
    nc_input_init();

    while (g_running) {
        struct sockaddr_in addr = {};
        addr.sin_family = AF_INET;
        addr.sin_port   = htons((uint16_t)g_server_port);
        inet_pton(AF_INET, g_server_ip, &addr.sin_addr);

        nc_sock_t sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock == NC_INVALID_SOCK) {
            std::this_thread::sleep_for(std::chrono::milliseconds(RECONNECT_DELAY_MS));
            continue;
        }

        if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
            nc_close(sock);
            std::this_thread::sleep_for(std::chrono::milliseconds(RECONNECT_DELAY_MS));
            continue;
        }

        nc_set_nodelay(sock);
        g_running   = true;
        g_streaming = false;
        command_loop(sock);
        nc_close(sock);

        if (g_running) {
            g_running = true;
            std::this_thread::sleep_for(std::chrono::milliseconds(RECONNECT_DELAY_MS));
        }
    }

    nc_input_shutdown();
    nc_capture_shutdown();
    nc_net_cleanup();
}

// ── Entry point ───────────────────────────────────────
#ifdef _WIN32
int main(int argc, char* argv[])
{
    // Let the service wrapper intercept --install / --uninstall / --run
    if (svc_dispatch_or_handle(argc, argv, run_client))
        return 0;

    // Direct (debug) execution — hide console window
    ShowWindow(GetConsoleWindow(), SW_HIDE);

    if (argc < 2) {
        MessageBoxA(NULL,
            "Usage: netcommand-client.exe <server-ip> [port]\n"
            "       netcommand-client.exe --install <server-ip> [port]\n"
            "       netcommand-client.exe --uninstall",
            "NetCommand Client", MB_ICONINFORMATION);
        return 1;
    }
    run_client(argv[1], argc >= 3 ? atoi(argv[2]) : 7890);
    return 0;
}
#else
int main(int argc, char* argv[])
{
    if (argc < 2) {
        fprintf(stderr,
            "Usage: netcommand-client <server-ip> [port]\n"
            "       netcommand-client --install <server-ip> [port]\n"
            "       netcommand-client --uninstall\n");
        return 1;
    }

#if defined(__APPLE__)
    // ── macOS: --install → launchd plist ─────────────────
    if (argc >= 2 && std::string(argv[1]) == "--install") {
        if (argc < 3) { fprintf(stderr, "Usage: --install <server-ip> [port]\n"); return 1; }
        const char* ip   = argv[2];
        int         port = (argc >= 4) ? atoi(argv[3]) : 7890;
        char exe[1024]; uint32_t sz = sizeof(exe);
        _NSGetExecutablePath(exe, &sz);

        char plist[4096];
        snprintf(plist, sizeof(plist),
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
            "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\"\n"
            "  \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
            "<plist version=\"1.0\"><dict>\n"
            "  <key>Label</key><string>com.netcommand.client</string>\n"
            "  <key>ProgramArguments</key><array>\n"
            "    <string>%s</string>\n"
            "    <string>%s</string>\n"
            "    <string>%d</string>\n"
            "  </array>\n"
            "  <key>RunAtLoad</key><true/>\n"
            "  <key>KeepAlive</key><true/>\n"
            "  <key>StandardOutPath</key><string>/dev/null</string>\n"
            "  <key>StandardErrorPath</key><string>/dev/null</string>\n"
            "</dict></plist>\n",
            exe, ip, port);

        const char* plist_path =
            "/Library/LaunchDaemons/com.netcommand.client.plist";
        FILE* f = fopen(plist_path, "w");
        if (!f) { perror("fopen (need sudo?)"); return 1; }
        fputs(plist, f);
        fclose(f);
        chmod(plist_path, 0644);

        char cmd[512];
        snprintf(cmd, sizeof(cmd), "launchctl load -w \"%s\"", plist_path);
        system(cmd);
        printf("[install] launchd daemon installed and loaded.\n");
        return 0;
    }
    if (argc >= 2 && std::string(argv[1]) == "--uninstall") {
        const char* plist_path =
            "/Library/LaunchDaemons/com.netcommand.client.plist";
        char cmd[512];
        snprintf(cmd, sizeof(cmd), "launchctl unload -w \"%s\"", plist_path);
        system(cmd);
        remove(plist_path);
        printf("[uninstall] launchd daemon removed.\n");
        return 0;
    }
#else
    // ── Linux: --install → systemd unit ──────────────────
    if (argc >= 2 && std::string(argv[1]) == "--install") {
        if (argc < 3) { fprintf(stderr, "Usage: --install <server-ip> [port]\n"); return 1; }
        const char* ip   = argv[2];
        int         port = (argc >= 4) ? atoi(argv[3]) : 7890;

        char exe[1024] = {};
        ssize_t len = readlink("/proc/self/exe", exe, sizeof(exe)-1);
        if (len < 0) { perror("readlink"); return 1; }

        char unit[2048];
        snprintf(unit, sizeof(unit),
            "[Unit]\n"
            "Description=NetCommand Client Daemon\n"
            "After=network.target\n\n"
            "[Service]\n"
            "Type=simple\n"
            "ExecStart=%s %s %d\n"
            "Restart=always\n"
            "RestartSec=5\n"
            "StandardOutput=null\n"
            "StandardError=null\n\n"
            "[Install]\n"
            "WantedBy=multi-user.target\n",
            exe, ip, port);

        const char* unit_path = "/etc/systemd/system/netcommand-client.service";
        FILE* f = fopen(unit_path, "w");
        if (!f) { perror("fopen (need sudo?)"); return 1; }
        fputs(unit, f);
        fclose(f);

        system("systemctl daemon-reload");
        system("systemctl enable --now netcommand-client");
        printf("[install] systemd service installed and started.\n");
        return 0;
    }
    if (argc >= 2 && std::string(argv[1]) == "--uninstall") {
        system("systemctl disable --now netcommand-client");
        remove("/etc/systemd/system/netcommand-client.service");
        system("systemctl daemon-reload");
        printf("[uninstall] systemd service removed.\n");
        return 0;
    }
#endif  // Linux vs macOS

    daemonize();
    run_client(argv[1], argc >= 3 ? atoi(argv[2]) : 7890);
    return 0;
}
#endif  // _WIN32
