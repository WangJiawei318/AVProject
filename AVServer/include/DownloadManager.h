#ifndef DOWNLOADMANAGER_H
#define DOWNLOADMANAGER_H

#include <stdint.h>
#include <string>
#include <vector>

class DownloadManager
{
public:
    explicit DownloadManager(const std::string &mediaDir = "media");

    bool getFileInfo(const std::string &fileName,
                     int64_t *fileSize,
                     std::string *message) const;
    bool readBlock(const std::string &fileName,
                   int64_t offset,
                   int32_t requestSize,
                   std::vector<char> *data,
                   std::string *message) const;
    bool validateCompletion(const std::string &fileName,
                            int64_t fileSize,
                            std::string *message) const;

private:
    bool isSafeFileName(const std::string &fileName) const;
    bool isSupportedMediaFile(const std::string &fileName) const;
    std::string extensionOf(const std::string &fileName) const;
    std::string filePath(const std::string &fileName) const;

private:
    std::string m_mediaDir;
};

#endif // DOWNLOADMANAGER_H
