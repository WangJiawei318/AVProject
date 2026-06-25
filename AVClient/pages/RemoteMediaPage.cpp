#include "RemoteMediaPage.h"

#include "AVNetworkClient.h"

#include <QAbstractItemView>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStringList>
#include <QTableWidget>
#include <QTextEdit>
#include <QTime>
#include <QVBoxLayout>

RemoteMediaPage::RemoteMediaPage(AVNetworkClient *networkClient, QWidget *parent)
    : QWidget(parent),
      m_networkClient(networkClient)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(10);

    auto *top = new QHBoxLayout;
    m_statusLabel = new QLabel(this);
    m_refreshButton = new QPushButton("Refresh media list", this);
    top->addWidget(new QLabel("Server:", this));
    top->addWidget(m_statusLabel);
    top->addStretch();
    top->addWidget(m_refreshButton);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(4);
    m_table->setHorizontalHeaderLabels(QStringList() << "File name" << "Size" << "Modified time" << "Type");
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);

    m_logEdit = new QTextEdit(this);
    m_logEdit->setReadOnly(true);
    m_logEdit->setMaximumHeight(120);

    root->addLayout(top);
    root->addWidget(m_table);
    root->addWidget(new QLabel("Remote media log", this));
    root->addWidget(m_logEdit);

    connect(m_refreshButton, SIGNAL(clicked()), this, SLOT(slotRefreshClicked()));
    connect(m_networkClient, SIGNAL(connectedChanged(bool)), this, SLOT(slotConnectedChanged(bool)));
    connect(m_networkClient, SIGNAL(mediaListReceived(QString)), this, SLOT(slotMediaListReceived(QString)));
    connect(m_networkClient, SIGNAL(logMessage(QString)), this, SLOT(slotLogMessage(QString)));

    slotConnectedChanged(m_networkClient->isConnected());
    appendLog("remote media page ready");
}

void RemoteMediaPage::slotRefreshClicked()
{
    if (!m_networkClient->isConnected()) {
        appendLog("please connect to server first");
        return;
    }

    appendLog("requesting remote media list");
    m_networkClient->sendMediaListRequest();
}

void RemoteMediaPage::slotConnectedChanged(bool connected)
{
    m_statusLabel->setText(connected ? "Connected" : "Disconnected");
    m_refreshButton->setEnabled(connected);
}

void RemoteMediaPage::slotMediaListReceived(const QString &payload)
{
    fillTable(payload);
}

void RemoteMediaPage::slotLogMessage(const QString &message)
{
    if (message.contains("MEDIA_LIST"))
        appendLog(message);
}

void RemoteMediaPage::appendLog(const QString &message)
{
    m_logEdit->append(QString("[%1] %2")
                      .arg(QTime::currentTime().toString("HH:mm:ss"))
                      .arg(message));
}

void RemoteMediaPage::clearTable()
{
    m_table->setRowCount(0);
}

void RemoteMediaPage::fillTable(const QString &payload)
{
    clearTable();

    if (payload.trimmed().isEmpty()) {
        appendLog("server media directory is empty");
        return;
    }

    const QStringList lines = payload.split('\n', QString::SkipEmptyParts);
    for (const QString &line : lines) {
        const QStringList fields = line.split('|');
        if (fields.size() < 4) {
            appendLog(QString("skip invalid media row: %1").arg(line));
            continue;
        }

        const int row = m_table->rowCount();
        m_table->insertRow(row);
        for (int col = 0; col < 4; ++col)
            m_table->setItem(row, col, new QTableWidgetItem(fields.at(col)));
    }

    appendLog(QString("loaded %1 media file(s)").arg(m_table->rowCount()));
}
