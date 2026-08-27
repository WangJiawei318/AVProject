#ifndef UPLOADMANAGER_H
#define UPLOADMANAGER_H

#include <stdint.h>
#include <stddef.h>
#include <ctime>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <sys/types.h>

class UploadManager
{
public:
    struct CompletedUpload
    {
        uint64_t ownerUserId;
        std::string originalFileName;
        std::string storedFileName;
        std::string storagePath;
        std::string extension;
        int64_t fileSize;
    };

    typedef std::function<bool(const CompletedUpload &,
                               uint64_t *,
                               std::string *)> MediaPublisher;

    UploadManager(const std::string &tempDir = "temp",
                  const std::string &mediaDir = "media");
    ~UploadManager();

    bool initialize();
    bool createUpload(uint64_t ownerConnectionId,
                      uint64_t ownerUserId,
                      const std::string &fileName,
                      const std::string &extension,
                      int64_t fileSize,
                      std::string *transferId,
                      std::string *resumeToken,
                      int64_t *resumeOffset,
                      std::string *finalFileName,
                      std::string *message);
    bool resumeUpload(uint64_t ownerConnectionId,
                      uint64_t ownerUserId,
                      const std::string &transferId,
                      const std::string &resumeToken,
                      const std::string &fileName,
                      int64_t expectedSize,
                      int64_t *resumeOffset,
                      std::string *finalFileName,
                      std::string *message);
    bool writeBlock(uint64_t ownerConnectionId,
                    uint64_t ownerUserId,
                    const std::string &transferId,
                    int64_t offset,
                    const char *data,
                    int32_t dataSize,
                    int64_t *receivedOffset,
                    std::string *message);
    bool finishUpload(uint64_t ownerConnectionId,
                      uint64_t ownerUserId,
                      const std::string &transferId,
                      const std::string &fileName,
                      int64_t fileSize,
                      const MediaPublisher &publisher,
                      std::string *savedFileName,
                      uint64_t *mediaId,
                      std::string *message);
    size_t unbindConnection(uint64_t ownerConnectionId);
    size_t cleanupExpiredTasks(std::time_t now);

    const std::string &tempDir() const;
    const std::string &tasksDir() const;

private:
    struct UploadTask
    {
        std::string transferId;
        std::string resumeToken;
        std::string originalFileName;
        std::string finalFileName;
        std::string extension;
        std::string tempPath;
        int64_t expectedSize;
        int64_t receivedSize;
        std::time_t createdTime;
        std::time_t updatedTime;
        std::string status;
        uint64_t ownerUserId;
        uint64_t activeOwnerConnectionId;
    };

    bool ensureDirectories();
    bool ensureDirectory(const std::string &path, mode_t mode) const;
    bool loadPersistedTasks();
    bool loadTaskFile(const std::string &path,
                      UploadTask *task,
                      std::string *error) const;
    bool persistTask(const UploadTask &task) const;
    bool reconcileTask(UploadTask *task, bool persistCorrection) const;
    void scanOrphanPartFiles() const;
    void removeTaskFiles(const UploadTask &task) const;
    bool isSafeFileName(const std::string &fileName) const;
    bool isSafeIdentifier(const std::string &value) const;
    bool isValidResumeToken(const std::string &value) const;
    bool isSupportedExtension(const std::string &extension) const;
    std::string extensionOf(const std::string &fileName) const;
    std::string normalizedExtension(const std::string &extension) const;
    std::string makeTransferId();
    std::string makeResumeToken() const;
    std::string randomHex(size_t byteCount) const;
    std::string makeAvailableFileName(const std::string &fileName,
                                      const std::string &exceptTransferId = std::string()) const;
    bool pathExists(const std::string &path) const;
    bool finalNameReserved(const std::string &fileName,
                           const std::string &exceptTransferId) const;
    std::string taskPath(const std::string &transferId) const;
    bool secureEquals(const std::string &left, const std::string &right) const;
    size_t cleanupExpiredTasksUnlocked(std::time_t now);

private:
    enum
    {
        kTaskTtlSeconds = 72 * 60 * 60
    };

    std::string m_tempDir;
    std::string m_mediaDir;
    std::string m_tasksDir;
    std::map<std::string, UploadTask> m_tasks;
    unsigned long m_nextId;
    mutable std::mutex m_mutex;
};

#endif // UPLOADMANAGER_H
