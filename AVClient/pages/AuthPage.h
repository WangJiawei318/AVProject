#ifndef AUTHPAGE_H
#define AUTHPAGE_H

#include <QWidget>

class AVNetworkClient;
class QLabel;
class QLineEdit;
class QPushButton;
class QTextEdit;

class AuthPage : public QWidget
{
    Q_OBJECT

public:
    explicit AuthPage(AVNetworkClient *networkClient,
                      QWidget *parent = nullptr);

private slots:
    void slotRegisterClicked();
    void slotLoginClicked();
    void slotRegisterResponse(bool success, int errorCode, quint64 userId,
                              const QString &message);
    void slotLoginResponse(bool success, int errorCode, quint64 userId,
                           const QString &username, const QString &message);
    void slotAuthenticationChanged(bool authenticated, quint64 userId,
                                   const QString &username);
    void slotConnectedChanged(bool connected);

private:
    bool validateInput(QString *username, QString *password);
    void appendMessage(const QString &message);
    void updateState();

    AVNetworkClient *m_networkClient;
    QLineEdit *m_usernameEdit;
    QLineEdit *m_passwordEdit;
    QPushButton *m_registerButton;
    QPushButton *m_loginButton;
    QLabel *m_statusLabel;
    QTextEdit *m_messageEdit;
};

#endif // AUTHPAGE_H

