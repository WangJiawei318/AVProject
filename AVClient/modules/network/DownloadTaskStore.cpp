#include "DownloadTaskStore.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

DownloadTaskState::DownloadTaskState()
    : serverPort(0),
      expectedFileSize(0),
      expectedModifiedTime(0),
      confirmedOffset(0),
      playAfterDownload(false)
{
}

DownloadTaskStore::DownloadTaskStore()
{
}

QList<DownloadTaskState> DownloadTaskStore::loadAll(QStringList *warnings) const
{
    QList<DownloadTaskState> tasks;
    QDir directory(stateDirectory());
    if (!directory.exists())
        return tasks;

    const QFileInfoList files = directory.entryInfoList(
                QStringList() << "*.download.json",
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
        DownloadTaskState task;
        task.taskId = object.value("task_id").toString();
        task.remoteFileName = object.value("remote_filename").toString();
        task.localPartPath = object.value("local_part_path").toString();
        task.localFinalPath = object.value("local_final_path").toString();
        task.serverIp = object.value("server_ip").toString();
        task.serverPort = static_cast<quint16>(object.value("server_port").toInt());
        task.expectedFileSize = static_cast<qint64>(
                    object.value("expected_file_size").toDouble());
        task.expectedModifiedTime = static_cast<qint64>(
                    object.value("expected_modified_time").toDouble());
        task.confirmedOffset = static_cast<qint64>(
                    object.value("confirmed_offset").toDouble());
        task.playAfterDownload = object.value("play_after_download").toBool(false);
        task.status = object.value("status").toString("waiting");

        if (!isValidTask(task) ||
                fileInfo.fileName() != task.taskId + ".download.json") {
            if (warnings)
                warnings->append(QString("incomplete task file %1").arg(fileInfo.fileName()));
            continue;
        }
        tasks.append(task);
    }
    return tasks;
}

bool DownloadTaskStore::save(const DownloadTaskState &task, QString *error) const
{
    if (!isValidTask(task)) {
        if (error)
            *error = "invalid local download task";
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
    object.insert("task_id", task.taskId);
    object.insert("remote_filename", task.remoteFileName);
    object.insert("local_part_path", task.localPartPath);
    object.insert("local_final_path", task.localFinalPath);
    object.insert("server_ip", task.serverIp);
    object.insert("server_port", static_cast<int>(task.serverPort));
    object.insert("expected_file_size", static_cast<double>(task.expectedFileSize));
    object.insert("expected_modified_time", static_cast<double>(task.expectedModifiedTime));
    object.insert("confirmed_offset", static_cast<double>(task.confirmedOffset));
    object.insert("play_after_download", task.playAfterDownload);
    object.insert("status", task.status.isEmpty() ? "waiting" : task.status);

    QSaveFile file(taskFilePath(task.taskId));
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

bool DownloadTaskStore::remove(const QString &taskId, QString *error) const
{
    if (!isValidTaskId(taskId)) {
        if (error)
            *error = "invalid download task id";
        return false;
    }
    const QString path = taskFilePath(taskId);
    if (!QFileInfo::exists(path) || QFile::remove(path))
        return true;
    if (error)
        *error = QString("cannot remove %1").arg(QFileInfo(path).fileName());
    return false;
}

QString DownloadTaskStore::stateDirectory() const
{
    return QDir::cleanPath(QCoreApplication::applicationDirPath() +
                           "/../transfer_state");
}

bool DownloadTaskStore::isValidTask(const DownloadTaskState &task) const
{
    return isValidTaskId(task.taskId) &&
            isSafeRemoteFileName(task.remoteFileName) &&
            hasExpectedCachePaths(task) &&
            !task.serverIp.isEmpty() &&
            task.serverPort != 0 &&
            task.expectedFileSize > 0 &&
            task.expectedModifiedTime > 0 &&
            task.confirmedOffset >= 0 &&
            task.confirmedOffset <= task.expectedFileSize;
}

bool DownloadTaskStore::isValidTaskId(const QString &taskId) const
{
    if (taskId.isEmpty() || taskId.size() >= 96)
        return false;
    for (const QChar ch : taskId) {
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

bool DownloadTaskStore::isSafeRemoteFileName(const QString &fileName) const
{
    return !fileName.isEmpty() &&
            fileName != "." &&
            fileName != ".." &&
            !fileName.contains('/') &&
            !fileName.contains('\\') &&
            !fileName.contains("..") &&
            QFileInfo(fileName).fileName() == fileName;
}

bool DownloadTaskStore::hasExpectedCachePaths(const DownloadTaskState &task) const
{
    if (!isSafeRemoteFileName(task.remoteFileName))
        return false;
    const QString expectedFinal = QDir(cacheDirectory()).filePath(task.remoteFileName);
    return QDir::cleanPath(task.localFinalPath) == QDir::cleanPath(expectedFinal) &&
            QDir::cleanPath(task.localPartPath) == QDir::cleanPath(expectedFinal + ".part");
}

QString DownloadTaskStore::cacheDirectory() const
{
    return QDir::cleanPath(QCoreApplication::applicationDirPath() + "/../cache");
}

QString DownloadTaskStore::taskFilePath(const QString &taskId) const
{
    return QDir(stateDirectory()).filePath(taskId + ".download.json");
}
