#ifndef REMOTEMEDIAPAGE_H
#define REMOTEMEDIAPAGE_H

#include <QByteArray>
#include <QWidget>

#include "DownloadTaskStore.h"
#include "UploadTaskStore.h"

class AVNetworkClient;
class QComboBox;
class QLabel;
class QFile;
class QProgressBar;
class QPushButton;
class QTableWidget;
class QTextEdit;

class RemoteMediaPage : public QWidget
{
    Q_OBJECT

public:
    explicit RemoteMediaPage(AVNetworkClient *networkClient, QWidget *parent = nullptr);

signals:
    void requestPlayLocalFile(const QString &filePath);

private slots:
    void slotRefreshClicked();
    void slotConnectedChanged(bool connected);
    void slotAuthenticationChanged(bool authenticated,
                                   quint64 userId,
                                   const QString &username);
    void slotMediaListResponse(bool success,
                               const QString &payload,
                               const QString &message);
    void slotLogMessage(const QString &message);
    void slotUploadClicked();
    void slotUploadInitResponse(bool success,
                                const QString &transferId,
                                const QString &resumeToken,
                                qint64 resumeOffset,
                                const QString &finalFileName,
                                const QString &message);
    void slotUploadResumeResponse(bool success,
                                  const QString &transferId,
                                  qint64 resumeOffset,
                                  const QString &finalFileName,
                                  const QString &message);
    void slotUploadBlockResponse(bool success,
                                 const QString &transferId,
                                 qint64 receivedOffset,
                                 const QString &message);
    void slotUploadFinishResponse(bool success,
                                  quint64 mediaId,
                                  const QString &fileName,
                                  const QString &message);
    void slotSelectionChanged();
    void slotDownloadClicked();
    void slotDownloadAndPlayClicked();
    void slotDownloadInitResponse(bool success,
                                  quint64 mediaId,
                                  const QString &fileName,
                                  qint64 fileSize,
                                  qint64 modifiedTime,
                                  qint64 acceptedOffset,
                                  const QString &message);
    void slotDownloadBlockResponse(bool success,
                                   quint64 mediaId,
                                   const QString &fileName,
                                   qint64 offset,
                                   const QByteArray &data,
                                   const QString &message);
    void slotDownloadFinishResponse(bool success,
                                    quint64 mediaId,
                                    const QString &fileName,
                                    const QString &message);
    void slotUploadTaskSelectionChanged();
    void slotResumeUploadClicked();
    void slotAbandonUploadClicked();
    void slotDownloadTaskSelectionChanged();
    void slotResumeDownloadClicked();
    void slotAbandonDownloadClicked();

private:
    void appendLog(const QString &message);
    void clearTable();
    void fillTable(const QString &payload);
    void sendNextUploadBlock();
    void continueUploadAfterConfirmation();
    void finishUploadState(bool success, const QString &message);
    bool saveCurrentUploadTask(const QString &status);
    void refreshUploadTaskTable();
    bool selectedUploadTask(UploadTaskState *task) const;
    QString localTaskStatus(const UploadTaskState &task) const;
    bool isSupportedMediaFile(const QString &filePath) const;
    void startDownload(bool playAfterDownload);
    void continueDownloadAfterInitialization();
    void requestNextDownloadBlock();
    void finishDownloadState(bool success,
                             const QString &message,
                             const QString &failureStatus = "waiting");
    bool saveCurrentDownloadTask(const QString &status);
    void refreshDownloadTaskTable();
    bool selectedDownloadTask(DownloadTaskState *task) const;
    QString localDownloadTaskStatus(const DownloadTaskState &task) const;
    bool prepareSafeDownloadOffset(DownloadTaskState *task, QString *error);
    bool hasConflictingDownloadTask(const QString &fileName) const;
    quint64 selectedMediaId() const;
    QString selectedMediaFile() const;
    bool isSafeCacheFileName(const QString &fileName) const;
    void updateActionStates();

private:
    AVNetworkClient *m_networkClient;
    QLabel *m_statusLabel;
    QComboBox *m_scopeCombo;
    QPushButton *m_refreshButton;
    QPushButton *m_uploadButton;
    QLabel *m_uploadStatusLabel;
    QProgressBar *m_uploadProgress;
    QTableWidget *m_uploadTaskTable;
    QPushButton *m_resumeUploadButton;
    QPushButton *m_abandonUploadButton;
    QTableWidget *m_table;
    QTextEdit *m_logEdit;
    QFile *m_uploadFile;
    QString m_uploadFileName;
    QString m_transferId;
    qint64 m_uploadFileSize;
    qint64 m_confirmedOffset;
    qint64 m_expectedOffset;
    bool m_uploading;
    bool m_resumingUpload;
    bool m_hasCurrentUploadTask;
    UploadTaskState m_currentUploadTask;
    UploadTaskStore m_uploadTaskStore;
    QPushButton *m_downloadButton;
    QPushButton *m_downloadAndPlayButton;
    QLabel *m_selectedFileLabel;
    QLabel *m_downloadStatusLabel;
    QProgressBar *m_downloadProgress;
    QTableWidget *m_downloadTaskTable;
    QPushButton *m_resumeDownloadButton;
    QPushButton *m_abandonDownloadButton;
    QFile *m_downloadFile;
    quint64 m_downloadMediaId;
    QString m_downloadFileName;
    QString m_downloadPartPath;
    QString m_downloadFinalPath;
    qint64 m_downloadFileSize;
    qint64 m_downloadModifiedTime;
    qint64 m_downloadOffset;
    bool m_downloading;
    bool m_resumingDownload;
    bool m_hasCurrentDownloadTask;
    bool m_playAfterDownload;
    DownloadTaskState m_currentDownloadTask;
    DownloadTaskStore m_downloadTaskStore;
};

#endif // REMOTEMEDIAPAGE_H
