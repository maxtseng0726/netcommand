# NetCommand

A cross-platform LAN administration tool built in **C++ / Qt 6**.  
Consists of a GUI Admin console and a silent background Client daemon, supporting **Windows / macOS / Linux**.

> ⚠️ **Legal notice**: This tool is intended solely for use on devices and networks you own or have explicit administrative authority over. Unauthorized monitoring of others is illegal.

---

## Features

| Feature | Description |
|---------|-------------|
| Message Broadcast | Send pop-up notifications to all or specific clients |
| Screen Monitor | 16+ client thumbnail grid at 5 FPS with right-click context menu |
| Remote Control | Double-click any thumbnail to open a full-screen remote session (13 FPS) |
| Client Side Panel | Right-click a tile to view host details and adjust per-client FPS / JPEG quality |
| System Service | Client installs itself as a Windows Service, macOS launchd daemon, or Linux systemd unit |
| Auto-reconnect | Client retries the connection every 5 seconds after a disconnect |

---

## Project Structure

```
netcommand/
├── .github/
│   └── workflows/
│       ├── ci.yml              # CI: build check on every push (Win + macOS + Linux)
│       └── release.yml         # Release: build + package binaries on version tag
│
├── common/
│   ├── protocol.h              # Shared packet format and command definitions
│   └── nc_socket.h             # Cross-platform socket abstraction (Winsock / POSIX)
│
├── admin/                      # Admin side — Qt 6 GUI application
│   ├── main.cpp
│   ├── mainwindow.h / .cpp     # Main window: thumbnail grid, toolbar, context menu
│   ├── server.h / .cpp         # TCP server, session pool, packet parser
│   ├── clientpanel.h / .cpp    # Right sidebar: client details and per-client controls
│   ├── netcommand-admin.pro    # Qt project file
│   └── Makefile                # Wraps qmake + make
│
├── client/                     # Client side — headless background daemon
│   ├── main.cpp                # Main loop, auto-reconnect, command dispatch
│   ├── win_service.h           # Windows SCM service wrapper
│   ├── screencapture.h / .cpp  # Cross-platform screen capture + JPEG compression
│   ├── inputinjector.h / .cpp  # Cross-platform mouse and keyboard injection
│   └── Makefile
│
├── deploy-client.sh            # One-shot build + install script (Linux / macOS)
├── deploy-client.bat           # One-shot build + install script (Windows)
├── .gitignore
└── README.md
```

---

## Requirements

### Admin (GUI)

| Requirement | Details |
|-------------|---------|
| Qt 6 | `Qt::Widgets` + `Qt::Network` |
| Compiler | g++ or clang++ with C++17 |
| Platform | Windows / macOS / Linux |

### Client (Daemon)

| Platform | Dependencies |
|----------|-------------|
| Windows | MinGW-w64 or MSVC; Winsock2 (built-in) |
| macOS | Xcode Command Line Tools; CoreGraphics (built-in) |
| Linux | `libx11-dev`, `libxtst-dev`, g++ |
| All | `stb_image_write.h` — downloaded automatically by the Makefile |

---

## Building

### Admin

```bash
cd admin

# If qmake is in PATH
make

# Specify Qt path manually
make QTDIR=/usr/local/opt/qt6        # macOS (Homebrew)
make QTDIR=/opt/Qt/6.6/gcc_64        # Linux

# Build and run immediately
make run
```

### Client

**Linux / macOS**
```bash
cd client
make        # auto-detects platform, downloads stb, compiles
```

**Windows (MinGW, native)**
```bat
cd client
make
```

---

## Deploying the Client

### Option 1 — One-shot script (recommended)

**Linux / macOS** (requires `sudo`)
```bash
sudo ./deploy-client.sh 192.168.1.100
sudo ./deploy-client.sh 192.168.1.100 7890   # custom port
```

**Windows** (run as Administrator)
```bat
deploy-client.bat 192.168.1.100
deploy-client.bat 192.168.1.100 7890
```

The script automatically: installs dependencies → downloads stb → compiles → copies to system path → registers the daemon for boot-time autostart.

---

### Option 2 — Manual service registration

**Windows**
```bat
:: Install as a Windows Service (autostart at boot)
netcommand-client.exe --install 192.168.1.100 7890

:: Remove the service
netcommand-client.exe --uninstall

:: Run directly (debug / test, no service)
netcommand-client.exe 192.168.1.100 7890
```

**macOS** (requires `sudo`)
```bash
# Installs a launchd daemon plist to /Library/LaunchDaemons/
sudo ./netcommand-client --install 192.168.1.100 7890
sudo ./netcommand-client --uninstall
```

**Linux** (requires `sudo`)
```bash
# Installs a systemd unit to /etc/systemd/system/
sudo ./netcommand-client --install 192.168.1.100 7890
sudo ./netcommand-client --uninstall

# Service management
sudo systemctl status netcommand-client
sudo systemctl stop   netcommand-client
journalctl -u netcommand-client -f
```

---

## Using the Admin GUI

1. Enter a port number (default `7890`) and click **Start Server**.
2. Clients appear as thumbnail tiles automatically when they connect (5 FPS preview).
3. **Right-click** any tile to open the context menu and side panel:
   - Send a message to that specific client
   - Open remote control
   - Adjust FPS and JPEG quality independently
   - Disconnect the client
4. **Double-click** any tile to open a full-screen remote control window (auto-boosts to 13 FPS).
5. Use the toolbar broadcast field to send a message to **all** connected clients at once.

### Remote Control Window

| Input | Behaviour |
|-------|-----------|
| Mouse move | Syncs cursor position to remote |
| Left / right / middle click | Injected as click on remote |
| Double-click | Injected as double-click on remote |
| Keyboard | All keys forwarded (click the window first to capture focus) |
| Close window | Stream drops back to 5 FPS automatically |

---

## Protocol

```
Magic:      0x4E43  ("NC")
Version:    1
Header:     [magic 2B][version 1B][cmd 1B][length 4B][seq 4B]  =  12 bytes total
Byte order: Big-endian (network order)
Default port: TCP 7890
```

| Command | Direction | Description |
|---------|-----------|-------------|
| `CMD_HELLO` | C → S | Handshake: hostname, screen resolution, platform |
| `CMD_PING` / `CMD_PONG` | S ↔ C | Heartbeat every 5 seconds |
| `CMD_BROADCAST` | S → C | UTF-8 text pop-up message |
| `CMD_REQ_SCREEN` | S → C | Start / stop streaming; set FPS and quality |
| `CMD_SCREEN_FRAME` | C → S | JPEG-compressed frame with frame header |
| `CMD_MOUSE_EVENT` | S → C | Normalised coordinates (0–65535) + action |
| `CMD_KEY_EVENT` | S → C | USB HID keycode + press / release |
| `CMD_DISCONNECT` | S → C | Graceful disconnect |

---

## Cross-Platform API Reference

### Screen Capture

| Platform | API |
|----------|-----|
| Windows | `BitBlt` + GDI `DIBSection` |
| macOS | `CGDisplayCreateImage` (CoreGraphics) |
| Linux | X11 `XGetImage` |

### Input Injection

| Platform | API |
|----------|-----|
| Windows | `SendInput` |
| macOS | `CGEventPost(kCGHIDEventTap, ...)` |
| Linux | `XTestFakeMotionEvent` / `XTestFakeKeyEvent` (XTest extension) |

### System Service

| Platform | Mechanism | Config location |
|----------|-----------|----------------|
| Windows | Windows SCM | Registry (managed by SCM) |
| macOS | launchd | `/Library/LaunchDaemons/com.netcommand.client.plist` |
| Linux | systemd | `/etc/systemd/system/netcommand-client.service` |

---

## Performance

| Scenario | FPS | JPEG Quality | Estimated bandwidth (1080p) |
|----------|-----|-------------|----------------------------|
| Thumbnail grid preview | 5 | 50 | ~200 KB/s per client |
| Remote control session | 13 | 75 | ~800 KB/s |
| Custom (via side panel) | 1–30 | 10–95 | Depends on settings |

---

## Known Limitations

- **Wayland**: Input injection uses X11 XTest. On pure Wayland sessions, use `uinput` instead (requires root).
- **macOS Accessibility**: Remote control requires granting Accessibility permission to the client binary under *System Settings → Privacy & Security → Accessibility*.
- **Multi-monitor**: Currently captures the primary display. Support for additional monitors can be added by extending `CMD_REQ_SCREEN` with a display index field.
- **Encryption**: No encryption by default (designed for trusted LANs). TLS can be added via `QSslSocket` or OpenSSL.

---

## Quick Local Test

```bash
# Terminal 1 — start the admin GUI
./netcommand-admin

# Terminal 2 — run a client connecting to localhost
./netcommand-client 127.0.0.1 7890
```

---

## CI / CD

Every push to `main` or any pull request triggers the CI workflow, which compiles the client on all three platforms and the admin on Linux. Pushing a version tag (e.g. `v1.0.0`) triggers the release workflow, which builds, packages, and publishes binaries to GitHub Releases automatically.

```bash
git tag v1.0.0
git push origin v1.0.0
```

---

## License

MIT
