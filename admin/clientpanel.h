#pragma once
// clientpanel.h — Sidebar panel shown when a client tile is selected
// Displays: hostname, IP, platform, screen size, connection status
// Actions:  broadcast to this client, disconnect, toggle stream, remote control
#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QLineEdit>
#include <QSlider>
#include <QCheckBox>
#include <QTcpSocket>
#include "server.h"

class ClientPanel : public QWidget
{
    Q_OBJECT
public:
    explicit ClientPanel(QWidget* parent = nullptr);
    void setClient(QTcpSocket* sock, Server* server);
    void clear();

signals:
    void requestRemoteControl(QTcpSocket* sock);

private slots:
    void onSendMessage();
    void onDisconnect();
    void onToggleStream(bool checked);
    void onRemoteControl();
    void onFpsChanged(int val);
    void onQualityChanged(int val);

private:
    QTcpSocket* m_sock   = nullptr;
    Server*     m_server = nullptr;

    // Info labels
    QLabel* m_hostname;
    QLabel* m_ip;
    QLabel* m_platform;
    QLabel* m_resolution;
    QLabel* m_status;

    // Controls
    QLineEdit*   m_msg_edit;
    QPushButton* m_send_btn;
    QCheckBox*   m_stream_chk;
    QSlider*     m_fps_slider;
    QLabel*      m_fps_label;
    QSlider*     m_quality_slider;
    QLabel*      m_quality_label;
    QPushButton* m_remote_btn;
    QPushButton* m_disconnect_btn;
};
