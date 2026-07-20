#ifndef UPLOADTASKSTORE_H
#define UPLOADTASKSTORE_H

#include <QList>
#include <QString>
#include <QStringList>

struct UploadTaskState
{
    UploadTaskState();

    QString localFilePath;
    QString fileName;
    qint64 fileSize;
    qint64 lastModifiedMs;
    QString serverIp;
    quint16 serverPort;
    QString transferId;
    QString resumeToken;
    qint64 confirmedOffset;
    QString finalFileName;
    QString status;
};

class UploadTaskStore
{
public:
    UploadTaskStore();

    QList<UploadTaskState> loadAll(QStringList *warnings = nullptr) const;
    bool save(const UploadTaskState &task, QString *error = nullptr) const;
    bool remove(const QString &transferId, QString *error = nullptr) const;
    QString stateDirectory() const;

private:
    bool isValidTransferId(const QString &transferId) const;
    bool isValidResumeToken(const QString &resumeToken) const;
    QString taskFilePath(const QString &transferId) const;
};

#endif // UPLOADTASKSTORE_H
