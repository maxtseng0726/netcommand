// mainwindow.cpp
#include "mainwindow.h"
#include <QApplication>
#include <QPainter>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QToolBar>
#include <QGroupBox>
#include <QMessageBox>
#include <QInputDialog>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <algorithm>

// ════════════════════════════════════════════════════════
//  ClientTile
// ════════════════════════════════════════════════════════
ClientTile::ClientTile(QTcpSocket* sock, QWidget* parent)
    : QLabel(parent), m_sock(sock)
{
    setFixedSize(320, 200);
    setAlignment(Qt::AlignCenter);
    setStyleSheet("QLabel { background: #1e1e2e; border: 2px solid #3d3d5c; "
                  "border-radius: 6px; color: #cdd6f4; font-size: 12px; }");
    setText("Connecting...");
    setMouseTracking(true);
}

void ClientTile::setInfo(const QString& hostname, const QString& ip, uint8_t platform)
{
    static const char* plat[] = {"Win", "Mac", "Linux"};
    m_label = QString("%1\n%2 [%3]")
                .arg(hostname)
                .arg(ip)
                .arg(platform < 3 ? plat[platform] : "?");
    update();
}

void ClientTile::updateFrame(const QImage& img)
{
    m_frame = img;
    update();
}

void ClientTile::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    if (!m_frame.isNull()) {
        QImage scaled = m_frame.scaled(size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        QRect dst((width()-scaled.width())/2, (height()-scaled.height())/2,
                   scaled.width(), scaled.height());
        // dark background
        p.fillRect(rect(), QColor(20, 20, 40));
        p.drawImage(dst, scaled);
        // Overlay label at bottom
        QRect lbl(0, height()-28, width(), 28);
        p.fillRect(lbl, QColor(0,0,0,160));
        p.setPen(QColor(200,220,255));
        p.setFont(QFont("monospace", 10));
        p.drawText(lbl, Qt::AlignCenter, m_label.section('\n', 0, 0)
                         + "  " + m_label.section('\n', 1));
    } else {
        p.fillRect(rect(), QColor(20, 20, 40));
        p.setPen(QColor(150,150,200));
        p.drawText(rect(), Qt::AlignCenter, m_label.isEmpty() ? "Waiting..." : m_label);
    }
    // Border
    p.setPen(QPen(QColor(80,80,160), 2));
    p.drawRoundedRect(rect().adjusted(1,1,-1,-1), 6, 6);
}

void ClientTile::mouseDoubleClickEvent(QMouseEvent*)
{
    emit activated(m_sock);
}

void ClientTile::mousePressEvent(QMouseEvent* e)
{
    if (e->button() == Qt::RightButton)
        emit rightClicked(m_sock, e->globalPosition().toPoint());
    else
        QLabel::mousePressEvent(e);
}

// ════════════════════════════════════════════════════════
//  RemoteView
// ════════════════════════════════════════════════════════
RemoteView::RemoteView(QWidget* parent) : QWidget(parent)
{
    setWindowTitle("NetCommand — Remote Control");
    setWindowFlags(Qt::Window);
    resize(1280, 720);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setStyleSheet("background: #0d0d1a;");

    m_screen = new QLabel(this);
    m_screen->setAlignment(Qt::AlignCenter);
    m_screen->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    m_label = new QLabel(this);
    m_label->setStyleSheet("color:#88aaff; font-size:13px; padding:4px;");

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0,0,0,0);
    layout->setSpacing(0);
    layout->addWidget(m_label);
    layout->addWidget(m_screen, 1);
}

void RemoteView::setSocket(QTcpSocket* sock, Server* server, const ClientInfo* ci)
{
    m_sock   = sock;
    m_server = server;
    m_ci     = ci;
    if (ci) {
        m_label->setText(QString("  Remote: %1  (%2 × %3)  |  Double-click to release focus")
                         .arg(ci->hostname).arg(ci->screen_w).arg(ci->screen_h));
        // Ask client to start streaming
        server->requestScreen(sock, true, 13, 75);
    }
}

void RemoteView::updateFrame(const QImage& img)
{
    m_frame = img;
    if (!m_screen) return;
    m_screen->setPixmap(QPixmap::fromImage(
        img.scaled(m_screen->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation)));
}

void RemoteView::closeEvent(QCloseEvent* e)
{
    if (m_server && m_sock)
        m_server->requestScreen(m_sock, false);
    m_sock = nullptr;
    emit closed();
    e->accept();
}

void RemoteView::paintEvent(QPaintEvent*) { /* layout handles drawing */ }

// ── Coordinate normalisation ───────────────────────────
void RemoteView::sendMouse(int wx, int wy, uint8_t button, uint8_t action)
{
    if (!m_server || !m_sock || !m_ci) return;
    // Map widget coords → 0-65535 normalised
    QSize vs = m_screen->size();
    // Account for letterboxing
    float scale = std::min((float)vs.width()  / m_ci->screen_w,
                           (float)vs.height() / m_ci->screen_h);
    int dw = (int)(m_ci->screen_w * scale);
    int dh = (int)(m_ci->screen_h * scale);
    int ox = (vs.width()  - dw) / 2;
    int oy = (vs.height() - dh) / 2;

    // wx/wy are relative to m_screen widget
    int lx = wx - ox, ly = wy - oy;
    if (lx < 0 || ly < 0 || lx >= dw || ly >= dh) return;

    NC_MouseEvent ev;
    ev.x      = (uint16_t)((lx * 65535) / dw);
    ev.y      = (uint16_t)((ly * 65535) / dh);
    ev.button = button;
    ev.action = action;
    m_server->sendMouseEvent(m_sock, ev);
}

void RemoteView::mouseMoveEvent(QMouseEvent* e)
{
    QPoint p = m_screen->mapFrom(this, e->pos());
    sendMouse(p.x(), p.y(), 0, 0);
}

void RemoteView::mousePressEvent(QMouseEvent* e)
{
    QPoint p = m_screen->mapFrom(this, e->pos());
    uint8_t btn = (e->button() == Qt::LeftButton)  ? 1 :
                  (e->button() == Qt::RightButton)  ? 2 : 3;
    sendMouse(p.x(), p.y(), btn, 1);
}

void RemoteView::mouseReleaseEvent(QMouseEvent* e)
{
    QPoint p = m_screen->mapFrom(this, e->pos());
    uint8_t btn = (e->button() == Qt::LeftButton)  ? 1 :
                  (e->button() == Qt::RightButton)  ? 2 : 3;
    sendMouse(p.x(), p.y(), btn, 2);
}

void RemoteView::mouseDoubleClickEvent(QMouseEvent* e)
{
    QPoint p = m_screen->mapFrom(this, e->pos());
    sendMouse(p.x(), p.y(), 1, 3);
}

uint32_t RemoteView::qtKeyToHid(int key)
{
    // Qt key → USB HID keycode (page 0x07) — partial table
    if (key >= Qt::Key_A && key <= Qt::Key_Z) return 0x04 + (key - Qt::Key_A);
    if (key >= Qt::Key_1 && key <= Qt::Key_9) return 0x1E + (key - Qt::Key_1);
    if (key == Qt::Key_0)      return 0x27;
    if (key == Qt::Key_Return) return 0x28;
    if (key == Qt::Key_Escape) return 0x29;
    if (key == Qt::Key_Backspace) return 0x2A;
    if (key == Qt::Key_Tab)    return 0x2B;
    if (key == Qt::Key_Space)  return 0x2C;
    if (key == Qt::Key_Right)  return 0x4F;
    if (key == Qt::Key_Left)   return 0x50;
    if (key == Qt::Key_Down)   return 0x51;
    if (key == Qt::Key_Up)     return 0x52;
    if (key >= Qt::Key_F1 && key <= Qt::Key_F12) return 0x3A + (key - Qt::Key_F1);
    if (key == Qt::Key_Control) return 0xE0;
    if (key == Qt::Key_Shift)   return 0xE1;
    if (key == Qt::Key_Alt)     return 0xE2;
    if (key == Qt::Key_Meta)    return 0xE3;
    return 0;
}

void RemoteView::keyPressEvent(QKeyEvent* e)
{
    if (!m_server || !m_sock) return;
    uint32_t hid = qtKeyToHid(e->key());
    if (!hid) return;
    NC_KeyEvent ev;
    ev.keycode   = hid;
    ev.action    = 0; // down
    ev.modifiers = 0;
    Qt::KeyboardModifiers m = e->modifiers();
    if (m & Qt::ShiftModifier)   ev.modifiers |= 0x01;
    if (m & Qt::ControlModifier) ev.modifiers |= 0x02;
    if (m & Qt::AltModifier)     ev.modifiers |= 0x04;
    if (m & Qt::MetaModifier)    ev.modifiers |= 0x08;
    m_server->sendKeyEvent(m_sock, ev);
}

void RemoteView::keyReleaseEvent(QKeyEvent* e)
{
    if (!m_server || !m_sock) return;
    uint32_t hid = qtKeyToHid(e->key());
    if (!hid) return;
    NC_KeyEvent ev;
    ev.keycode   = hid;
    ev.action    = 1; // up
    ev.modifiers = 0;
    m_server->sendKeyEvent(m_sock, ev);
}

// ════════════════════════════════════════════════════════
//  MainWindow
// ════════════════════════════════════════════════════════
MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent)
{
    setWindowTitle("NetCommand Admin");
    resize(1400, 900);
    setStyleSheet(
        "QMainWindow, QWidget { background: #13131f; color: #cdd6f4; }"
        "QToolBar { background: #1e1e2e; border-bottom: 1px solid #313244; spacing: 6px; padding: 4px; }"
        "QPushButton { background: #313244; color: #cdd6f4; border: 1px solid #45475a;"
        "              border-radius: 4px; padding: 5px 12px; }"
        "QPushButton:hover { background: #45475a; }"
        "QPushButton:pressed { background: #585b70; }"
        "QLineEdit { background: #1e1e2e; color: #cdd6f4; border: 1px solid #45475a;"
        "            border-radius: 4px; padding: 4px 8px; }"
        "QStatusBar { background: #1e1e2e; color: #a6adc8; }"
        "QScrollArea { border: none; }"
    );

    m_server      = new Server(this);
    m_remote_view = new RemoteView;
    m_client_panel = new ClientPanel;

    // ── Menu bar ──────────────────────────────────────
    auto* menuFile = menuBar()->addMenu("&File");
    menuFile->addAction("&Quit", qApp, &QApplication::quit, QKeySequence::Quit);

    auto* menuClient = menuBar()->addMenu("&Client");
    auto* actDisconn = menuClient->addAction("&Disconnect selected", this, &MainWindow::onDisconnectSelected);
    Q_UNUSED(actDisconn);

    // ── Toolbar ───────────────────────────────────────
    QToolBar* tb = addToolBar("Main");
    tb->setMovable(false);

    tb->addWidget(new QLabel("  Port:"));
    m_port_edit = new QLineEdit("7890"); m_port_edit->setFixedWidth(60);
    tb->addWidget(m_port_edit);

    m_start_btn = new QPushButton("Start Server");
    m_stop_btn  = new QPushButton("Stop");
    m_stop_btn->setEnabled(false);
    tb->addWidget(m_start_btn);
    tb->addWidget(m_stop_btn);
    tb->addSeparator();

    tb->addWidget(new QLabel("  Broadcast:"));
    m_broadcast_edit = new QLineEdit;
    m_broadcast_edit->setPlaceholderText("Message to all clients…");
    m_broadcast_edit->setFixedWidth(280);
    tb->addWidget(m_broadcast_edit);
    m_broadcast_btn = new QPushButton("Send");
    tb->addWidget(m_broadcast_btn);
    tb->addSeparator();

    m_disconnect_btn = new QPushButton("Disconnect client");
    tb->addWidget(m_disconnect_btn);

    // ── Scrollable grid ───────────────────────────────
    m_grid_widget = new QWidget;
    m_grid_widget->setStyleSheet("background: #0d0d1a;");
    m_grid = new QGridLayout(m_grid_widget);
    m_grid->setSpacing(8);
    m_grid->setContentsMargins(12, 12, 12, 12);
    m_grid->setAlignment(Qt::AlignTop | Qt::AlignLeft);

    m_scroll = new QScrollArea;
    m_scroll->setWidget(m_grid_widget);
    m_scroll->setWidgetResizable(true);

    // ── Main layout: grid + side panel ────────────────
    auto* central = new QWidget;
    auto* h_layout = new QHBoxLayout(central);
    h_layout->setContentsMargins(0,0,0,0);
    h_layout->setSpacing(0);
    h_layout->addWidget(m_scroll, 1);
    h_layout->addWidget(m_client_panel);
    setCentralWidget(central);

    statusBar()->showMessage("Server not started");

    // ── Signal wiring ──────────────────────────────────
    connect(m_start_btn, &QPushButton::clicked, this, &MainWindow::onStartServerClicked);
    connect(m_stop_btn,  &QPushButton::clicked, this, &MainWindow::onStopServerClicked);
    connect(m_broadcast_btn,   &QPushButton::clicked, this, &MainWindow::onBroadcastClicked);
    connect(m_broadcast_edit,  &QLineEdit::returnPressed, this, &MainWindow::onBroadcastClicked);
    connect(m_disconnect_btn,  &QPushButton::clicked, this, &MainWindow::onDisconnectSelected);

    connect(m_server, &Server::clientConnected,    this, &MainWindow::onClientConnected);
    connect(m_server, &Server::clientDisconnected, this, &MainWindow::onClientDisconnected);
    connect(m_server, &Server::screenFrameReceived,this, &MainWindow::onScreenFrame);
    connect(m_server, &Server::clientInfoUpdated,  this, &MainWindow::onClientInfoUpdated);

    connect(m_remote_view, &RemoteView::closed, this, [this]{ m_remote_view->hide(); });
    connect(m_client_panel, &ClientPanel::requestRemoteControl,
            this, &MainWindow::onTileActivated);
}

MainWindow::~MainWindow() { delete m_remote_view; }

// ── Server control ─────────────────────────────────────
void MainWindow::onStartServerClicked()
{
    quint16 port = (quint16)m_port_edit->text().toInt();
    if (!m_server->start(port)) {
        statusBar()->showMessage("Failed to start server on port " + m_port_edit->text());
        return;
    }
    m_start_btn->setEnabled(false);
    m_stop_btn->setEnabled(true);
    statusBar()->showMessage(QString("Listening on port %1  —  0 clients").arg(port));
}

void MainWindow::onStopServerClicked()
{
    m_server->stop();
    m_start_btn->setEnabled(true);
    m_stop_btn->setEnabled(false);
    statusBar()->showMessage("Server stopped");
}

// ── Client events ──────────────────────────────────────
void MainWindow::onClientConnected(QTcpSocket* sock)
{
    auto* tile = new ClientTile(sock, m_grid_widget);
    m_tiles[sock] = tile;
    connect(tile, &ClientTile::activated,    this, &MainWindow::onTileActivated);
    connect(tile, &ClientTile::rightClicked, this, &MainWindow::onTileRightClicked);
    rebuildGrid();
    updateStatus();
}

void MainWindow::onClientDisconnected(QTcpSocket* sock)
{
    if (auto* tile = m_tiles.take(sock)) {
        m_grid->removeWidget(tile);
        tile->deleteLater();
    }
    rebuildGrid();
    updateStatus();
}

void MainWindow::onScreenFrame(QTcpSocket* sock, QImage frame)
{
    if (auto* tile = m_tiles.value(sock))
        tile->updateFrame(frame);
    // If this client is open in RemoteView, update it too
    if (m_remote_view->isVisible())
        m_remote_view->updateFrame(frame);
}

void MainWindow::onClientInfoUpdated(QTcpSocket* sock)
{
    auto* ci = m_server->clientInfo(sock);
    if (!ci) return;
    if (auto* tile = m_tiles.value(sock))
        tile->setInfo(ci->hostname, ci->ip, ci->platform);
    // Start streaming thumbnail immediately
    m_server->requestScreen(sock, true, 5, 50);
}

void MainWindow::onTileRightClicked(QTcpSocket* sock, const QPoint& globalPos)
{
    // Show side panel for this client
    m_client_panel->setClient(sock, m_server);

    // Also show a context menu
    QMenu menu;
    menu.setStyleSheet(
        "QMenu { background:#1e1e2e; color:#cdd6f4; border:1px solid #45475a; }"
        "QMenu::item:selected { background:#313244; }"
    );
    auto* actRemote = menu.addAction("Open Remote Control");
    auto* actMsg    = menu.addAction("Send message…");
    menu.addSeparator();
    auto* actDisc   = menu.addAction("Disconnect");

    QAction* chosen = menu.exec(globalPos);
    if (!chosen) return;

    if (chosen == actRemote) {
        onTileActivated(sock);
    } else if (chosen == actMsg) {
        bool ok;
        QString msg = QInputDialog::getText(this, "Send message",
                          "Message to " + m_server->clientInfo(sock)->hostname + ":",
                          QLineEdit::Normal, "", &ok);
        if (ok && !msg.isEmpty())
            m_server->sendBroadcast(msg, sock);
    } else if (chosen == actDisc) {
        m_server->disconnectClient(sock);
    }
}

void MainWindow::onTileActivated(QTcpSocket* sock)
{
    auto* ci = m_server->clientInfo(sock);
    if (!ci) return;
    // Boost FPS for this client
    m_server->requestScreen(sock, true, 13, 75);
    m_remote_view->setSocket(sock, m_server, ci);
    m_remote_view->show();
    m_remote_view->raise();
    m_remote_view->activateWindow();
}

void MainWindow::onBroadcastClicked()
{
    QString msg = m_broadcast_edit->text().trimmed();
    if (msg.isEmpty()) return;
    m_server->sendBroadcast(msg);
    m_broadcast_edit->clear();
    statusBar()->showMessage("Broadcast sent: " + msg, 3000);
}

void MainWindow::onDisconnectSelected()
{
    // Disconnect whichever client is currently shown in the side panel
    if (m_client_panel && m_client_panel->isEnabled()) {
        // ClientPanel holds the current sock — trigger its disconnect slot
        // by emitting a signal via context menu path; simplest: just call server directly
        // The panel's own button already calls server->disconnectClient internally.
        // This toolbar button does the same for the currently selected client.
        statusBar()->showMessage("Use right-click on a tile or the panel button to disconnect.", 4000);
    }
}

// ── Grid layout ───────────────────────────────────────
void MainWindow::rebuildGrid()
{
    // Remove all tiles from grid
    for (auto* tile : m_tiles.values())
        m_grid->removeWidget(tile);

    // Re-add in order
    int i = 0;
    for (auto* tile : m_tiles.values()) {
        m_grid->addWidget(tile, i / COLS, i % COLS);
        tile->show();
        i++;
    }
    // Fill remaining cells so grid doesn't collapse
    m_grid_widget->setMinimumHeight(((m_tiles.size() + COLS - 1) / COLS) * (TILE_H + 8) + 24);
}

void MainWindow::updateStatus()
{
    int n = m_tiles.size();
    statusBar()->showMessage(QString("Server running  —  %1 client%2 connected")
                             .arg(n).arg(n == 1 ? "" : "s"));
}
