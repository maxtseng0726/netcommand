// client/main.cpp — NetCommand Client Daemon
//
// One-click usage:
//   1. Place netcommand-client(.exe) next to config.txt
//   2. Edit config.txt: set server_ip and port
//   3. Double-click / run — client silently installs itself as a
//      system service and connects to the admin in the background.
//
// config.txt format (lines starting with # are comments):
//   server_ip = 192.168.1.100
//   port      = 7890
//
// CLI overrides (optional):
//   netcommand-client --uninstall
//   netcommand-client --run <ip> <port>   (used internally by service)

// ── Standard library ─────────────────────────────────────
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>
#include <fstream>
#include <sstream>

// ── Platform headers ──────────────────────────────────────
#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  pragma comment(lib, "ws2_32.lib")
#  include "win_service.h"
   static void daemonize() {}
   static void show_message(const char* msg) {
       MessageBoxA(NULL, msg, "NetCommand", MB_OK | MB_ICONINFORMATION | MB_TOPMOST);
   }
   // Return directory containing this executable (with trailing backslash)
   static std::string exe_dir() {
       char buf[MAX_PATH] = {};
       GetModuleFileNameA(NULL, buf, MAX_PATH);
       std::string s(buf);
       auto p = s.find_last_of("\\/");
       return (p == std::string::npos) ? "" : s.substr(0, p + 1);
   }
#else
#  include <unistd.h>
#  include <signal.h>
#  include <sys/stat.h>
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
#    include <CoreGraphics/CoreGraphics.h>
#    include <mach-o/dyld.h>
     static void show_message(const char* msg) {
         char cmd[512];
         snprintf(cmd, sizeof(cmd),
             "osascript -e 'display notification \"%s\" with title \"NetCommand\"'", msg);
         (void)system(cmd);
     }
     static std::string exe_dir() {
         char buf[1024] = {}; uint32_t sz = sizeof(buf);
         _NSGetExecutablePath(buf, &sz);
         std::string s(buf);
         auto p = s.find_last_of('/');
         return (p == std::string::npos) ? "" : s.substr(0, p + 1);
     }
#  else
     static void show_message(const char* msg) {
         char cmd[512];
         snprintf(cmd, sizeof(cmd), "notify-send 'NetCommand' '%s' &", msg);
         (void)system(cmd);
     }
     static std::string exe_dir() {
         char buf[1024] = {};
         ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf)-1);
         if (n < 0) return "";
         std::string s(buf, n);
         auto p = s.find_last_of('/');
         return (p == std::string::npos) ? "" : s.substr(0, p + 1);
     }
#  endif
#endif

#include "../common/nc_socket.h"
#include "../common/protocol.h"
#include "screencapture.h"
#include "inputinjector.h"

// ── Constants ─────────────────────────────────────────────
static const int DEFAULT_PORT         = 7890;
static const int RECONNECT_DELAY_MS   = 5000;
static const int HEARTBEAT_TIMEOUT_MS = 15000;

// ── Global state ──────────────────────────────────────────
static std::atomic<bool> g_running{true};
static std::atomic<bool> g_streaming{false};
static std::atomic<int>  g_fps{12};
static std::atomic<int>  g_quality{70};
static char              g_server_ip[64] = {};
static int               g_server_port   = DEFAULT_PORT;

// ── config.txt parser ─────────────────────────────────────
// Reads  server_ip = <ip>  and  port = <n>  from the file next to the binary.
// Returns true if server_ip was found.
static bool load_config(std::string& out_ip, int& out_port)
{
    std::string path = exe_dir() + "config.txt";
    std::ifstream f(path);
    if (!f.is_open()) return false;

    std::string line;
    while (std::getline(f, line)) {
        // Strip comments
        auto c = line.find('#');
        if (c != std::string::npos) line = line.substr(0, c);

        auto eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);

        // Trim whitespace
        auto trim = [](std::string& s) {
            size_t a = s.find_first_not_of(" \t\r\n");
            size_t b = s.find_last_not_of(" \t\r\n");
            s = (a == std::string::npos) ? "" : s.substr(a, b - a + 1);
        };
        trim(key); trim(val);

        if (key == "server_ip") out_ip   = val;
        if (key == "port")      out_port = std::stoi(val);
    }
    return !out_ip.empty();
}

// ── Sequence counter ──────────────────────────────────────
static uint32_t g_seq = 0;
static inline uint32_t next_seq() { return ++g_seq; }

// ── Hostname helper ───────────────────────────────────────
static void get_hostname(char* out, int maxlen)
{
#ifdef _WIN32
    DWORD len = (DWORD)maxlen;
    GetComputerNameA(out, &len);
#else
    gethostname(out, maxlen);
    out[maxlen-1] = '\0';
#endif
}

// ── Screen size helper ────────────────────────────────────
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
    *w = 1920; *h = 1080; // fallback; screencapture.cpp sets actual size
#endif
}

// ── Screen streaming thread ───────────────────────────────
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
            uint8_t* jpeg = nullptr; int jsz = 0;
            if (nc_frame_to_jpeg(&frame, g_quality, &jpeg, &jsz)) {
                NC_ScreenFrameHeader sfh;
                sfh.width    = (uint16_t)frame.width;
                sfh.height   = (uint16_t)frame.height;
                sfh.quality  = (uint8_t)g_quality;
                sfh._pad[0]  = sfh._pad[1] = sfh._pad[2] = 0;
                sfh.frame_id = next_seq();
                std::vector<uint8_t> pkt(sizeof(sfh) + jsz);
                memcpy(pkt.data(), &sfh, sizeof(sfh));
                memcpy(pkt.data() + sizeof(sfh), jpeg, jsz);
                free(jpeg);
                nc_send_packet(sock, CMD_SCREEN_FRAME,
                               pkt.data(), (uint32_t)pkt.size(), next_seq());
            }
            nc_capture_free(&frame);
        }
        int frame_ms = 1000 / g_fps;
        auto elapsed = duration_cast<milliseconds>(steady_clock::now() - t0).count();
        if (elapsed < frame_ms)
            std::this_thread::sleep_for(milliseconds(frame_ms - elapsed));
    }
}

// ── Command receive loop ──────────────────────────────────
static void command_loop(nc_sock_t sock)
{
    nc_set_timeout(sock, HEARTBEAT_TIMEOUT_MS);
    uint16_t sw = 1920, sh = 1080;
    get_screen_size(&sw, &sh);

    // Handshake
    NC_Hello hello = {};
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

    std::thread st(stream_thread, sock);

    while (g_running) {
        NC_Header hdr;
        if (!nc_recv_header(sock, &hdr)) break;

        std::vector<uint8_t> payload(hdr.length);
        if (hdr.length > 0)
            if (nc_recv_all(sock, payload.data(), (int)hdr.length) < 0) break;

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
            if ((int)payload.size() >= (int)sizeof(NC_ReqScreen)) {
                NC_ReqScreen* rs = (NC_ReqScreen*)payload.data();
                g_streaming = rs->enable != 0;
                if (rs->fps > 0 && rs->fps <= 30)  g_fps     = rs->fps;
                if (rs->quality > 0)               g_quality = rs->quality;
            }
            break;
        case CMD_MOUSE_EVENT:
            if ((int)payload.size() >= (int)sizeof(NC_MouseEvent)) {
                NC_MouseEvent* me = (NC_MouseEvent*)payload.data();
                me->x = ntohs(me->x); me->y = ntohs(me->y);
                nc_inject_mouse(me, sw, sh);
            }
            break;
        case CMD_KEY_EVENT:
            if ((int)payload.size() >= (int)sizeof(NC_KeyEvent)) {
                NC_KeyEvent* ke = (NC_KeyEvent*)payload.data();
                ke->keycode = ntohl(ke->keycode);
                nc_inject_key(ke);
            }
            break;
        case CMD_DISCONNECT:
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

// ── Core run loop ─────────────────────────────────────────
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
        if (g_running)
            std::this_thread::sleep_for(std::chrono::milliseconds(RECONNECT_DELAY_MS));
    }

    nc_input_shutdown();
    nc_capture_shutdown();
    nc_net_cleanup();
}

// ── Service install helpers (POSIX) ───────────────────────
#ifndef _WIN32
static std::string get_exe_path() {
#  if defined(__APPLE__)
    char buf[1024] = {}; uint32_t sz = sizeof(buf);
    _NSGetExecutablePath(buf, &sz);
    return std::string(buf);
#  else
    char buf[1024] = {};
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf)-1);
    return n > 0 ? std::string(buf, n) : std::string();
#  endif
}

static bool install_service(const std::string& ip, int port)
{
    std::string exe = get_exe_path();
    if (exe.empty()) { fprintf(stderr, "Cannot determine exe path\n"); return false; }

    // Also copy config.txt next to the installed binary path if needed
    // (the service binary IS itself, just registered)
#  if defined(__APPLE__)
    std::string plist_path = "/Library/LaunchDaemons/com.netcommand.client.plist";
    char plist[4096];
    snprintf(plist, sizeof(plist),
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\"\n"
        "  \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
        "<plist version=\"1.0\"><dict>\n"
        "  <key>Label</key><string>com.netcommand.client</string>\n"
        "  <key>ProgramArguments</key><array>\n"
        "    <string>%s</string>\n"
        "    <string>--run</string>\n"
        "    <string>%s</string>\n"
        "    <string>%d</string>\n"
        "  </array>\n"
        "  <key>RunAtLoad</key><true/>\n"
        "  <key>KeepAlive</key><true/>\n"
        "  <key>StandardOutPath</key><string>/dev/null</string>\n"
        "  <key>StandardErrorPath</key><string>/dev/null</string>\n"
        "</dict></plist>\n",
        exe.c_str(), ip.c_str(), port);
    FILE* f = fopen(plist_path.c_str(), "w");
    if (!f) { perror("fopen"); return false; }
    fputs(plist, f); fclose(f);
    chmod(plist_path.c_str(), 0644);
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "launchctl load -w \"%s\"", plist_path.c_str());
    (void)system(cmd);
    return true;
#  else
    char unit[2048];
    snprintf(unit, sizeof(unit),
        "[Unit]\nDescription=NetCommand Client Daemon\nAfter=network.target\n\n"
        "[Service]\nType=simple\n"
        "ExecStart=%s --run %s %d\n"
        "Restart=always\nRestartSec=5\n"
        "StandardOutput=null\nStandardError=null\n\n"
        "[Install]\nWantedBy=multi-user.target\n",
        exe.c_str(), ip.c_str(), port);
    FILE* f = fopen("/etc/systemd/system/netcommand-client.service", "w");
    if (!f) { perror("fopen"); return false; }
    fputs(unit, f); fclose(f);
    (void)system("systemctl daemon-reload");
    (void)system("systemctl enable --now netcommand-client");
    return true;
#  endif
}

static void uninstall_service()
{
#  if defined(__APPLE__)
    (void)system("launchctl unload -w /Library/LaunchDaemons/com.netcommand.client.plist");
    remove("/Library/LaunchDaemons/com.netcommand.client.plist");
#  else
    (void)system("systemctl disable --now netcommand-client");
    remove("/etc/systemd/system/netcommand-client.service");
    (void)system("systemctl daemon-reload");
#  endif
}
#endif // !_WIN32

// ════════════════════════════════════════════════════════
//  Entry point
// ════════════════════════════════════════════════════════
#ifdef _WIN32
int main(int argc, char* argv[])
{
    // Windows Service dispatch: --install / --uninstall / --run
    if (svc_dispatch_or_handle(argc, argv, run_client))
        return 0;

    // Hide console window for silent operation
    ShowWindow(GetConsoleWindow(), SW_HIDE);

    // --uninstall
    if (argc >= 2 && std::string(argv[1]) == "--uninstall") {
        svc_uninstall();
        return 0;
    }

    // Read config.txt for silent one-click operation
    std::string ip; int port = DEFAULT_PORT;
    if (!load_config(ip, port)) {
        MessageBoxA(NULL,
            "config.txt not found or missing server_ip.\n\n"
            "Create config.txt next to this .exe with:\n"
            "  server_ip = 192.168.1.100\n"
            "  port      = 7890",
            "NetCommand Client — Setup Required",
            MB_ICONWARNING);
        return 1;
    }

    // Install as Windows Service (autostart at boot), then run immediately
    g_svc_server_ip = ip;
    g_svc_port      = port;
    svc_install(ip, port);  // idempotent — skips if already installed

    // Also run directly now (service will take over after reboot)
    run_client(ip, port);
    return 0;
}

#else
int main(int argc, char* argv[])
{
    // --uninstall
    if (argc >= 2 && std::string(argv[1]) == "--uninstall") {
        uninstall_service();
        printf("NetCommand Client service removed.\n");
        return 0;
    }

    // --run <ip> <port>  — called by launchd / systemd
    if (argc >= 4 && std::string(argv[1]) == "--run") {
        daemonize();
        run_client(argv[2], atoi(argv[3]));
        return 0;
    }

    // Default: read config.txt, install service, run
    std::string ip; int port = DEFAULT_PORT;
    if (!load_config(ip, port)) {
        fprintf(stderr,
            "NetCommand Client: config.txt not found or missing server_ip.\n"
            "Create config.txt next to this binary:\n"
            "  server_ip = 192.168.1.100\n"
            "  port      = 7890\n");
        return 1;
    }

    // Install as system service (needs sudo on first run)
    install_service(ip, port);

    // Run directly as well (daemonize)
    daemonize();
    run_client(ip, port);
    return 0;
}
#endif
