#ifndef UPLOADMANAGER_H
#define UPLOADMANAGER_H

#include <stdint.h>
#include <stddef.h>
#include <map>
#include <string>

class UploadManager
{
public:
    UploadManager(const std::string &tempDir = "temp",
                  const std::string &mediaDir = "media");
    ~UploadManager();

    bool ensureDirectories();
    bool createUpload(int ownerFd,
                      const std::string &fileName,
                      const std::string &extension,
                      int64_t fileSize,
                      std::string *uploadId,
                      std::string *message);
    bool writeBlock(int ownerFd,
                    const std::string &uploadId,
                    int64_t offset,
                    const char *data,
                    int32_t dataSize,
                    int64_t *receivedOffset,
                    std::string *message);
    bool finishUpload(int ownerFd,
                      const std::string &uploadId,
                      const std::string &fileName,
                      int64_t fileSize,
                      std::string *savedFileName,
                      std::string *message);
    size_t abortByOwner(int ownerFd);
    void abortAll();

    const std::string &tempDir() const;

private:
    struct UploadTask
    {
        int ownerFd;
        std::string fileName;
        std::string extension;
        std::string tempPath;
        int64_t expectedSize;
        int64_t receivedSize;
    };

    bool ensureDirectory(const std::string &path) const;
    bool isSafeFileName(const std::string &fileName) const;
    bool isSupportedExtension(const std::string &extension) const;
    std::string extensionOf(const std::string &fileName) const;
    std::string normalizedExtension(const std::string &extension) const;
    std::string makeUploadId();
    std::string makeAvailableFileName(const std::string &fileName) const;
    bool pathExists(const std::string &path) const;
    void removeTaskFile(const UploadTask &task) const;

private:
    std::string m_tempDir;
    std::string m_mediaDir;
    std::map<std::string, UploadTask> m_tasks;
    unsigned long m_nextId;
};

#endif // UPLOADMANAGER_H
