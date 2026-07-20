#include "UploadManager.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

UploadManager::UploadManager(const std::string &tempDir,
                             const std::string &mediaDir)
    : m_tempDir(tempDir),
      m_mediaDir(mediaDir),
      m_nextId(0)
{
}

UploadManager::~UploadManager()
{
    abortAll();
}

bool UploadManager::ensureDirectories()
{
    return ensureDirectory(m_tempDir) && ensureDirectory(m_mediaDir);
}

bool UploadManager::createUpload(int ownerFd,
                                 const std::string &fileName,
                                 const std::string &extension,
                                 int64_t fileSize,
                                 std::string *uploadId,
                                 std::string *message)
{
    if (!ensureDirectories()) {
        *message = "failed to create upload directories";
        return false;
    }
    if (!isSafeFileName(fileName)) {
        *message = "invalid file name";
        return false;
    }

    const std::string ext = normalizedExtension(extension);
    if (!isSupportedExtension(ext) || extensionOf(fileName) != ext) {
        *message = "unsupported media extension";
        return false;
    }
    if (fileSize <= 0) {
        *message = "file size must be greater than zero";
        return false;
    }

    UploadTask task;
    task.ownerFd = ownerFd;
    task.fileName = fileName;
    task.extension = ext;
    task.expectedSize = fileSize;
    task.receivedSize = 0;

    const std::string id = makeUploadId();
    task.tempPath = m_tempDir + "/" + id + ".part";

    std::ofstream output(task.tempPath.c_str(), std::ios::binary | std::ios::trunc);
    if (!output) {
        *message = "failed to create temporary file";
        return false;
    }
    output.close();

    m_tasks[id] = task;
    *uploadId = id;
    *message = "upload initialized";
    return true;
}

bool UploadManager::writeBlock(int ownerFd,
                               const std::string &uploadId,
                               int64_t offset,
                               const char *data,
                               int32_t dataSize,
                               int64_t *receivedOffset,
                               std::string *message)
{
    std::map<std::string, UploadTask>::iterator it = m_tasks.find(uploadId);
    if (it == m_tasks.end()) {
        *message = "unknown upload id";
        return false;
    }

    UploadTask &task = it->second;
    *receivedOffset = task.receivedSize;
    if (task.ownerFd != ownerFd) {
        *message = "upload task belongs to another connection";
        return false;
    }
    if (!data || dataSize <= 0 || dataSize > 64 * 1024) {
        *message = "invalid block size";
        return false;
    }
    if (offset != task.receivedSize) {
        *message = "unexpected block offset";
        return false;
    }
    if (offset + dataSize > task.expectedSize) {
        *message = "block exceeds expected file size";
        return false;
    }

    std::fstream output(task.tempPath.c_str(),
                        std::ios::binary | std::ios::in | std::ios::out);
    if (!output) {
        *message = "failed to open temporary file";
        return false;
    }
    output.seekp(offset);
    output.write(data, dataSize);
    output.flush();
    if (!output) {
        *message = "failed to write upload block";
        return false;
    }

    task.receivedSize += dataSize;
    *receivedOffset = task.receivedSize;
    *message = "block accepted";
    return true;
}

bool UploadManager::finishUpload(int ownerFd,
                                 const std::string &uploadId,
                                 const std::string &fileName,
                                 int64_t fileSize,
                                 std::string *savedFileName,
                                 std::string *message)
{
    std::map<std::string, UploadTask>::iterator it = m_tasks.find(uploadId);
    if (it == m_tasks.end()) {
        *message = "unknown upload id";
        return false;
    }

    UploadTask task = it->second;
    if (task.ownerFd != ownerFd) {
        *message = "upload task belongs to another connection";
        return false;
    }
    if (fileName != task.fileName || fileSize != task.expectedSize) {
        *message = "upload metadata does not match";
        return false;
    }

    struct stat st;
    if (task.receivedSize != task.expectedSize ||
            stat(task.tempPath.c_str(), &st) != 0 ||
            static_cast<int64_t>(st.st_size) != task.expectedSize) {
        *message = "uploaded file size does not match";
        return false;
    }

    const std::string finalName = makeAvailableFileName(task.fileName);
    const std::string finalPath = m_mediaDir + "/" + finalName;
    if (std::rename(task.tempPath.c_str(), finalPath.c_str()) != 0) {
        *message = "failed to move file to media directory";
        return false;
    }

    m_tasks.erase(it);
    *savedFileName = finalName;
    *message = "upload completed";
    return true;
}

size_t UploadManager::abortByOwner(int ownerFd)
{
    size_t removed = 0;
    std::map<std::string, UploadTask>::iterator it = m_tasks.begin();
    while (it != m_tasks.end()) {
        if (it->second.ownerFd == ownerFd) {
            removeTaskFile(it->second);
            it = m_tasks.erase(it);
            ++removed;
        } else {
            ++it;
        }
    }
    return removed;
}

void UploadManager::abortAll()
{
    for (std::map<std::string, UploadTask>::const_iterator it = m_tasks.begin();
         it != m_tasks.end(); ++it) {
        removeTaskFile(it->second);
    }
    m_tasks.clear();
}

const std::string &UploadManager::tempDir() const
{
    return m_tempDir;
}

bool UploadManager::ensureDirectory(const std::string &path) const
{
    struct stat st;
    if (stat(path.c_str(), &st) == 0)
        return S_ISDIR(st.st_mode);
    return mkdir(path.c_str(), 0755) == 0;
}

bool UploadManager::isSafeFileName(const std::string &fileName) const
{
    if (fileName.empty() || fileName == "." || fileName == "..")
        return false;
    if (fileName.find('/') != std::string::npos ||
            fileName.find('\\') != std::string::npos ||
            fileName.find("..") != std::string::npos ||
            fileName.find('\0') != std::string::npos) {
        return false;
    }
    return fileName.size() < 256;
}

bool UploadManager::isSupportedExtension(const std::string &extension) const
{
    static const char *kExtensions[] = {
        "mp4", "flv", "avi", "mkv", "mov", "wmv", "mp3", "aac", "wav"
    };
    for (const char *item : kExtensions) {
        if (extension == item)
            return true;
    }
    return false;
}

std::string UploadManager::extensionOf(const std::string &fileName) const
{
    const std::string::size_type pos = fileName.find_last_of('.');
    if (pos == std::string::npos || pos + 1 >= fileName.size())
        return std::string();
    return normalizedExtension(fileName.substr(pos + 1));
}

std::string UploadManager::normalizedExtension(const std::string &extension) const
{
    std::string result = extension;
    if (!result.empty() && result[0] == '.')
        result.erase(0, 1);
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return result;
}

std::string UploadManager::makeUploadId()
{
    std::ostringstream value;
    value << static_cast<unsigned long>(std::time(nullptr))
          << "_" << static_cast<unsigned long>(getpid())
          << "_" << ++m_nextId;
    return value.str();
}

std::string UploadManager::makeAvailableFileName(const std::string &fileName) const
{
    if (!pathExists(m_mediaDir + "/" + fileName))
        return fileName;

    const std::string::size_type dot = fileName.find_last_of('.');
    const std::string base = dot == std::string::npos ? fileName : fileName.substr(0, dot);
    const std::string suffix = dot == std::string::npos ? std::string() : fileName.substr(dot);

    for (unsigned int index = 1; ; ++index) {
        std::ostringstream candidate;
        candidate << base << "_" << index << suffix;
        if (!pathExists(m_mediaDir + "/" + candidate.str()))
            return candidate.str();
    }
}

bool UploadManager::pathExists(const std::string &path) const
{
    struct stat st;
    return stat(path.c_str(), &st) == 0;
}

void UploadManager::removeTaskFile(const UploadTask &task) const
{
    std::remove(task.tempPath.c_str());
}
