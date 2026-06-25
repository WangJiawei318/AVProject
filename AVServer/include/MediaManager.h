#ifndef MEDIAMANAGER_H
#define MEDIAMANAGER_H

#include <string>

class MediaManager
{
public:
    explicit MediaManager(const std::string &mediaDir = "media");

    bool ensureMediaDir();
    std::string buildMediaListPayload(int *mediaCount = nullptr);
    const std::string &mediaDir() const;

private:
    bool isSupportedMediaFile(const std::string &fileName) const;
    std::string extensionOf(const std::string &fileName) const;
    std::string formatModifyTime(long sec) const;
    std::string sanitizeField(const std::string &value) const;

private:
    std::string m_mediaDir;
};

#endif // MEDIAMANAGER_H
