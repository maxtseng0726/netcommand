#pragma once
#include <QMainWindow>
#include <QGridLayout>
#include <QScrollArea>
#include <QLabel>
#include <QPushButton>
#include <QLineEdit>
#include <QTextEdit>
#include <QSplitter>
#include <QListWidget>
#include <QStatusBar>
#include <QHash>
#include <QTcpSocket>
#include "server.h"
#include "clientpanel.h"

// ── Thumbnail tile for each client in the grid ────────
class ClientTile : public QLabel
{
    Q_OBJECT
public:
    explicit ClientTile(QTcpSocket* sock, QWidget* parent = nullptr);
    void updateFrame(const QImage& img);
    void setInfo(const QString& hostname, const QString& ip, uint8_t platform);
    QTcpSocket* socket() const { return m_sock; }

signals:
    void activated(QTcpSocket* sock);      // double-click → full view
    void rightClicked(QTcpSocket* sock, const QPoint& globalPos);

protected:
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void paintEvent(QPaintEvent*) override;

private:
    QTcpSocket* m_sock;
    QImage      m_frame;
    QString     m_label;
};

// ── Full-screen remote control widget ─────────────────
class RemoteView : public QWidget
{
    Q_OBJECT
public:
    explicit RemoteView(QWidget* parent = nullptr);
    void setSocket(QTcpSocket* sock, Server* server, const ClientInfo* ci);
    void updateFrame(const QImage& img);

signals:
    void closed();

protected:
    void mouseMoveEvent(QMouseEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void keyReleaseEvent(QKeyEvent*) override;
    void closeEvent(QCloseEvent*) override;
    void paintEvent(QPaintEvent*) override;

private:
    void sendMouse(int x, int y, uint8_t button, uint8_t action);
    void sendKey(int qtkey, bool down, Qt::KeyboardModifiers mods);
    uint32_t qtKeyToHid(int key);

    QTcpSocket*      m_sock   = nullptr;
    Server*          m_server = nullptr;
    const ClientInfo* m_ci    = nullptr;
    QImage           m_frame;
    QLabel*          m_label;
    QLabel*          m_screen;
};

// ── Main window ───────────────────────────────────────
class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

private slots:
    void onClientConnected(QTcpSocket* sock);
    void onClientDisconnected(QTcpSocket* sock);
    void onScreenFrame(QTcpSocket* sock, QImage frame);
    void onClientInfoUpdated(QTcpSocket* sock);
    void onTileActivated(QTcpSocket* sock);

    void onBroadcastClicked();
    void onStartServerClicked();
    void onStopServerClicked();
    void onDisconnectSelected();
    void onTileRightClicked(QTcpSocket* sock, const QPoint& globalPos);

private:
    void rebuildGrid();
    void updateStatus();

    Server*       m_server;
    RemoteView*   m_remote_view;
    ClientPanel*  m_client_panel;

    // Toolbar widgets
    QLineEdit*    m_port_edit;
    QPushButton*  m_start_btn;
    QPushButton*  m_stop_btn;
    QLineEdit*    m_broadcast_edit;
    QPushButton*  m_broadcast_btn;
    QPushButton*  m_disconnect_btn;

    // Grid scroll area
    QScrollArea*  m_scroll;
    QWidget*      m_grid_widget;
    QGridLayout*  m_grid;

    QHash<QTcpSocket*, ClientTile*> m_tiles;

    static constexpr int TILE_W = 320;
    static constexpr int TILE_H = 200;
    static constexpr int COLS   = 4;  // 4 columns → 16+ tiles visible
};
