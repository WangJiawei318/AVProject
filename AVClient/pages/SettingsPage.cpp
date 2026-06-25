#include "SettingsPage.h"

#include "AVNetworkClient.h"

#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTextEdit>
#include <QTime>
#include <QVBoxLayout>

SettingsPage::SettingsPage(QWidget *parent)
    : QWidget(parent),
      m_networkClient(new AVNetworkClient(this))
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(24, 24, 24, 24);
    root->setSpacing(12);

    auto *form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignRight);

    m_ipEdit = new QLineEdit(this);
    m_ipEdit->setText("192.168.44.130");

    m_portEdit = new QLineEdit(this);
    m_portEdit->setText("8000");

    m_statusLabel = new QLabel("Disconnected", this);
    m_lastResponseLabel = new QLabel("-", this);

    form->addRow("Server IP", m_ipEdit);
    form->addRow("Port", m_portEdit);
    form->addRow("Status", m_statusLabel);
    form->addRow("Last response", m_lastResponseLabel);

    auto *buttonLayout = new QHBoxLayout;
    m_connectButton = new QPushButton("Connect", this);
    m_disconnectButton = new QPushButton("Disconnect", this);
    m_pingButton = new QPushButton("Send Ping", this);
    m_disconnectButton->setEnabled(false);
    m_pingButton->setEnabled(false);

    buttonLayout->addWidget(m_connectButton);
    buttonLayout->addWidget(m_disconnectButton);
    buttonLayout->addWidget(m_pingButton);
    buttonLayout->addStretch();

    m_logEdit = new QTextEdit(this);
    m_logEdit->setReadOnly(true);
    m_logEdit->setMinimumHeight(260);

    auto *hint = new QLabel("Stage 2 only verifies basic TCP connect and PING_RQ/PING_RS. Upload, download and remote playback are not implemented here.", this);
    hint->setWordWrap(true);

    root->addLayout(form);
    root->addLayout(buttonLayout);
    root->addWidget(new QLabel("Network log", this));
    root->addWidget(m_logEdit);
    root->addWidget(hint);

    connect(m_connectButton, SIGNAL(clicked()), this, SLOT(slotConnectClicked()));
    connect(m_disconnectButton, SIGNAL(clicked()), this, SLOT(slotDisconnectClicked()));
    connect(m_pingButton, SIGNAL(clicked()), this, SLOT(slotPingClicked()));
    connect(m_networkClient, SIGNAL(connectedChanged(bool)), this, SLOT(slotConnectedChanged(bool)));
    connect(m_networkClient, SIGNAL(logMessage(QString)), this, SLOT(slotLogMessage(QString)));
    connect(m_networkClient, SIGNAL(pingResponse(QString)), this, SLOT(slotPingResponse(QString)));

    appendLog("network test page ready");
}

void SettingsPage::slotConnectClicked()
{
    const QString ip = m_ipEdit->text().trimmed();
    const quint16 port = portValue();
    if (ip.isEmpty() || port == 0) {
        appendLog("invalid IP or port");
        return;
    }

    m_networkClient->connectToServer(ip, port);
}

void SettingsPage::slotDisconnectClicked()
{
    m_networkClient->disconnectFromServer();
}

void SettingsPage::slotPingClicked()
{
    if (!m_networkClient->isConnected()) {
        appendLog("not connected");
        return;
    }

    m_networkClient->sendPing();
}

void SettingsPage::slotConnectedChanged(bool connected)
{
    m_statusLabel->setText(connected ? "Connected" : "Disconnected");
    m_connectButton->setEnabled(!connected);
    m_disconnectButton->setEnabled(connected);
    m_pingButton->setEnabled(connected);
}

void SettingsPage::slotLogMessage(const QString &message)
{
    appendLog(message);
}

void SettingsPage::slotPingResponse(const QString &message)
{
    m_lastResponseLabel->setText(message);
}

void SettingsPage::appendLog(const QString &message)
{
    m_logEdit->append(QString("[%1] %2")
                      .arg(QTime::currentTime().toString("HH:mm:ss"))
                      .arg(message));
}

quint16 SettingsPage::portValue() const
{
    bool ok = false;
    uint port = m_portEdit->text().trimmed().toUInt(&ok);
    if (!ok || port == 0 || port > 65535)
        return 0;
    return static_cast<quint16>(port);
}
