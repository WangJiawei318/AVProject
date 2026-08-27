#include "AuthPage.h"

#include "AVNetworkClient.h"

#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTextEdit>
#include <QTime>
#include <QVBoxLayout>

AuthPage::AuthPage(AVNetworkClient *networkClient, QWidget *parent)
    : QWidget(parent),
      m_networkClient(networkClient),
      m_usernameEdit(new QLineEdit(this)),
      m_passwordEdit(new QLineEdit(this)),
      m_registerButton(new QPushButton("Register", this)),
      m_loginButton(new QPushButton("Login", this)),
      m_statusLabel(new QLabel(this)),
      m_messageEdit(new QTextEdit(this))
{
    m_usernameEdit->setMaxLength(32);
    m_passwordEdit->setMaxLength(64);
    m_passwordEdit->setEchoMode(QLineEdit::Password);
    m_messageEdit->setReadOnly(true);

    auto *form = new QFormLayout;
    form->addRow("Username", m_usernameEdit);
    form->addRow("Password", m_passwordEdit);
    auto *actions = new QHBoxLayout;
    actions->addWidget(m_registerButton);
    actions->addWidget(m_loginButton);
    actions->addStretch();
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(18, 18, 18, 18);
    root->addWidget(m_statusLabel);
    root->addLayout(form);
    root->addLayout(actions);
    root->addWidget(m_messageEdit, 1);

    connect(m_registerButton, SIGNAL(clicked()), this, SLOT(slotRegisterClicked()));
    connect(m_loginButton, SIGNAL(clicked()), this, SLOT(slotLoginClicked()));
    connect(m_networkClient,
            SIGNAL(registerResponse(bool,int,quint64,QString)),
            this,
            SLOT(slotRegisterResponse(bool,int,quint64,QString)));
    connect(m_networkClient,
            SIGNAL(loginResponse(bool,int,quint64,QString,QString)),
            this,
            SLOT(slotLoginResponse(bool,int,quint64,QString,QString)));
    connect(m_networkClient,
            SIGNAL(authenticationChanged(bool,quint64,QString)),
            this,
            SLOT(slotAuthenticationChanged(bool,quint64,QString)));
    connect(m_networkClient, SIGNAL(connectedChanged(bool)),
            this, SLOT(slotConnectedChanged(bool)));
    updateState();
}

void AuthPage::slotRegisterClicked()
{
    QString username, password;
    if (!validateInput(&username, &password))
        return;
    if (!m_networkClient->sendRegister(username, password))
        appendMessage("Failed to send registration request.");
    m_passwordEdit->clear();
}

void AuthPage::slotLoginClicked()
{
    QString username, password;
    if (!validateInput(&username, &password))
        return;
    if (!m_networkClient->sendLogin(username, password))
        appendMessage("Failed to send login request.");
    m_passwordEdit->clear();
}

void AuthPage::slotRegisterResponse(bool success, int, quint64,
                                    const QString &message)
{
    appendMessage(success ? "Registration successful. Please log in." : message);
}

void AuthPage::slotLoginResponse(bool success, int, quint64,
                                 const QString &, const QString &message)
{
    appendMessage(success ? "Login successful." : message);
}

void AuthPage::slotAuthenticationChanged(bool, quint64, const QString &)
{
    updateState();
}

void AuthPage::slotConnectedChanged(bool)
{
    updateState();
}

bool AuthPage::validateInput(QString *username, QString *password)
{
    *username = m_usernameEdit->text().trimmed();
    *password = m_passwordEdit->text();
    const int usernameBytes = username->toUtf8().size();
    const int passwordBytes = password->toUtf8().size();
    if (usernameBytes < 3 || usernameBytes > 32 ||
            passwordBytes < 6 || passwordBytes > 64) {
        appendMessage("Username must be 3-32 bytes and password 6-64 bytes.");
        return false;
    }
    if (!m_networkClient->isConnected()) {
        appendMessage("Connect to the server in Settings first.");
        return false;
    }
    return true;
}

void AuthPage::appendMessage(const QString &message)
{
    m_messageEdit->append(QString("[%1] %2")
                          .arg(QTime::currentTime().toString("HH:mm:ss"))
                          .arg(message));
}

void AuthPage::updateState()
{
    const bool connected = m_networkClient->isConnected();
    const bool authenticated = m_networkClient->isAuthenticated();
    if (!connected)
        m_statusLabel->setText("Disconnected. Connect in Settings first.");
    else if (!authenticated)
        m_statusLabel->setText("Connected. Registration and login are available.");
    else
        m_statusLabel->setText(QString("Authenticated as %1 (user ID %2)")
                               .arg(m_networkClient->currentUsername())
                               .arg(m_networkClient->currentUserId()));
    m_registerButton->setEnabled(connected && !authenticated);
    m_loginButton->setEnabled(connected && !authenticated);
}

