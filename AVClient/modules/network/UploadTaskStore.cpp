#include "UploadTaskStore.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

UploadTaskState::UploadTaskState()
    : fileSize(0),
      lastModifiedMs(0),
      serverPort(0),
      confirmedOffset(0)
{
}

UploadTaskStore::UploadTaskStore()
{
}

QList<UploadTaskState> UploadTaskStore::loadAll(QStringList *warnings) const
{
    QList<UploadTaskState> tasks;
    QDir directory(stateDirectory());
    if (!directory.exists())
        return tasks;

    const QFileInfoList files = directory.entryInfoList(
                QStringList() << "*.upload.json",
                QDir::Files,
                QDir::Name);
    for (const QFileInfo &fileInfo : files) {
        QFile file(fileInfo.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly)) {
            if (warnings)
                warnings->append(QString("cannot read %1").arg(fileInfo.fileName()));
            continue;
        }

        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            if (warnings)
                warnings->append(QString("invalid task file %1").arg(fileInfo.fileName()));
            continue;
        }

        const QJsonObject object = document.object();
        UploadTaskState task;
        task.localFilePath = object.value("local_file_path").toString();
        task.fileName = object.value("filename").toString();
        task.fileSize = static_cast<qint64>(object.value("file_size").toDouble());
        task.lastModifiedMs = static_cast<qint64>(object.value("last_modified_ms").toDouble());
        task.serverIp = object.value("server_ip").toString();
        task.serverPort = static_cast<quint16>(object.value("server_port").toInt());
        task.transferId = object.value("transfer_id").toString();
        task.resumeToken = object.value("resume_token").toString();
        task.confirmedOffset = static_cast<qint64>(
                    object.value("confirmed_offset").toDouble());
        task.finalFileName = object.value("final_filename").toString();
        task.status = object.value("status").toString("waiting");

        if (!isValidTransferId(task.transferId) ||
                !isValidResumeToken(task.resumeToken) ||
                fileInfo.fileName() != task.transferId + ".upload.json" ||
                task.localFilePath.isEmpty() ||
                task.fileName.isEmpty() ||
                task.fileSize <= 0 ||
                task.lastModifiedMs <= 0 ||
                task.serverIp.isEmpty() ||
                task.serverPort == 0 ||
                task.confirmedOffset < 0 ||
                task.confirmedOffset > task.fileSize) {
            if (warnings)
                warnings->append(QString("incomplete task file %1").arg(fileInfo.fileName()));
            continue;
        }
        tasks.append(task);
    }
    return tasks;
}

bool UploadTaskStore::save(const UploadTaskState &task, QString *error) const
{
    if (!isValidTransferId(task.transferId) ||
            !isValidResumeToken(task.resumeToken) ||
            task.localFilePath.isEmpty() ||
            task.fileName.isEmpty() ||
            task.fileSize <= 0 ||
            task.lastModifiedMs <= 0 ||
            task.serverIp.isEmpty() ||
            task.serverPort == 0 ||
            task.confirmedOffset < 0 ||
            task.confirmedOffset > task.fileSize) {
        if (error)
            *error = "invalid local upload task";
        return false;
    }

    QDir directory;
    if (!directory.mkpath(stateDirectory())) {
        if (error)
            *error = "cannot create transfer_state directory";
        return false;
    }

    QJsonObject object;
    object.insert("version", 1);
    object.insert("local_file_path", task.localFilePath);
    object.insert("filename", task.fileName);
    object.insert("file_size", static_cast<double>(task.fileSize));
    object.insert("last_modified_ms", static_cast<double>(task.lastModifiedMs));
    object.insert("server_ip", task.serverIp);
    object.insert("server_port", static_cast<int>(task.serverPort));
    object.insert("transfer_id", task.transferId);
    object.insert("resume_token", task.resumeToken);
    object.insert("confirmed_offset", static_cast<double>(task.confirmedOffset));
    object.insert("final_filename", task.finalFileName);
    object.insert("status", task.status.isEmpty() ? "waiting" : task.status);

    QSaveFile file(taskFilePath(task.transferId));
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    if (file.write(QJsonDocument(object).toJson(QJsonDocument::Indented)) < 0 ||
            !file.commit()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    return true;
}

bool UploadTaskStore::remove(const QString &transferId, QString *error) const
{
    if (!isValidTransferId(transferId)) {
        if (error)
            *error = "invalid transfer id";
        return false;
    }
    const QString path = taskFilePath(transferId);
    if (!QFileInfo::exists(path) || QFile::remove(path))
        return true;
    if (error)
        *error = QString("cannot remove %1").arg(QFileInfo(path).fileName());
    return false;
}

QString UploadTaskStore::stateDirectory() const
{
    return QDir::cleanPath(QCoreApplication::applicationDirPath() +
                           "/../transfer_state");
}

bool UploadTaskStore::isValidTransferId(const QString &transferId) const
{
    if (transferId.isEmpty() || transferId.size() >= 96)
        return false;
    for (const QChar ch : transferId) {
        const ushort value = ch.unicode();
        const bool asciiAlphaNumeric =
                (value >= '0' && value <= '9') ||
                (value >= 'A' && value <= 'Z') ||
                (value >= 'a' && value <= 'z');
        if (!asciiAlphaNumeric && value != '_' && value != '-')
            return false;
    }
    return true;
}

bool UploadTaskStore::isValidResumeToken(const QString &resumeToken) const
{
    if (resumeToken.size() != 64)
        return false;
    for (const QChar ch : resumeToken) {
        const ushort value = ch.unicode();
        const bool hexadecimal =
                (value >= '0' && value <= '9') ||
                (value >= 'A' && value <= 'F') ||
                (value >= 'a' && value <= 'f');
        if (!hexadecimal)
            return false;
    }
    return true;
}

QString UploadTaskStore::taskFilePath(const QString &transferId) const
{
    return QDir(stateDirectory()).filePath(transferId + ".upload.json");
}
