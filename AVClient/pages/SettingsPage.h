#ifndef SETTINGSPAGE_H
#define SETTINGSPAGE_H

#include <QWidget>

class AVNetworkClient;
class QLabel;
class QLineEdit;
class QPushButton;
class QTextEdit;

class SettingsPage : public QWidget
{
    Q_OBJECT

public:
    explicit SettingsPage(AVNetworkClient *networkClient, QWidget *parent = nullptr);

private slots:
    void slotConnectClicked();
    void slotDisconnectClicked();
    void slotPingClicked();
    void slotConnectedChanged(bool connected);
    void slotLogMessage(const QString &message);
    void slotPingResponse(const QString &message);

private:
    void appendLog(const QString &message);
    quint16 portValue() const;

private:
    AVNetworkClient *m_networkClient;
    QLineEdit *m_ipEdit;
    QLineEdit *m_portEdit;
    QPushButton *m_connectButton;
    QPushButton *m_disconnectButton;
    QPushButton *m_pingButton;
    QLabel *m_statusLabel;
    QLabel *m_lastResponseLabel;
    QTextEdit *m_logEdit;
};

#endif // SETTINGSPAGE_H
