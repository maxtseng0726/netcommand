// clientpanel.cpp
#include "clientpanel.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QFrame>

static QLabel* makeInfoRow(QVBoxLayout* layout, const QString& key)
{
    auto* row = new QHBoxLayout;
    auto* k   = new QLabel(key + ":");
    k->setStyleSheet("color: #6c7086; font-size: 12px; min-width: 80px;");
    auto* v   = new QLabel("—");
    v->setStyleSheet("color: #cdd6f4; font-size: 12px;");
    v->setWordWrap(true);
    row->addWidget(k);
    row->addWidget(v, 1);
    layout->addLayout(row);
    return v;
}

ClientPanel::ClientPanel(QWidget* parent) : QWidget(parent)
{
    setFixedWidth(260);
    setStyleSheet(
        "QWidget { background: #1e1e2e; }"
        "QGroupBox { color: #89b4fa; font-size: 12px; font-weight: 500;"
        "            border: 1px solid #313244; border-radius: 6px;"
        "            margin-top: 8px; padding-top: 8px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 8px; }"
        "QPushButton { background: #313244; color: #cdd6f4; border: 1px solid #45475a;"
        "              border-radius: 4px; padding: 5px 10px; font-size: 12px; }"
        "QPushButton:hover  { background: #45475a; }"
        "QPushButton:pressed{ background: #585b70; }"
        "QPushButton#remote { background: #1e3a5f; border-color: #4a8ac4; color: #89b4fa; }"
        "QPushButton#remote:hover { background: #2a4f7a; }"
        "QPushButton#disconnect { background: #3d1e2e; border-color: #a45a6a; color: #f38ba8; }"
        "QPushButton#disconnect:hover { background: #5a2a3a; }"
        "QLineEdit { background: #181825; color: #cdd6f4; border: 1px solid #45475a;"
        "            border-radius: 4px; padding: 4px 8px; font-size: 12px; }"
        "QSlider::groove:horizontal { background: #313244; height: 4px; border-radius: 2px; }"
        "QSlider::handle:horizontal { background: #89b4fa; width: 12px; height: 12px;"
        "                             margin: -4px 0; border-radius: 6px; }"
        "QSlider::sub-page:horizontal { background: #89b4fa; border-radius: 2px; }"
        "QCheckBox { color: #cdd6f4; font-size: 12px; }"
        "QCheckBox::indicator { width: 14px; height: 14px; }"
    );

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(10, 10, 10, 10);
    root->setSpacing(10);

    // ── Header ─────────────────────────────────────────
    auto* title = new QLabel("Client Details");
    title->setStyleSheet("color: #cdd6f4; font-size: 14px; font-weight: 500;");
    root->addWidget(title);

    auto* sep = new QFrame;
    sep->setFrameShape(QFrame::HLine);
    sep->setStyleSheet("color: #313244;");
    root->addWidget(sep);

    // ── Info group ─────────────────────────────────────
    auto* info_box = new QGroupBox("Info");
    auto* info_lay = new QVBoxLayout(info_box);
    info_lay->setSpacing(4);
    m_hostname   = makeInfoRow(info_lay, "Host");
    m_ip         = makeInfoRow(info_lay, "IP");
    m_platform   = makeInfoRow(info_lay, "OS");
    m_resolution = makeInfoRow(info_lay, "Screen");
    m_status     = makeInfoRow(info_lay, "Status");
    root->addWidget(info_box);

    // ── Message group ──────────────────────────────────
    auto* msg_box = new QGroupBox("Send message");
    auto* msg_lay = new QVBoxLayout(msg_box);
    m_msg_edit = new QLineEdit;
    m_msg_edit->setPlaceholderText("Message text…");
    m_send_btn = new QPushButton("Send to this client");
    msg_lay->addWidget(m_msg_edit);
    msg_lay->addWidget(m_send_btn);
    root->addWidget(msg_box);

    // ── Stream controls ────────────────────────────────
    auto* stream_box = new QGroupBox("Screen stream");
    auto* stream_lay = new QVBoxLayout(stream_box);
    stream_lay->setSpacing(6);

    m_stream_chk = new QCheckBox("Enable streaming");
    stream_lay->addWidget(m_stream_chk);

    auto* fps_row = new QHBoxLayout;
    fps_row->addWidget(new QLabel("FPS"));
    m_fps_slider = new QSlider(Qt::Horizontal);
    m_fps_slider->setRange(1, 30); m_fps_slider->setValue(13);
    m_fps_label  = new QLabel("13");
    m_fps_label->setStyleSheet("color:#cdd6f4; font-size:12px; min-width:24px;");
    fps_row->addWidget(m_fps_slider, 1);
    fps_row->addWidget(m_fps_label);
    stream_lay->addLayout(fps_row);

    auto* q_row = new QHBoxLayout;
    q_row->addWidget(new QLabel("JPEG"));
    m_quality_slider = new QSlider(Qt::Horizontal);
    m_quality_slider->setRange(10, 95); m_quality_slider->setValue(70);
    m_quality_label  = new QLabel("70");
    m_quality_label->setStyleSheet("color:#cdd6f4; font-size:12px; min-width:24px;");
    q_row->addWidget(m_quality_slider, 1);
    q_row->addWidget(m_quality_label);
    stream_lay->addLayout(q_row);

    root->addWidget(stream_box);

    // ── Action buttons ─────────────────────────────────
    m_remote_btn = new QPushButton("Open Remote Control");
    m_remote_btn->setObjectName("remote");
    root->addWidget(m_remote_btn);

    m_disconnect_btn = new QPushButton("Disconnect");
    m_disconnect_btn->setObjectName("disconnect");
    root->addWidget(m_disconnect_btn);

    root->addStretch();

    // ── Wiring ─────────────────────────────────────────
    connect(m_send_btn,       &QPushButton::clicked,  this, &ClientPanel::onSendMessage);
    connect(m_disconnect_btn, &QPushButton::clicked,  this, &ClientPanel::onDisconnect);
    connect(m_remote_btn,     &QPushButton::clicked,  this, &ClientPanel::onRemoteControl);
    connect(m_stream_chk,     &QCheckBox::toggled,    this, &ClientPanel::onToggleStream);
    connect(m_fps_slider,     &QSlider::valueChanged, this, &ClientPanel::onFpsChanged);
    connect(m_quality_slider, &QSlider::valueChanged, this, &ClientPanel::onQualityChanged);

    clear();
}

void ClientPanel::clear()
{
    m_sock   = nullptr;
    m_server = nullptr;
    m_hostname->setText("—");
    m_ip->setText("—");
    m_platform->setText("—");
    m_resolution->setText("—");
    m_status->setText("—");
    setEnabled(false);
}

void ClientPanel::setClient(QTcpSocket* sock, Server* server)
{
    m_sock   = sock;
    m_server = server;
    setEnabled(true);

    auto* ci = server->clientInfo(sock);
    if (!ci) return;

    static const char* plat[] = {"Windows", "macOS", "Linux"};
    m_hostname->setText(ci->hostname);
    m_ip->setText(ci->ip);
    m_platform->setText(ci->platform < 3 ? plat[ci->platform] : "Unknown");
    m_resolution->setText(QString("%1 × %2").arg(ci->screen_w).arg(ci->screen_h));
    m_status->setText(ci->streaming ? "Streaming" : "Connected");
    m_stream_chk->setChecked(ci->streaming);
}

void ClientPanel::onSendMessage()
{
    if (!m_server || !m_sock) return;
    QString msg = m_msg_edit->text().trimmed();
    if (msg.isEmpty()) return;
    m_server->sendBroadcast(msg, m_sock);
    m_msg_edit->clear();
}

void ClientPanel::onDisconnect()
{
    if (!m_server || !m_sock) return;
    m_server->disconnectClient(m_sock);
}

void ClientPanel::onRemoteControl()
{
    if (m_sock) emit requestRemoteControl(m_sock);
}

void ClientPanel::onToggleStream(bool checked)
{
    if (!m_server || !m_sock) return;
    int fps     = m_fps_slider->value();
    int quality = m_quality_slider->value();
    m_server->requestScreen(m_sock, checked, fps, quality);
    m_status->setText(checked ? "Streaming" : "Connected");
}

void ClientPanel::onFpsChanged(int val)
{
    m_fps_label->setText(QString::number(val));
    if (m_stream_chk->isChecked()) onToggleStream(true);
}

void ClientPanel::onQualityChanged(int val)
{
    m_quality_label->setText(QString::number(val));
    if (m_stream_chk->isChecked()) onToggleStream(true);
}
