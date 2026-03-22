// server.cpp
#include "server.h"
#include <QDataStream>
#include <QHostAddress>
#include <arpa/inet.h>

Server::Server(QObject* parent) : QObject(parent)
{
    m_server   = new QTcpServer(this);
    m_heartbeat = new QTimer(this);
    connect(m_server,    &QTcpServer::newConnection, this, &Server::onNewConnection);
    connect(m_heartbeat, &QTimer::timeout,           this, &Server::onHeartbeatTick);
    m_heartbeat->start(5000);
}

Server::~Server() { stop(); }

bool Server::start(quint16 port)
{
    return m_server->listen(QHostAddress::Any, port);
}

void Server::stop()
{
    for (auto* sock : m_clients.keys()) sock->disconnectFromHost();
    m_server->close();
}

// ── Low-level send ─────────────────────────────────────
void Server::sendPacket(QTcpSocket* sock, NC_Command cmd,
                         const QByteArray& payload, ClientInfo* ci)
{
    NC_Header hdr;
    nc_fill_header(&hdr, cmd, (uint32_t)payload.size(), ++ci->seq);
    sock->write(reinterpret_cast<const char*>(&hdr), NC_HEADER_SIZE);
    if (!payload.isEmpty()) sock->write(payload);
}

// ── Public commands ────────────────────────────────────
void Server::sendBroadcast(const QString& msg, QTcpSocket* target)
{
    QByteArray utf8 = msg.toUtf8();
    auto targets = target ? QList<QTcpSocket*>{target} : m_clients.keys();
    for (auto* sock : targets) {
        if (auto* ci = m_clients.value(sock))
            sendPacket(sock, CMD_BROADCAST, utf8, ci);
    }
}

void Server::requestScreen(QTcpSocket* sock, bool enable, int fps, int quality)
{
    auto* ci = m_clients.value(sock);
    if (!ci) return;
    NC_ReqScreen rs;
    rs.enable  = enable ? 1 : 0;
    rs.fps     = (uint8_t)fps;
    rs.quality = (uint8_t)quality;
    rs._pad    = 0;
    ci->streaming = enable;
    sendPacket(sock, CMD_REQ_SCREEN, QByteArray(reinterpret_cast<const char*>(&rs), sizeof(rs)), ci);
}

void Server::sendMouseEvent(QTcpSocket* sock, const NC_MouseEvent& ev)
{
    auto* ci = m_clients.value(sock);
    if (!ci) return;
    NC_MouseEvent e = ev;
    e.x = htons(ev.x);
    e.y = htons(ev.y);
    sendPacket(sock, CMD_MOUSE_EVENT, QByteArray(reinterpret_cast<const char*>(&e), sizeof(e)), ci);
}

void Server::sendKeyEvent(QTcpSocket* sock, const NC_KeyEvent& ev)
{
    auto* ci = m_clients.value(sock);
    if (!ci) return;
    NC_KeyEvent e = ev;
    e.keycode = htonl(ev.keycode);
    sendPacket(sock, CMD_KEY_EVENT, QByteArray(reinterpret_cast<const char*>(&e), sizeof(e)), ci);
}

void Server::disconnectClient(QTcpSocket* sock)
{
    auto* ci = m_clients.value(sock);
    if (!ci) return;
    sendPacket(sock, CMD_DISCONNECT, {}, ci);
    sock->disconnectFromHost();
}

QList<QTcpSocket*> Server::connectedClients() const { return m_clients.keys(); }
ClientInfo*        Server::clientInfo(QTcpSocket* sock) { return m_clients.value(sock); }

// ── Slots ──────────────────────────────────────────────
void Server::onNewConnection()
{
    while (m_server->hasPendingConnections()) {
        QTcpSocket* sock = m_server->nextPendingConnection();
        auto* ci = new ClientInfo;
        ci->socket = sock;
        ci->ip     = sock->peerAddress().toString();
        m_clients[sock] = ci;

        connect(sock, &QTcpSocket::readyRead, this, &Server::onReadyRead);
        connect(sock, &QTcpSocket::disconnected, this, &Server::onClientDisconnected);
        emit clientConnected(sock);
    }
}

void Server::onClientDisconnected()
{
    auto* sock = qobject_cast<QTcpSocket*>(sender());
    if (!sock) return;
    delete m_clients.take(sock);
    sock->deleteLater();
    emit clientDisconnected(sock);
}

void Server::onHeartbeatTick()
{
    QByteArray empty;
    for (auto it = m_clients.begin(); it != m_clients.end(); ++it)
        sendPacket(it.key(), CMD_PING, empty, it.value());
}

// ── Packet parser ──────────────────────────────────────
void Server::onReadyRead()
{
    auto* sock = qobject_cast<QTcpSocket*>(sender());
    auto* ci   = m_clients.value(sock);
    if (!ci) return;

    ci->recv_buf.append(sock->readAll());

    while (true) {
        // Need at least a header
        if ((int)ci->recv_buf.size() < (int)NC_HEADER_SIZE) break;

        NC_Header hdr;
        memcpy(&hdr, ci->recv_buf.constData(), NC_HEADER_SIZE);
        hdr.magic  = ntohs(hdr.magic);
        hdr.length = ntohl(hdr.length);
        hdr.seq    = ntohl(hdr.seq);

        if (hdr.magic != NC_MAGIC || hdr.version != NC_VERSION) {
            // Protocol mismatch — drop connection
            sock->disconnectFromHost();
            return;
        }

        int total = NC_HEADER_SIZE + (int)hdr.length;
        if (ci->recv_buf.size() < total) break; // wait for more data

        QByteArray payload = ci->recv_buf.mid(NC_HEADER_SIZE, hdr.length);
        ci->recv_buf.remove(0, total);
        processPacket(sock, hdr, payload);
    }
}

void Server::processPacket(QTcpSocket* sock, const NC_Header& hdr, const QByteArray& payload)
{
    auto* ci = m_clients.value(sock);
    if (!ci) return;

    switch ((NC_Command)hdr.command) {

    case CMD_HELLO:
        if ((int)payload.size() >= (int)sizeof(NC_Hello)) {
            NC_Hello h;
            memcpy(&h, payload.constData(), sizeof(h));
            ci->hostname = QString::fromLatin1(h.hostname);
            ci->screen_w = ntohs(h.screen_w);
            ci->screen_h = ntohs(h.screen_h);
            ci->platform = h.platform;
            emit clientInfoUpdated(sock);
        }
        break;

    case CMD_PONG:
        // heartbeat ack — could update last-seen timestamp
        break;

    case CMD_SCREEN_FRAME:
        if ((int)payload.size() > (int)sizeof(NC_ScreenFrameHeader)) {
            // Decode JPEG
            QByteArray jpeg = payload.mid(sizeof(NC_ScreenFrameHeader));
            QImage img = QImage::fromData(jpeg, "JPEG");
            if (!img.isNull()) {
                ci->last_frame = img;
                emit screenFrameReceived(sock, img);
            }
        }
        break;

    default:
        break;
    }
}
