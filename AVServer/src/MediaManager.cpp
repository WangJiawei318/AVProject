#include "MediaManager.h"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <dirent.h>
#include <sstream>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

MediaManager::MediaManager(const std::string &mediaDir)
    : m_mediaDir(mediaDir)
{
}

bool MediaManager::ensureMediaDir()
{
    struct stat st;
    if (stat(m_mediaDir.c_str(), &st) == 0)
        return S_ISDIR(st.st_mode);

    return mkdir(m_mediaDir.c_str(), 0755) == 0;
}

std::string MediaManager::buildMediaListPayload(int *mediaCount)
{
    if (mediaCount)
        *mediaCount = 0;

    ensureMediaDir();

    DIR *dir = opendir(m_mediaDir.c_str());
    if (!dir)
        return std::string();

    std::ostringstream payload;
    dirent *entry = nullptr;
    while ((entry = readdir(dir)) != nullptr) {
        std::string name = entry->d_name;
        if (name == "." || name == "..")
            continue;

        std::string fullPath = m_mediaDir + "/" + name;
        struct stat st;
        if (stat(fullPath.c_str(), &st) != 0)
            continue;
        if (!S_ISREG(st.st_mode))
            continue;
        if (!isSupportedMediaFile(name))
            continue;

        std::string ext = extensionOf(name);
        payload << sanitizeField(name) << "|"
                << static_cast<long long>(st.st_size) << "|"
                << formatModifyTime(st.st_mtime) << "|"
                << ext << "\n";

        if (mediaCount)
            ++(*mediaCount);
    }

    closedir(dir);
    return payload.str();
}

const std::string &MediaManager::mediaDir() const
{
    return m_mediaDir;
}

bool MediaManager::isSupportedMediaFile(const std::string &fileName) const
{
    static const char *kExts[] = {
        "mp4", "flv", "avi", "mkv", "mov", "wmv", "mp3", "aac", "wav"
    };

    std::string ext = extensionOf(fileName);
    for (const char *item : kExts) {
        if (ext == item)
            return true;
    }
    return false;
}

std::string MediaManager::extensionOf(const std::string &fileName) const
{
    std::string::size_type pos = fileName.find_last_of('.');
    if (pos == std::string::npos || pos + 1 >= fileName.size())
        return std::string();

    std::string ext = fileName.substr(pos + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) {
        return static_cast<char>(::tolower(ch));
    });
    return ext;
}

std::string MediaManager::formatModifyTime(long sec) const
{
    std::time_t value = static_cast<std::time_t>(sec);
    std::tm tmValue;
    localtime_r(&value, &tmValue);

    char buffer[32] = {0};
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &tmValue);
    return buffer;
}

std::string MediaManager::sanitizeField(const std::string &value) const
{
    std::string result = value;
    for (char &ch : result) {
        if (ch == '|' || ch == '\n' || ch == '\r')
            ch = '_';
    }
    return result;
}
