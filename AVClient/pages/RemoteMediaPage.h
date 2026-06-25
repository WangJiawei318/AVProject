#ifndef REMOTEMEDIAPAGE_H
#define REMOTEMEDIAPAGE_H

#include <QWidget>

class AVNetworkClient;
class QLabel;
class QPushButton;
class QTableWidget;
class QTextEdit;

class RemoteMediaPage : public QWidget
{
    Q_OBJECT

public:
    explicit RemoteMediaPage(AVNetworkClient *networkClient, QWidget *parent = nullptr);

private slots:
    void slotRefreshClicked();
    void slotConnectedChanged(bool connected);
    void slotMediaListReceived(const QString &payload);
    void slotLogMessage(const QString &message);

private:
    void appendLog(const QString &message);
    void clearTable();
    void fillTable(const QString &payload);

private:
    AVNetworkClient *m_networkClient;
    QLabel *m_statusLabel;
    QPushButton *m_refreshButton;
    QTableWidget *m_table;
    QTextEdit *m_logEdit;
};

#endif // REMOTEMEDIAPAGE_H
