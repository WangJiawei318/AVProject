#include "DownloadManager.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sys/stat.h>

namespace {

static const int32_t kMaxDownloadBlockSize = 64 * 1024;

} // namespace

DownloadManager::DownloadManager(const std::string &mediaDir)
    : m_mediaDir(mediaDir)
{
}

bool DownloadManager::getFileInfo(const std::string &fileName,
                                  int64_t *fileSize,
                                  std::string *message) const
{
    if (!fileSize || !message)
        return false;
    if (!isSafeFileName(fileName) || !isSupportedMediaFile(fileName)) {
        *message = "invalid media file name";
        return false;
    }

    struct stat st;
    if (stat(filePath(fileName).c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
        *message = "media file does not exist";
        return false;
    }
    if (st.st_size <= 0) {
        *message = "media file is empty";
        return false;
    }

    *fileSize = static_cast<int64_t>(st.st_size);
    *message = "download initialized";
    return true;
}

bool DownloadManager::readBlock(const std::string &fileName,
                                int64_t offset,
                                int32_t requestSize,
                                std::vector<char> *data,
                                std::string *message) const
{
    if (!data || !message)
        return false;

    int64_t fileSize = 0;
    if (!getFileInfo(fileName, &fileSize, message))
        return false;
    if (offset < 0 || offset >= fileSize) {
        *message = "invalid download offset";
        return false;
    }
    if (requestSize <= 0 || requestSize > kMaxDownloadBlockSize) {
        *message = "invalid download block size";
        return false;
    }

    const int64_t remaining = fileSize - offset;
    const int32_t readSize = remaining < requestSize
            ? static_cast<int32_t>(remaining)
            : requestSize;

    std::ifstream input(filePath(fileName).c_str(), std::ios::binary);
    if (!input) {
        *message = "failed to open media file";
        return false;
    }
    input.seekg(offset);
    if (!input) {
        *message = "failed to seek media file";
        return false;
    }

    data->resize(readSize);
    input.read(data->data(), readSize);
    const std::streamsize actualSize = input.gcount();
    if (actualSize <= 0) {
        data->clear();
        *message = "failed to read media file";
        return false;
    }
    data->resize(static_cast<size_t>(actualSize));
    *message = "download block ready";
    return true;
}

bool DownloadManager::validateCompletion(const std::string &fileName,
                                         int64_t fileSize,
                                         std::string *message) const
{
    int64_t currentSize = 0;
    if (!getFileInfo(fileName, &currentSize, message))
        return false;
    if (currentSize != fileSize) {
        *message = "download file size does not match";
        return false;
    }

    *message = "download completed";
    return true;
}

bool DownloadManager::isSafeFileName(const std::string &fileName) const
{
    if (fileName.empty() || fileName == "." || fileName == "..")
        return false;
    if (fileName.size() >= 256 ||
            fileName.find('/') != std::string::npos ||
            fileName.find('\\') != std::string::npos ||
            fileName.find("..") != std::string::npos) {
        return false;
    }
    return true;
}

bool DownloadManager::isSupportedMediaFile(const std::string &fileName) const
{
    static const char *kExtensions[] = {
        "mp4", "flv", "avi", "mkv", "mov", "wmv", "mp3", "aac", "wav"
    };
    const std::string extension = extensionOf(fileName);
    for (const char *item : kExtensions) {
        if (extension == item)
            return true;
    }
    return false;
}

std::string DownloadManager::extensionOf(const std::string &fileName) const
{
    const std::string::size_type dot = fileName.find_last_of('.');
    if (dot == std::string::npos || dot + 1 >= fileName.size())
        return std::string();

    std::string result = fileName.substr(dot + 1);
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return result;
}

std::string DownloadManager::filePath(const std::string &fileName) const
{
    return m_mediaDir + "/" + fileName;
}
