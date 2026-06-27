#ifndef REMOTEMEDIAPAGE_H
#define REMOTEMEDIAPAGE_H

#include <QByteArray>
#include <QWidget>

class AVNetworkClient;
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
    void slotMediaListReceived(const QString &payload);
    void slotLogMessage(const QString &message);
    void slotUploadClicked();
    void slotUploadInitResponse(bool success,
                                const QString &uploadId,
                                const QString &message);
    void slotUploadBlockResponse(bool success,
                                 const QString &uploadId,
                                 qint64 receivedOffset,
                                 const QString &message);
    void slotUploadFinishResponse(bool success,
                                  const QString &fileName,
                                  const QString &message);
    void slotSelectionChanged();
    void slotDownloadClicked();
    void slotDownloadAndPlayClicked();
    void slotDownloadInitResponse(bool success,
                                  const QString &fileName,
                                  qint64 fileSize,
                                  const QString &message);
    void slotDownloadBlockResponse(bool success,
                                   const QString &fileName,
                                   qint64 offset,
                                   const QByteArray &data,
                                   const QString &message);
    void slotDownloadFinishResponse(bool success,
                                    const QString &fileName,
                                    const QString &message);

private:
    void appendLog(const QString &message);
    void clearTable();
    void fillTable(const QString &payload);
    void sendNextUploadBlock();
    void finishUploadState(bool success, const QString &message);
    bool isSupportedMediaFile(const QString &filePath) const;
    void startDownload(bool playAfterDownload);
    void requestNextDownloadBlock();
    void finishDownloadState(bool success, const QString &message);
    QString selectedMediaFile() const;
    bool isSafeCacheFileName(const QString &fileName) const;
    void updateActionStates();

private:
    AVNetworkClient *m_networkClient;
    QLabel *m_statusLabel;
    QPushButton *m_refreshButton;
    QPushButton *m_uploadButton;
    QLabel *m_uploadStatusLabel;
    QProgressBar *m_uploadProgress;
    QTableWidget *m_table;
    QTextEdit *m_logEdit;
    QFile *m_uploadFile;
    QString m_uploadFileName;
    QString m_uploadId;
    qint64 m_uploadFileSize;
    qint64 m_confirmedOffset;
    qint64 m_expectedOffset;
    bool m_uploading;
    QPushButton *m_downloadButton;
    QPushButton *m_downloadAndPlayButton;
    QLabel *m_selectedFileLabel;
    QLabel *m_downloadStatusLabel;
    QProgressBar *m_downloadProgress;
    QFile *m_downloadFile;
    QString m_downloadFileName;
    QString m_downloadPartPath;
    QString m_downloadFinalPath;
    qint64 m_downloadFileSize;
    qint64 m_downloadOffset;
    bool m_downloading;
    bool m_playAfterDownload;
};

#endif // REMOTEMEDIAPAGE_H
