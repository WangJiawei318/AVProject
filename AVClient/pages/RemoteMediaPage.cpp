#include "RemoteMediaPage.h"

#include "AVNetworkClient.h"

#include <QAbstractItemView>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QProgressBar>
#include <QSignalBlocker>
#include <QStringList>
#include <QTableWidget>
#include <QTextEdit>
#include <QTime>
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>

RemoteMediaPage::RemoteMediaPage(AVNetworkClient *networkClient, QWidget *parent)
    : QWidget(parent),
      m_networkClient(networkClient),
      m_uploadFile(new QFile(this)),
      m_uploadFileSize(0),
      m_confirmedOffset(0),
      m_expectedOffset(0),
      m_uploading(false),
      m_resumingUpload(false),
      m_hasCurrentUploadTask(false),
      m_downloadFile(new QFile(this)),
      m_downloadFileSize(0),
      m_downloadModifiedTime(0),
      m_downloadOffset(0),
      m_downloading(false),
      m_resumingDownload(false),
      m_hasCurrentDownloadTask(false),
      m_playAfterDownload(false)
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

    m_uploadTaskTable = new QTableWidget(this);
    m_uploadTaskTable->setColumnCount(5);
    m_uploadTaskTable->setHorizontalHeaderLabels(
                QStringList() << "File name" << "Size" << "Progress"
                              << "Status" << "Server");
    m_uploadTaskTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_uploadTaskTable->horizontalHeader()->setStretchLastSection(true);
    m_uploadTaskTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_uploadTaskTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_uploadTaskTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_uploadTaskTable->setMaximumHeight(155);

    auto *uploadTaskActions = new QHBoxLayout;
    m_resumeUploadButton = new QPushButton("Resume upload", this);
    m_abandonUploadButton = new QPushButton("Abandon task", this);
    uploadTaskActions->addStretch();
    uploadTaskActions->addWidget(m_resumeUploadButton);
    uploadTaskActions->addWidget(m_abandonUploadButton);

    auto *downloadActions = new QHBoxLayout;
    m_selectedFileLabel = new QLabel("Selected: none", this);
    m_downloadButton = new QPushButton("Download file", this);
    m_downloadAndPlayButton = new QPushButton("Download and play", this);
    downloadActions->addWidget(m_selectedFileLabel);
    downloadActions->addStretch();
    downloadActions->addWidget(m_downloadButton);
    downloadActions->addWidget(m_downloadAndPlayButton);

    auto *downloadProgressRow = new QHBoxLayout;
    m_downloadStatusLabel = new QLabel("No download in progress", this);
    m_downloadProgress = new QProgressBar(this);
    m_downloadProgress->setRange(0, 100);
    m_downloadProgress->setValue(0);
    downloadProgressRow->addWidget(m_downloadStatusLabel);
    downloadProgressRow->addWidget(m_downloadProgress, 1);

    m_downloadTaskTable = new QTableWidget(this);
    m_downloadTaskTable->setColumnCount(6);
    m_downloadTaskTable->setHorizontalHeaderLabels(
                QStringList() << "Remote file" << "Downloaded" << "Total"
                              << "Progress" << "Status" << "Server");
    m_downloadTaskTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_downloadTaskTable->horizontalHeader()->setStretchLastSection(true);
    m_downloadTaskTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_downloadTaskTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_downloadTaskTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_downloadTaskTable->setMaximumHeight(135);

    auto *downloadTaskActions = new QHBoxLayout;
    m_resumeDownloadButton = new QPushButton("Resume download", this);
    m_abandonDownloadButton = new QPushButton("Abandon task", this);
    downloadTaskActions->addStretch();
    downloadTaskActions->addWidget(m_resumeDownloadButton);
    downloadTaskActions->addWidget(m_abandonDownloadButton);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(4);
    m_table->setHorizontalHeaderLabels(QStringList() << "File name" << "Size" << "Modified time" << "Type");
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);

    m_logEdit = new QTextEdit(this);
    m_logEdit->setReadOnly(true);
    m_logEdit->setMaximumHeight(120);

    root->addLayout(top);
    root->addLayout(uploadRow);
    root->addWidget(new QLabel("Unfinished uploads", this));
    root->addWidget(m_uploadTaskTable);
    root->addLayout(uploadTaskActions);
    root->addLayout(downloadActions);
    root->addLayout(downloadProgressRow);
    root->addWidget(new QLabel("Unfinished downloads", this));
    root->addWidget(m_downloadTaskTable);
    root->addLayout(downloadTaskActions);
    root->addWidget(m_table);
    root->addWidget(new QLabel("Remote media log", this));
    root->addWidget(m_logEdit);

    connect(m_refreshButton, SIGNAL(clicked()), this, SLOT(slotRefreshClicked()));
    connect(m_uploadButton, SIGNAL(clicked()), this, SLOT(slotUploadClicked()));
    connect(m_resumeUploadButton,
            SIGNAL(clicked()),
            this,
            SLOT(slotResumeUploadClicked()));
    connect(m_abandonUploadButton,
            SIGNAL(clicked()),
            this,
            SLOT(slotAbandonUploadClicked()));
    connect(m_uploadTaskTable,
            SIGNAL(itemSelectionChanged()),
            this,
            SLOT(slotUploadTaskSelectionChanged()));
    connect(m_downloadButton, SIGNAL(clicked()), this, SLOT(slotDownloadClicked()));
    connect(m_downloadAndPlayButton,
            SIGNAL(clicked()),
            this,
            SLOT(slotDownloadAndPlayClicked()));
    connect(m_resumeDownloadButton,
            SIGNAL(clicked()),
            this,
            SLOT(slotResumeDownloadClicked()));
    connect(m_abandonDownloadButton,
            SIGNAL(clicked()),
            this,
            SLOT(slotAbandonDownloadClicked()));
    connect(m_downloadTaskTable,
            SIGNAL(itemSelectionChanged()),
            this,
            SLOT(slotDownloadTaskSelectionChanged()));
    connect(m_table,
            SIGNAL(itemSelectionChanged()),
            this,
            SLOT(slotSelectionChanged()));
    connect(m_networkClient, SIGNAL(connectedChanged(bool)), this, SLOT(slotConnectedChanged(bool)));
    connect(m_networkClient, SIGNAL(mediaListReceived(QString)), this, SLOT(slotMediaListReceived(QString)));
    connect(m_networkClient, SIGNAL(logMessage(QString)), this, SLOT(slotLogMessage(QString)));
    connect(m_networkClient,
            SIGNAL(uploadInitResponse(bool,QString,QString,qint64,QString,QString)),
            this,
            SLOT(slotUploadInitResponse(bool,QString,QString,qint64,QString,QString)));
    connect(m_networkClient,
            SIGNAL(uploadResumeResponse(bool,QString,qint64,QString,QString)),
            this,
            SLOT(slotUploadResumeResponse(bool,QString,qint64,QString,QString)));
    connect(m_networkClient,
            SIGNAL(uploadBlockResponse(bool,QString,qint64,QString)),
            this,
            SLOT(slotUploadBlockResponse(bool,QString,qint64,QString)));
    connect(m_networkClient,
            SIGNAL(uploadFinishResponse(bool,QString,QString)),
            this,
            SLOT(slotUploadFinishResponse(bool,QString,QString)));
    connect(m_networkClient,
            SIGNAL(downloadInitResponse(bool,QString,qint64,qint64,qint64,QString)),
            this,
            SLOT(slotDownloadInitResponse(bool,QString,qint64,qint64,qint64,QString)));
    connect(m_networkClient,
            SIGNAL(downloadBlockResponse(bool,QString,qint64,QByteArray,QString)),
            this,
            SLOT(slotDownloadBlockResponse(bool,QString,qint64,QByteArray,QString)));
    connect(m_networkClient,
            SIGNAL(downloadFinishResponse(bool,QString,QString)),
            this,
            SLOT(slotDownloadFinishResponse(bool,QString,QString)));

    slotConnectedChanged(m_networkClient->isConnected());
    refreshUploadTaskTable();
    refreshDownloadTaskTable();
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
    if (!connected && m_downloading)
        finishDownloadState(false, "download stopped: server disconnected");
    updateActionStates();
}

void RemoteMediaPage::slotMediaListReceived(const QString &payload)
{
    fillTable(payload);
}

void RemoteMediaPage::slotLogMessage(const QString &message)
{
    if (message.contains("MEDIA_LIST") ||
            message.contains("UPLOAD_") ||
            message.contains("DOWNLOAD_"))
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
    m_resumingUpload = false;
    m_hasCurrentUploadTask = false;
    m_uploadFileName = info.fileName();
    m_uploadFileSize = info.size();
    m_confirmedOffset = 0;
    m_expectedOffset = 0;
    m_transferId.clear();
    m_currentUploadTask = UploadTaskState();
    m_currentUploadTask.localFilePath = info.absoluteFilePath();
    m_currentUploadTask.fileName = info.fileName();
    m_currentUploadTask.fileSize = info.size();
    m_currentUploadTask.lastModifiedMs = info.lastModified().toMSecsSinceEpoch();
    m_currentUploadTask.serverIp = m_networkClient->serverIp();
    m_currentUploadTask.serverPort = m_networkClient->serverPort();
    m_currentUploadTask.status = "initializing";
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
                                             const QString &transferId,
                                             const QString &resumeToken,
                                             qint64 resumeOffset,
                                             const QString &finalFileName,
                                             const QString &message)
{
    if (!m_uploading || m_resumingUpload)
        return;
    if (!success || transferId.isEmpty() || resumeToken.isEmpty() ||
            resumeOffset != 0) {
        finishUploadState(false, QString("upload initialization failed: %1").arg(message));
        return;
    }

    m_transferId = transferId;
    m_currentUploadTask.transferId = transferId;
    m_currentUploadTask.resumeToken = resumeToken;
    m_currentUploadTask.confirmedOffset = resumeOffset;
    m_currentUploadTask.finalFileName = finalFileName;
    m_hasCurrentUploadTask = true;
    if (!saveCurrentUploadTask("uploading")) {
        finishUploadState(false, "upload stopped: failed to save local recovery state");
        return;
    }
    m_uploadStatusLabel->setText(QString("Uploading %1").arg(m_uploadFileName));
    appendLog(QString("upload initialized: transfer_id=%1").arg(transferId));
    continueUploadAfterConfirmation();
}

void RemoteMediaPage::slotUploadResumeResponse(bool success,
                                               const QString &transferId,
                                               qint64 resumeOffset,
                                               const QString &finalFileName,
                                               const QString &message)
{
    if (!m_uploading || !m_resumingUpload)
        return;
    if (!success || transferId != m_transferId ||
            resumeOffset < 0 || resumeOffset > m_uploadFileSize) {
        finishUploadState(false, QString("upload resume failed: %1").arg(message));
        return;
    }

    m_confirmedOffset = resumeOffset;
    m_expectedOffset = resumeOffset;
    m_currentUploadTask.confirmedOffset = resumeOffset;
    m_currentUploadTask.finalFileName = finalFileName;
    if (!saveCurrentUploadTask("uploading")) {
        finishUploadState(false, "upload stopped: failed to update local recovery state");
        return;
    }
    m_uploadProgress->setValue(
                static_cast<int>((m_confirmedOffset * 100) / m_uploadFileSize));
    m_uploadStatusLabel->setText(QString("Uploading %1").arg(m_uploadFileName));
    appendLog(QString("upload resumed at %1 bytes").arg(resumeOffset));
    continueUploadAfterConfirmation();
}

void RemoteMediaPage::slotUploadBlockResponse(bool success,
                                              const QString &transferId,
                                              qint64 receivedOffset,
                                              const QString &message)
{
    if (!m_uploading)
        return;
    if (transferId != m_transferId) {
        finishUploadState(false, "upload block failed: transfer id mismatch");
        return;
    }
    if (!success && message == "offset mismatch" &&
            receivedOffset >= 0 && receivedOffset <= m_uploadFileSize) {
        m_confirmedOffset = receivedOffset;
        m_expectedOffset = receivedOffset;
        m_currentUploadTask.confirmedOffset = receivedOffset;
        if (!saveCurrentUploadTask("uploading")) {
            finishUploadState(false, "upload stopped: failed to save corrected offset");
            return;
        }
        appendLog(QString("upload offset corrected to %1").arg(receivedOffset));
        continueUploadAfterConfirmation();
        return;
    }
    if (!success) {
        finishUploadState(false, QString("upload block failed: %1").arg(message));
        return;
    }
    if (receivedOffset < m_expectedOffset || receivedOffset > m_uploadFileSize) {
        finishUploadState(false, "server returned an invalid upload offset");
        return;
    }

    m_confirmedOffset = receivedOffset;
    m_currentUploadTask.confirmedOffset = receivedOffset;
    if (!saveCurrentUploadTask("uploading")) {
        finishUploadState(false, "upload stopped: failed to persist confirmed offset");
        return;
    }
    const int progress = static_cast<int>((m_confirmedOffset * 100) / m_uploadFileSize);
    m_uploadProgress->setValue(progress);
    continueUploadAfterConfirmation();
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

void RemoteMediaPage::slotUploadTaskSelectionChanged()
{
    updateActionStates();
}

void RemoteMediaPage::slotResumeUploadClicked()
{
    if (m_uploading || m_downloading)
        return;

    UploadTaskState task;
    if (!selectedUploadTask(&task)) {
        QMessageBox::information(this, "Resume upload",
                                 "Please select an unfinished upload task.");
        return;
    }
    if (!m_networkClient->isConnected()) {
        QMessageBox::information(this, "Resume upload",
                                 QString("Connect to %1:%2 first.")
                                 .arg(task.serverIp)
                                 .arg(task.serverPort));
        return;
    }
    if (m_networkClient->serverIp() != task.serverIp ||
            m_networkClient->serverPort() != task.serverPort) {
        QMessageBox::warning(this, "Resume upload",
                             QString("This task belongs to %1:%2.")
                             .arg(task.serverIp)
                             .arg(task.serverPort));
        return;
    }

    const QFileInfo info(task.localFilePath);
    if (!info.exists() || !info.isFile()) {
        QMessageBox::warning(this, "Resume upload", "The local source file is missing.");
        refreshUploadTaskTable();
        return;
    }
    if (info.size() != task.fileSize ||
            info.lastModified().toMSecsSinceEpoch() != task.lastModifiedMs) {
        QMessageBox::warning(this, "Resume upload",
                             "The local source file has changed. Resume is disabled.");
        refreshUploadTaskTable();
        return;
    }

    m_uploadFile->setFileName(task.localFilePath);
    if (!m_uploadFile->open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, "Resume upload",
                             QString("Cannot open the local file: %1")
                             .arg(m_uploadFile->errorString()));
        return;
    }

    m_uploading = true;
    m_resumingUpload = true;
    m_hasCurrentUploadTask = true;
    m_currentUploadTask = task;
    m_transferId = task.transferId;
    m_uploadFileName = task.fileName;
    m_uploadFileSize = task.fileSize;
    m_confirmedOffset = task.confirmedOffset;
    m_expectedOffset = task.confirmedOffset;
    m_uploadProgress->setValue(
                static_cast<int>((m_confirmedOffset * 100) / m_uploadFileSize));
    m_uploadStatusLabel->setText(QString("Resuming %1").arg(task.fileName));
    updateActionStates();
    appendLog(QString("requesting upload resume: transfer_id=%1")
              .arg(task.transferId));

    if (!m_networkClient->sendUploadResume(task.transferId,
                                           task.resumeToken,
                                           task.fileName,
                                           task.fileSize)) {
        finishUploadState(false, "failed to send upload resume request");
    }
}

void RemoteMediaPage::slotAbandonUploadClicked()
{
    UploadTaskState task;
    if (!selectedUploadTask(&task)) {
        QMessageBox::information(this, "Abandon upload",
                                 "Please select an unfinished upload task.");
        return;
    }
    if (QMessageBox::question(this,
                              "Abandon upload",
                              QString("Remove the local recovery record for %1?\n"
                                      "The server will remove its partial file after expiry.")
                              .arg(task.fileName)) != QMessageBox::Yes) {
        return;
    }

    QString error;
    if (!m_uploadTaskStore.remove(task.transferId, &error)) {
        QMessageBox::warning(this, "Abandon upload", error);
        return;
    }
    appendLog(QString("local upload task abandoned: %1").arg(task.fileName));
    refreshUploadTaskTable();
}

void RemoteMediaPage::slotSelectionChanged()
{
    const QString fileName = selectedMediaFile();
    m_selectedFileLabel->setText(fileName.isEmpty()
                                 ? "Selected: none"
                                 : QString("Selected: %1").arg(fileName));
}

void RemoteMediaPage::slotDownloadClicked()
{
    startDownload(false);
}

void RemoteMediaPage::slotDownloadAndPlayClicked()
{
    startDownload(true);
}

void RemoteMediaPage::slotDownloadTaskSelectionChanged()
{
    updateActionStates();
}

void RemoteMediaPage::slotResumeDownloadClicked()
{
    if (m_uploading || m_downloading)
        return;

    DownloadTaskState task;
    if (!selectedDownloadTask(&task)) {
        QMessageBox::information(this, "Resume download",
                                 "Please select an unfinished download task.");
        return;
    }
    if (!m_networkClient->isConnected()) {
        QMessageBox::information(this, "Resume download",
                                 QString("Connect to %1:%2 first.")
                                 .arg(task.serverIp)
                                 .arg(task.serverPort));
        return;
    }
    if (m_networkClient->serverIp() != task.serverIp ||
            m_networkClient->serverPort() != task.serverPort) {
        QMessageBox::warning(this, "Resume download",
                             QString("This task belongs to %1:%2.")
                             .arg(task.serverIp)
                             .arg(task.serverPort));
        return;
    }

    QString error;
    if (!prepareSafeDownloadOffset(&task, &error)) {
        QMessageBox::warning(this, "Resume download", error);
        refreshDownloadTaskTable();
        return;
    }

    m_downloading = true;
    m_resumingDownload = true;
    m_hasCurrentDownloadTask = true;
    m_currentDownloadTask = task;
    m_playAfterDownload = task.playAfterDownload;
    m_downloadFileName = task.remoteFileName;
    m_downloadPartPath = task.localPartPath;
    m_downloadFinalPath = task.localFinalPath;
    m_downloadFileSize = task.expectedFileSize;
    m_downloadModifiedTime = task.expectedModifiedTime;
    m_downloadOffset = task.confirmedOffset;
    m_downloadProgress->setValue(
                static_cast<int>((m_downloadOffset * 100) / m_downloadFileSize));
    m_downloadStatusLabel->setText(QString("Resuming %1").arg(m_downloadFileName));
    updateActionStates();
    appendLog(QString("requesting download resume at %1 bytes: %2")
              .arg(m_downloadOffset)
              .arg(m_downloadFileName));

    if (!m_networkClient->sendDownloadInit(m_downloadFileName,
                                           m_downloadOffset,
                                           m_downloadFileSize,
                                           m_downloadModifiedTime)) {
        finishDownloadState(false, "failed to send download resume request");
    }
}

void RemoteMediaPage::slotAbandonDownloadClicked()
{
    DownloadTaskState task;
    if (!selectedDownloadTask(&task)) {
        QMessageBox::information(this, "Abandon download",
                                 "Please select an unfinished download task.");
        return;
    }
    if (QMessageBox::question(this,
                              "Abandon download",
                              QString("Remove the recovery record and partial file for %1?")
                              .arg(task.remoteFileName)) != QMessageBox::Yes) {
        return;
    }

    if (QFileInfo::exists(task.localPartPath) && !QFile::remove(task.localPartPath)) {
        QMessageBox::warning(this, "Abandon download",
                             "Cannot remove the local partial file.");
        return;
    }
    QString error;
    if (!m_downloadTaskStore.remove(task.taskId, &error)) {
        QMessageBox::warning(this, "Abandon download", error);
        return;
    }
    appendLog(QString("local download task abandoned: %1")
              .arg(task.remoteFileName));
    refreshDownloadTaskTable();
}

void RemoteMediaPage::slotDownloadInitResponse(bool success,
                                               const QString &fileName,
                                               qint64 fileSize,
                                               qint64 modifiedTime,
                                               qint64 acceptedOffset,
                                               const QString &message)
{
    if (!m_downloading)
        return;
    if (!success) {
        finishDownloadState(false,
                            QString("download initialization failed: %1").arg(message),
                            message == "remote file changed"
                            ? "remote file changed" : "waiting");
        return;
    }
    if (fileName != m_downloadFileName || fileSize <= 0 ||
            modifiedTime <= 0 || acceptedOffset < 0 ||
            acceptedOffset > fileSize) {
        finishDownloadState(false, "server returned invalid download metadata");
        return;
    }
    if (!isSafeCacheFileName(fileName)) {
        finishDownloadState(false, "server returned an unsafe cache file name");
        return;
    }

    const QString cacheDir = QDir::cleanPath(
                QCoreApplication::applicationDirPath() + "/../cache");
    if (!QDir().mkpath(cacheDir)) {
        finishDownloadState(false, "failed to create local cache directory");
        return;
    }

    if (m_resumingDownload) {
        if (!m_hasCurrentDownloadTask ||
                fileSize != m_currentDownloadTask.expectedFileSize ||
                modifiedTime != m_currentDownloadTask.expectedModifiedTime ||
                acceptedOffset > QFileInfo(m_downloadPartPath).size()) {
            finishDownloadState(false, "download resume metadata mismatch");
            return;
        }

        m_downloadFile->setFileName(m_downloadPartPath);
        if (!m_downloadFile->open(QIODevice::ReadWrite) ||
                !m_downloadFile->resize(acceptedOffset) ||
                !m_downloadFile->seek(acceptedOffset)) {
            finishDownloadState(false,
                                QString("failed to open partial download: %1")
                                .arg(m_downloadFile->errorString()));
            return;
        }
        m_downloadOffset = acceptedOffset;
        m_currentDownloadTask.confirmedOffset = acceptedOffset;
        if (!saveCurrentDownloadTask("downloading")) {
            finishDownloadState(false, "failed to update local download state");
            return;
        }
        appendLog(QString("download resumed at %1 bytes").arg(acceptedOffset));
    } else {
        if (acceptedOffset != 0) {
            finishDownloadState(false, "new download returned non-zero offset");
            return;
        }

        m_downloadFileSize = fileSize;
        m_downloadModifiedTime = modifiedTime;
        m_downloadFinalPath = QDir(cacheDir).filePath(fileName);
        m_downloadPartPath = m_downloadFinalPath + ".part";
        QFile::remove(m_downloadPartPath);
        m_downloadFile->setFileName(m_downloadPartPath);
        if (!m_downloadFile->open(QIODevice::ReadWrite | QIODevice::Truncate)) {
            finishDownloadState(false,
                                QString("failed to create cache file: %1")
                                .arg(m_downloadFile->errorString()));
            return;
        }

        m_currentDownloadTask = DownloadTaskState();
        m_currentDownloadTask.taskId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        m_currentDownloadTask.remoteFileName = fileName;
        m_currentDownloadTask.localPartPath = m_downloadPartPath;
        m_currentDownloadTask.localFinalPath = m_downloadFinalPath;
        m_currentDownloadTask.serverIp = m_networkClient->serverIp();
        m_currentDownloadTask.serverPort = m_networkClient->serverPort();
        m_currentDownloadTask.expectedFileSize = fileSize;
        m_currentDownloadTask.expectedModifiedTime = modifiedTime;
        m_currentDownloadTask.confirmedOffset = 0;
        m_currentDownloadTask.playAfterDownload = m_playAfterDownload;
        m_hasCurrentDownloadTask = true;
        if (!saveCurrentDownloadTask("downloading")) {
            m_downloadFile->close();
            QFile::remove(m_downloadPartPath);
            m_hasCurrentDownloadTask = false;
            finishDownloadState(false, "failed to save local download state");
            return;
        }
        appendLog(QString("download initialized: %1 bytes, mtime=%2")
                  .arg(fileSize)
                  .arg(modifiedTime));
    }

    m_downloadStatusLabel->setText(QString("Downloading %1").arg(fileName));
    m_downloadProgress->setValue(
                static_cast<int>((m_downloadOffset * 100) / m_downloadFileSize));
    refreshDownloadTaskTable();
    continueDownloadAfterInitialization();
}

void RemoteMediaPage::slotDownloadBlockResponse(bool success,
                                                const QString &fileName,
                                                qint64 offset,
                                                const QByteArray &data,
                                                const QString &message)
{
    if (!m_downloading)
        return;
    if (!success) {
        finishDownloadState(false, QString("download block failed: %1").arg(message));
        return;
    }
    if (fileName != m_downloadFileName ||
            offset != m_downloadOffset ||
            data.isEmpty() ||
            data.size() > 64 * 1024 ||
            m_downloadOffset < 0 ||
            m_downloadOffset > m_downloadFileSize ||
            data.size() > m_downloadFileSize - m_downloadOffset) {
        finishDownloadState(false, "server returned an invalid download block");
        return;
    }

    if (m_downloadFile->write(data) != data.size() || !m_downloadFile->flush()) {
        finishDownloadState(false,
                            QString("failed to write cache file: %1")
                            .arg(m_downloadFile->errorString()));
        return;
    }
    m_downloadOffset += data.size();
    m_currentDownloadTask.confirmedOffset = m_downloadOffset;
    if (!saveCurrentDownloadTask("downloading")) {
        finishDownloadState(false, "failed to persist download progress");
        return;
    }
    m_downloadProgress->setValue(
                static_cast<int>((m_downloadOffset * 100) / m_downloadFileSize));
    refreshDownloadTaskTable();

    if (m_downloadOffset == m_downloadFileSize) {
        m_downloadFile->flush();
        m_downloadFile->close();
        m_downloadStatusLabel->setText(QString("Finalizing %1").arg(m_downloadFileName));
        if (!m_networkClient->sendDownloadFinish(m_downloadFileName,
                                                 m_downloadFileSize)) {
            finishDownloadState(false, "failed to send download completion request");
        }
        return;
    }

    QTimer::singleShot(0, this, [this]() {
        requestNextDownloadBlock();
    });
}

void RemoteMediaPage::slotDownloadFinishResponse(bool success,
                                                 const QString &fileName,
                                                 const QString &message)
{
    if (!m_downloading)
        return;
    if (!success || fileName != m_downloadFileName) {
        finishDownloadState(false, QString("download completion failed: %1").arg(message));
        return;
    }
    if (QFileInfo(m_downloadPartPath).size() != m_downloadFileSize) {
        finishDownloadState(false, "local cache file size does not match");
        return;
    }

    QFile::remove(m_downloadFinalPath);
    if (!QFile::rename(m_downloadPartPath, m_downloadFinalPath)) {
        finishDownloadState(false, "failed to finalize local cache file");
        return;
    }

    const QString localPath = m_downloadFinalPath;
    const bool shouldPlay = m_playAfterDownload;
    finishDownloadState(true, QString("download completed: %1").arg(localPath));
    if (shouldPlay)
        emit requestPlayLocalFile(localPath);
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
    slotSelectionChanged();
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
    if (!m_uploading || m_transferId.isEmpty())
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
    if (!m_networkClient->sendUploadBlock(m_transferId, m_confirmedOffset, block))
        finishUploadState(false, "failed to send upload block");
}

void RemoteMediaPage::continueUploadAfterConfirmation()
{
    if (!m_uploading)
        return;
    refreshUploadTaskTable();
    if (m_confirmedOffset == m_uploadFileSize) {
        m_uploadStatusLabel->setText(QString("Finalizing %1").arg(m_uploadFileName));
        if (!m_networkClient->sendUploadFinish(m_transferId,
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

void RemoteMediaPage::finishUploadState(bool success, const QString &message)
{
    if (m_uploadFile->isOpen())
        m_uploadFile->close();

    if (m_hasCurrentUploadTask) {
        QString error;
        if (success) {
            if (!m_uploadTaskStore.remove(m_currentUploadTask.transferId, &error))
                appendLog(QString("warning: failed to remove local upload task: %1").arg(error));
        } else if (!saveCurrentUploadTask("waiting")) {
            appendLog("warning: failed to preserve local upload recovery state");
        }
    }

    m_uploading = false;
    m_resumingUpload = false;
    m_hasCurrentUploadTask = false;
    m_currentUploadTask = UploadTaskState();
    m_transferId.clear();
    m_uploadFileName.clear();
    m_uploadFileSize = 0;
    m_confirmedOffset = 0;
    m_expectedOffset = 0;
    m_uploadStatusLabel->setText(success ? "Upload completed" : "Upload stopped");
    if (success)
        m_uploadProgress->setValue(100);
    appendLog(message);
    refreshUploadTaskTable();
    updateActionStates();
}

bool RemoteMediaPage::saveCurrentUploadTask(const QString &status)
{
    if (!m_hasCurrentUploadTask || m_currentUploadTask.transferId.isEmpty())
        return false;
    m_currentUploadTask.confirmedOffset = m_confirmedOffset;
    m_currentUploadTask.status = status;
    QString error;
    if (!m_uploadTaskStore.save(m_currentUploadTask, &error)) {
        appendLog(QString("failed to save local upload task: %1").arg(error));
        return false;
    }
    return true;
}

void RemoteMediaPage::refreshUploadTaskTable()
{
    const QSignalBlocker blocker(m_uploadTaskTable);
    const QString selectedId = m_uploadTaskTable->currentRow() >= 0 &&
            m_uploadTaskTable->item(m_uploadTaskTable->currentRow(), 0)
            ? m_uploadTaskTable->item(m_uploadTaskTable->currentRow(), 0)
              ->data(Qt::UserRole).toString()
            : QString();

    QStringList warnings;
    const QList<UploadTaskState> tasks = m_uploadTaskStore.loadAll(&warnings);
    m_uploadTaskTable->setRowCount(0);
    int selectedRow = -1;
    for (const UploadTaskState &task : tasks) {
        const int row = m_uploadTaskTable->rowCount();
        m_uploadTaskTable->insertRow(row);
        QTableWidgetItem *nameItem = new QTableWidgetItem(task.fileName);
        nameItem->setData(Qt::UserRole, task.transferId);
        m_uploadTaskTable->setItem(row, 0, nameItem);
        m_uploadTaskTable->setItem(row, 1,
                                   new QTableWidgetItem(QString::number(task.fileSize)));
        const int progress = static_cast<int>((task.confirmedOffset * 100) /
                                              task.fileSize);
        m_uploadTaskTable->setItem(row, 2,
                                   new QTableWidgetItem(QString("%1%").arg(progress)));
        m_uploadTaskTable->setItem(row, 3,
                                   new QTableWidgetItem(localTaskStatus(task)));
        m_uploadTaskTable->setItem(row, 4,
                                   new QTableWidgetItem(QString("%1:%2")
                                                        .arg(task.serverIp)
                                                        .arg(task.serverPort)));
        if (task.transferId == selectedId)
            selectedRow = row;
    }
    if (selectedRow >= 0)
        m_uploadTaskTable->selectRow(selectedRow);
    for (const QString &warning : warnings)
        appendLog(QString("upload task warning: %1").arg(warning));
    updateActionStates();
}

bool RemoteMediaPage::selectedUploadTask(UploadTaskState *task) const
{
    if (!task)
        return false;
    const int row = m_uploadTaskTable->currentRow();
    QTableWidgetItem *item = row >= 0 ? m_uploadTaskTable->item(row, 0) : nullptr;
    if (!item)
        return false;
    const QString transferId = item->data(Qt::UserRole).toString();
    const QList<UploadTaskState> tasks = m_uploadTaskStore.loadAll();
    for (const UploadTaskState &candidate : tasks) {
        if (candidate.transferId == transferId) {
            *task = candidate;
            return true;
        }
    }
    return false;
}

QString RemoteMediaPage::localTaskStatus(const UploadTaskState &task) const
{
    const QFileInfo info(task.localFilePath);
    if (!info.exists() || !info.isFile())
        return "Local file missing";
    if (info.size() != task.fileSize ||
            info.lastModified().toMSecsSinceEpoch() != task.lastModifiedMs) {
        return "Local file changed";
    }
    if (m_uploading && m_hasCurrentUploadTask &&
            task.transferId == m_currentUploadTask.transferId) {
        return m_resumingUpload ? "Resuming" : "Uploading";
    }
    return task.status == "uploading" ? "Waiting to resume" : task.status;
}

bool RemoteMediaPage::isSupportedMediaFile(const QString &filePath) const
{
    static const QStringList extensions =
            QStringList() << "mp4" << "flv" << "avi" << "mkv" << "mov"
                          << "wmv" << "mp3" << "aac" << "wav";
    return extensions.contains(QFileInfo(filePath).suffix().toLower());
}

void RemoteMediaPage::startDownload(bool playAfterDownload)
{
    if (!m_networkClient->isConnected()) {
        appendLog("please connect to server first");
        QMessageBox::information(this, "Download media", "Please connect to server first.");
        return;
    }
    if (m_uploading || m_downloading)
        return;

    const QString fileName = selectedMediaFile();
    if (fileName.isEmpty()) {
        appendLog("please select a remote media file first");
        QMessageBox::information(this,
                                 "Download media",
                                 "Please select a remote media file first.");
        return;
    }
    if (hasConflictingDownloadTask(fileName)) {
        QMessageBox::information(this,
                                 "Download media",
                                 "An unfinished download already uses this cache file. "
                                 "Resume or abandon that task first.");
        return;
    }

    m_downloading = true;
    m_resumingDownload = false;
    m_hasCurrentDownloadTask = false;
    m_playAfterDownload = playAfterDownload;
    m_downloadFileName = fileName;
    m_downloadFileSize = 0;
    m_downloadModifiedTime = 0;
    m_downloadOffset = 0;
    m_downloadPartPath.clear();
    m_downloadFinalPath.clear();
    m_downloadProgress->setValue(0);
    m_downloadStatusLabel->setText(QString("Initializing %1").arg(fileName));
    updateActionStates();
    appendLog(QString("starting download: %1").arg(fileName));

    if (!m_networkClient->sendDownloadInit(fileName, 0, 0, 0))
        finishDownloadState(false, "failed to send download initialization");
}

void RemoteMediaPage::continueDownloadAfterInitialization()
{
    if (!m_downloading)
        return;
    if (m_downloadOffset == m_downloadFileSize) {
        if (m_downloadFile->isOpen())
            m_downloadFile->close();
        m_downloadStatusLabel->setText(QString("Finalizing %1").arg(m_downloadFileName));
        if (!m_networkClient->sendDownloadFinish(m_downloadFileName,
                                                 m_downloadFileSize)) {
            finishDownloadState(false, "failed to send download completion request");
        }
        return;
    }

    QTimer::singleShot(0, this, [this]() {
        requestNextDownloadBlock();
    });
}

void RemoteMediaPage::requestNextDownloadBlock()
{
    if (!m_downloading || m_downloadFileSize <= 0)
        return;
    if (!m_networkClient->isConnected()) {
        finishDownloadState(false, "download stopped: server disconnected");
        return;
    }

    const qint64 remaining = m_downloadFileSize - m_downloadOffset;
    const int requestSize = static_cast<int>(qMin<qint64>(64 * 1024, remaining));
    if (requestSize <= 0 ||
            !m_networkClient->sendDownloadBlock(m_downloadFileName,
                                                m_downloadOffset,
                                                requestSize)) {
        finishDownloadState(false, "failed to request download block");
    }
}

void RemoteMediaPage::finishDownloadState(bool success,
                                          const QString &message,
                                          const QString &failureStatus)
{
    if (m_downloadFile->isOpen())
        m_downloadFile->close();

    if (m_hasCurrentDownloadTask) {
        QString error;
        if (success) {
            if (!m_downloadTaskStore.remove(m_currentDownloadTask.taskId, &error)) {
                appendLog(QString("warning: failed to remove local download task: %1")
                          .arg(error));
            }
        } else if (!saveCurrentDownloadTask(failureStatus)) {
            appendLog("warning: failed to preserve local download recovery state");
        }
    }

    m_downloading = false;
    m_resumingDownload = false;
    m_hasCurrentDownloadTask = false;
    m_playAfterDownload = false;
    m_currentDownloadTask = DownloadTaskState();
    m_downloadFileName.clear();
    m_downloadPartPath.clear();
    m_downloadFinalPath.clear();
    m_downloadFileSize = 0;
    m_downloadModifiedTime = 0;
    m_downloadOffset = 0;
    m_downloadStatusLabel->setText(success ? "Download completed" : "Download stopped");
    if (success)
        m_downloadProgress->setValue(100);
    appendLog(message);
    refreshDownloadTaskTable();
    updateActionStates();
}

bool RemoteMediaPage::saveCurrentDownloadTask(const QString &status)
{
    if (!m_hasCurrentDownloadTask || m_currentDownloadTask.taskId.isEmpty())
        return false;
    m_currentDownloadTask.confirmedOffset = m_downloadOffset;
    m_currentDownloadTask.playAfterDownload = m_playAfterDownload;
    m_currentDownloadTask.status = status;
    QString error;
    if (!m_downloadTaskStore.save(m_currentDownloadTask, &error)) {
        appendLog(QString("failed to save local download task: %1").arg(error));
        return false;
    }
    return true;
}

void RemoteMediaPage::refreshDownloadTaskTable()
{
    const QSignalBlocker blocker(m_downloadTaskTable);
    const QString selectedId = m_downloadTaskTable->currentRow() >= 0 &&
            m_downloadTaskTable->item(m_downloadTaskTable->currentRow(), 0)
            ? m_downloadTaskTable->item(m_downloadTaskTable->currentRow(), 0)
              ->data(Qt::UserRole).toString()
            : QString();

    QStringList warnings;
    const QList<DownloadTaskState> tasks = m_downloadTaskStore.loadAll(&warnings);
    m_downloadTaskTable->setRowCount(0);
    int selectedRow = -1;
    for (const DownloadTaskState &task : tasks) {
        const qint64 partSize = QFileInfo(task.localPartPath).exists()
                ? QFileInfo(task.localPartPath).size() : 0;
        const qint64 safeOffset = qMin(task.confirmedOffset, partSize);
        const int progress = static_cast<int>((safeOffset * 100) /
                                              task.expectedFileSize);
        const int row = m_downloadTaskTable->rowCount();
        m_downloadTaskTable->insertRow(row);
        QTableWidgetItem *nameItem = new QTableWidgetItem(task.remoteFileName);
        nameItem->setData(Qt::UserRole, task.taskId);
        m_downloadTaskTable->setItem(row, 0, nameItem);
        m_downloadTaskTable->setItem(row, 1,
                                     new QTableWidgetItem(QString::number(safeOffset)));
        m_downloadTaskTable->setItem(row, 2,
                                     new QTableWidgetItem(QString::number(task.expectedFileSize)));
        m_downloadTaskTable->setItem(row, 3,
                                     new QTableWidgetItem(QString("%1%").arg(progress)));
        m_downloadTaskTable->setItem(row, 4,
                                     new QTableWidgetItem(localDownloadTaskStatus(task)));
        m_downloadTaskTable->setItem(row, 5,
                                     new QTableWidgetItem(QString("%1:%2")
                                                          .arg(task.serverIp)
                                                          .arg(task.serverPort)));
        if (task.taskId == selectedId)
            selectedRow = row;
    }
    if (selectedRow >= 0)
        m_downloadTaskTable->selectRow(selectedRow);
    for (const QString &warning : warnings)
        appendLog(QString("download task warning: %1").arg(warning));
    updateActionStates();
}

bool RemoteMediaPage::selectedDownloadTask(DownloadTaskState *task) const
{
    if (!task)
        return false;
    const int row = m_downloadTaskTable->currentRow();
    QTableWidgetItem *item = row >= 0 ? m_downloadTaskTable->item(row, 0) : nullptr;
    if (!item)
        return false;
    const QString taskId = item->data(Qt::UserRole).toString();
    const QList<DownloadTaskState> tasks = m_downloadTaskStore.loadAll();
    for (const DownloadTaskState &candidate : tasks) {
        if (candidate.taskId == taskId) {
            *task = candidate;
            return true;
        }
    }
    return false;
}

QString RemoteMediaPage::localDownloadTaskStatus(const DownloadTaskState &task) const
{
    const QFileInfo partInfo(task.localPartPath);
    if (!partInfo.exists() || !partInfo.isFile())
        return "Partial file missing";
    if (m_downloading && m_hasCurrentDownloadTask &&
            task.taskId == m_currentDownloadTask.taskId) {
        return m_resumingDownload ? "Resuming" : "Downloading";
    }
    return task.status == "downloading" || task.status == "waiting"
            ? "Waiting to resume" : task.status;
}

bool RemoteMediaPage::prepareSafeDownloadOffset(DownloadTaskState *task,
                                                QString *error)
{
    if (!task || !error)
        return false;
    QFile partFile(task->localPartPath);
    if (!partFile.exists()) {
        *error = "The local partial file is missing.";
        return false;
    }
    if (!partFile.open(QIODevice::ReadWrite)) {
        *error = QString("Cannot open the local partial file: %1")
                .arg(partFile.errorString());
        return false;
    }

    const qint64 actualSize = partFile.size();
    const qint64 safeOffset = qMin(task->confirmedOffset, actualSize);
    if (actualSize > safeOffset && !partFile.resize(safeOffset)) {
        *error = QString("Cannot truncate the local partial file: %1")
                .arg(partFile.errorString());
        return false;
    }
    partFile.close();

    if (safeOffset != task->confirmedOffset) {
        task->confirmedOffset = safeOffset;
        task->status = "waiting";
        if (!m_downloadTaskStore.save(*task, error))
            return false;
    }
    return true;
}

bool RemoteMediaPage::hasConflictingDownloadTask(const QString &fileName) const
{
    const QString cacheDir = QDir::cleanPath(
                QCoreApplication::applicationDirPath() + "/../cache");
    const QString partPath = QDir(cacheDir).filePath(fileName) + ".part";
    const QList<DownloadTaskState> tasks = m_downloadTaskStore.loadAll();
    for (const DownloadTaskState &task : tasks) {
        if (QDir::cleanPath(task.localPartPath) == QDir::cleanPath(partPath))
            return true;
    }
    return false;
}

QString RemoteMediaPage::selectedMediaFile() const
{
    const int row = m_table->currentRow();
    QTableWidgetItem *item = row >= 0 ? m_table->item(row, 0) : nullptr;
    return item ? item->text() : QString();
}

bool RemoteMediaPage::isSafeCacheFileName(const QString &fileName) const
{
    return !fileName.isEmpty() &&
            fileName != "." &&
            fileName != ".." &&
            !fileName.contains('/') &&
            !fileName.contains('\\') &&
            !fileName.contains("..") &&
            QFileInfo(fileName).fileName() == fileName;
}

void RemoteMediaPage::updateActionStates()
{
    const bool connected = m_networkClient->isConnected();
    const bool transferBusy = m_uploading || m_downloading;
    m_refreshButton->setEnabled(connected && !transferBusy);
    m_uploadButton->setEnabled(!transferBusy);
    m_downloadButton->setEnabled(!transferBusy);
    m_downloadAndPlayButton->setEnabled(!transferBusy);
    const bool uploadTaskSelected = m_uploadTaskTable->currentRow() >= 0;
    m_resumeUploadButton->setEnabled(uploadTaskSelected && !transferBusy);
    m_abandonUploadButton->setEnabled(uploadTaskSelected && !transferBusy);
    const bool downloadTaskSelected = m_downloadTaskTable->currentRow() >= 0;
    m_resumeDownloadButton->setEnabled(downloadTaskSelected && !transferBusy);
    m_abandonDownloadButton->setEnabled(downloadTaskSelected && !transferBusy);
}
