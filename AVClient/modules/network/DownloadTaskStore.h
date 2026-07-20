#ifndef DOWNLOADTASKSTORE_H
#define DOWNLOADTASKSTORE_H

#include <QList>
#include <QString>
#include <QStringList>

struct DownloadTaskState
{
    DownloadTaskState();

    QString taskId;
    QString remoteFileName;
    QString localPartPath;
    QString localFinalPath;
    QString serverIp;
    quint16 serverPort;
    qint64 expectedFileSize;
    qint64 expectedModifiedTime;
    qint64 confirmedOffset;
    bool playAfterDownload;
    QString status;
};

class DownloadTaskStore
{
public:
    DownloadTaskStore();

    QList<DownloadTaskState> loadAll(QStringList *warnings = nullptr) const;
    bool save(const DownloadTaskState &task, QString *error = nullptr) const;
    bool remove(const QString &taskId, QString *error = nullptr) const;
    QString stateDirectory() const;

private:
    bool isValidTask(const DownloadTaskState &task) const;
    bool isValidTaskId(const QString &taskId) const;
    bool isSafeRemoteFileName(const QString &fileName) const;
    bool hasExpectedCachePaths(const DownloadTaskState &task) const;
    QString cacheDirectory() const;
    QString taskFilePath(const QString &taskId) const;
};

#endif // DOWNLOADTASKSTORE_H
