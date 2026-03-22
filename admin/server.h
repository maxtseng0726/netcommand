#pragma once
// server.h — TCP server managing all connected clients
#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QHash>
#include <QTimer>
#include <QByteArray>
#include <QString>
#include <QImage>
#include "../common/protocol.h"

// ── Per-client session info ────────────────────────────
struct ClientInfo {
    QTcpSocket* socket    = nullptr;
    QString     hostname;
    QString     ip;
    uint16_t    screen_w  = 1920;
    uint16_t    screen_h  = 1080;
    uint8_t     platform  = 0;       // 0=Win 1=Mac 2=Linux
    bool        streaming = false;
    QByteArray  recv_buf;            // accumulator for partial reads
    quint32     seq       = 0;       // outgoing sequence counter
    QImage      last_frame;          // latest decoded screen frame
};

class Server : public QObject
{
    Q_OBJECT
public:
    explicit Server(QObject* parent = nullptr);
    ~Server();

    bool start(quint16 port = 7890);
    void stop();

    // Commands to one or all clients
    void sendBroadcast(const QString& msg, QTcpSocket* target = nullptr); // null=all
    void requestScreen(QTcpSocket* sock, bool enable, int fps = 12, int quality = 70);
    void sendMouseEvent(QTcpSocket* sock, const NC_MouseEvent& ev);
    void sendKeyEvent(QTcpSocket* sock, const NC_KeyEvent& ev);
    void disconnectClient(QTcpSocket* sock);

    QList<QTcpSocket*> connectedClients() const;
    ClientInfo*        clientInfo(QTcpSocket* sock);

signals:
    void clientConnected(QTcpSocket* sock);
    void clientDisconnected(QTcpSocket* sock);
    void screenFrameReceived(QTcpSocket* sock, QImage frame);
    void clientInfoUpdated(QTcpSocket* sock);

private slots:
    void onNewConnection();
    void onReadyRead();
    void onClientDisconnected();
    void onHeartbeatTick();

private:
    void processPacket(QTcpSocket* sock, const NC_Header& hdr, const QByteArray& payload);
    void sendPacket(QTcpSocket* sock, NC_Command cmd,
                    const QByteArray& payload, ClientInfo* ci);

    QTcpServer*              m_server;
    QHash<QTcpSocket*, ClientInfo*> m_clients;
    QTimer*                  m_heartbeat;
};
