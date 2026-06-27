#include "RemoteMediaPage.h"

#include "AVNetworkClient.h"

#include <QAbstractItemView>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QProgressBar>
#include <QStringList>
#include <QTableWidget>
#include <QTextEdit>
#include <QTime>
#include <QTimer>
#include <QVBoxLayout>

RemoteMediaPage::RemoteMediaPage(AVNetworkClient *networkClient, QWidget *parent)
    : QWidget(parent),
      m_networkClient(networkClient),
      m_uploadFile(new QFile(this)),
      m_uploadFileSize(0),
      m_confirmedOffset(0),
      m_expectedOffset(0),
      m_uploading(false)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(10);

    auto *top = new QHBoxLayout;
    m_statusLabel = new QLabel(this);
    m_refreshButton = new QPushButton("Refresh media list", this);
    m_uploadButton = new QPushButton("Select and upload file", this);
    top->addWidget(new QLabel("Server:", this));
    top->addWidget(m_statusLabel);
    top->addStretch();
    top->addWidget(m_uploadButton);
    top->addWidget(m_refreshButton);

    auto *uploadRow = new QHBoxLayout;
    m_uploadStatusLabel = new QLabel("No upload in progress", this);
    m_uploadProgress = new QProgressBar(this);
    m_uploadProgress->setRange(0, 100);
    m_uploadProgress->setValue(0);
    uploadRow->addWidget(m_uploadStatusLabel);
    uploadRow->addWidget(m_uploadProgress, 1);

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
    root->addLayout(uploadRow);
    root->addWidget(m_table);
    root->addWidget(new QLabel("Remote media log", this));
    root->addWidget(m_logEdit);

    connect(m_refreshButton, SIGNAL(clicked()), this, SLOT(slotRefreshClicked()));
    connect(m_uploadButton, SIGNAL(clicked()), this, SLOT(slotUploadClicked()));
    connect(m_networkClient, SIGNAL(connectedChanged(bool)), this, SLOT(slotConnectedChanged(bool)));
    connect(m_networkClient, SIGNAL(mediaListReceived(QString)), this, SLOT(slotMediaListReceived(QString)));
    connect(m_networkClient, SIGNAL(logMessage(QString)), this, SLOT(slotLogMessage(QString)));
    connect(m_networkClient,
            SIGNAL(uploadInitResponse(bool,QString,QString)),
            this,
            SLOT(slotUploadInitResponse(bool,QString,QString)));
    connect(m_networkClient,
            SIGNAL(uploadBlockResponse(bool,QString,qint64,QString)),
            this,
            SLOT(slotUploadBlockResponse(bool,QString,qint64,QString)));
    connect(m_networkClient,
            SIGNAL(uploadFinishResponse(bool,QString,QString)),
            this,
            SLOT(slotUploadFinishResponse(bool,QString,QString)));

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
    if (!connected && m_uploading)
        finishUploadState(false, "upload stopped: server disconnected");
    updateActionStates();
}

void RemoteMediaPage::slotMediaListReceived(const QString &payload)
{
    fillTable(payload);
}

void RemoteMediaPage::slotLogMessage(const QString &message)
{
    if (message.contains("MEDIA_LIST") || message.contains("UPLOAD_"))
        appendLog(message);
}

void RemoteMediaPage::slotUploadClicked()
{
    if (!m_networkClient->isConnected()) {
        appendLog("please connect to server first");
        QMessageBox::information(this, "Upload media", "Please connect to server first.");
        return;
    }
    if (m_uploading)
        return;

    const QString filter =
            "Media files (*.mp4 *.flv *.avi *.mkv *.mov *.wmv *.mp3 *.aac *.wav)";
    const QString filePath = QFileDialog::getOpenFileName(this,
                                                         "Select media file",
                                                         QString(),
                                                         filter);
    if (filePath.isEmpty())
        return;
    if (!isSupportedMediaFile(filePath)) {
        appendLog("unsupported media extension");
        return;
    }

    const QFileInfo info(filePath);
    if (info.size() <= 0) {
        appendLog("cannot upload an empty file");
        return;
    }

    m_uploadFile->setFileName(filePath);
    if (!m_uploadFile->open(QIODevice::ReadOnly)) {
        appendLog(QString("failed to open file: %1").arg(m_uploadFile->errorString()));
        return;
    }

    m_uploading = true;
    m_uploadFileName = info.fileName();
    m_uploadFileSize = info.size();
    m_confirmedOffset = 0;
    m_expectedOffset = 0;
    m_uploadId.clear();
    m_uploadProgress->setValue(0);
    m_uploadStatusLabel->setText(QString("Initializing %1").arg(m_uploadFileName));
    updateActionStates();
    appendLog(QString("starting upload: %1 (%2 bytes)")
              .arg(m_uploadFileName)
              .arg(m_uploadFileSize));

    if (!m_networkClient->sendUploadInit(m_uploadFileName,
                                         info.suffix().toLower(),
                                         m_uploadFileSize)) {
        finishUploadState(false, "failed to send upload initialization");
    }
}

void RemoteMediaPage::slotUploadInitResponse(bool success,
                                             const QString &uploadId,
                                             const QString &message)
{
    if (!m_uploading)
        return;
    if (!success || uploadId.isEmpty()) {
        finishUploadState(false, QString("upload initialization failed: %1").arg(message));
        return;
    }

    m_uploadId = uploadId;
    m_uploadStatusLabel->setText(QString("Uploading %1").arg(m_uploadFileName));
    appendLog(QString("upload initialized: id=%1").arg(uploadId));
    QTimer::singleShot(0, this, [this]() {
        sendNextUploadBlock();
    });
}

void RemoteMediaPage::slotUploadBlockResponse(bool success,
                                              const QString &uploadId,
                                              qint64 receivedOffset,
                                              const QString &message)
{
    if (!m_uploading)
        return;
    if (!success || uploadId != m_uploadId) {
        finishUploadState(false, QString("upload block failed: %1").arg(message));
        return;
    }
    if (receivedOffset != m_expectedOffset) {
        finishUploadState(false, "server returned an invalid upload offset");
        return;
    }

    m_confirmedOffset = receivedOffset;
    const int progress = static_cast<int>((m_confirmedOffset * 100) / m_uploadFileSize);
    m_uploadProgress->setValue(progress);

    if (m_confirmedOffset == m_uploadFileSize) {
        m_uploadStatusLabel->setText(QString("Finalizing %1").arg(m_uploadFileName));
        if (!m_networkClient->sendUploadFinish(m_uploadId,
                                               m_uploadFileName,
                                               m_uploadFileSize)) {
            finishUploadState(false, "failed to send upload completion request");
        }
        return;
    }

    QTimer::singleShot(0, this, [this]() {
        sendNextUploadBlock();
    });
}

void RemoteMediaPage::slotUploadFinishResponse(bool success,
                                               const QString &fileName,
                                               const QString &message)
{
    if (!m_uploading)
        return;

    if (!success) {
        finishUploadState(false, QString("upload completion failed: %1").arg(message));
        return;
    }

    const QString savedName = fileName.isEmpty() ? m_uploadFileName : fileName;
    finishUploadState(true, QString("upload completed: %1").arg(savedName));
    slotRefreshClicked();
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

void RemoteMediaPage::sendNextUploadBlock()
{
    if (!m_uploading || m_uploadId.isEmpty())
        return;
    if (!m_networkClient->isConnected()) {
        finishUploadState(false, "upload stopped: server disconnected");
        return;
    }
    if (!m_uploadFile->seek(m_confirmedOffset)) {
        finishUploadState(false, "failed to seek local upload file");
        return;
    }

    const QByteArray block = m_uploadFile->read(64 * 1024);
    if (block.isEmpty()) {
        finishUploadState(false,
                          m_uploadFile->atEnd()
                          ? "local file ended before expected size"
                          : QString("failed to read local file: %1").arg(m_uploadFile->errorString()));
        return;
    }

    m_expectedOffset = m_confirmedOffset + block.size();
    if (!m_networkClient->sendUploadBlock(m_uploadId, m_confirmedOffset, block))
        finishUploadState(false, "failed to send upload block");
}

void RemoteMediaPage::finishUploadState(bool success, const QString &message)
{
    if (m_uploadFile->isOpen())
        m_uploadFile->close();

    m_uploading = false;
    m_uploadId.clear();
    m_uploadFileName.clear();
    m_uploadFileSize = 0;
    m_confirmedOffset = 0;
    m_expectedOffset = 0;
    m_uploadStatusLabel->setText(success ? "Upload completed" : "Upload stopped");
    if (success)
        m_uploadProgress->setValue(100);
    appendLog(message);
    updateActionStates();
}

bool RemoteMediaPage::isSupportedMediaFile(const QString &filePath) const
{
    static const QStringList extensions =
            QStringList() << "mp4" << "flv" << "avi" << "mkv" << "mov"
                          << "wmv" << "mp3" << "aac" << "wav";
    return extensions.contains(QFileInfo(filePath).suffix().toLower());
}

void RemoteMediaPage::updateActionStates()
{
    const bool connected = m_networkClient->isConnected();
    m_refreshButton->setEnabled(connected);
    m_uploadButton->setEnabled(!m_uploading);
}
