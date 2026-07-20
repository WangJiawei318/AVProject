#include "UploadManager.h"

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <iomanip>
#include <random>
#include <set>
#include <sstream>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <vector>

namespace {

bool hasSuffix(const std::string &value, const std::string &suffix)
{
    return value.size() >= suffix.size() &&
            value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool parseInt64(const std::string &value, int64_t *result)
{
    if (!result || value.empty())
        return false;
    errno = 0;
    char *end = nullptr;
    const long long parsed = std::strtoll(value.c_str(), &end, 10);
    if (errno != 0 || !end || *end != '\0')
        return false;
    *result = static_cast<int64_t>(parsed);
    return true;
}

bool writeAll(int fd, const char *data, size_t size)
{
    size_t written = 0;
    while (written < size) {
        const ssize_t result = write(fd, data + written, size - written);
        if (result > 0) {
            written += static_cast<size_t>(result);
            continue;
        }
        if (result < 0 && errno == EINTR)
            continue;
        return false;
    }
    return true;
}

} // namespace

UploadManager::UploadManager(const std::string &tempDir,
                             const std::string &mediaDir)
    : m_tempDir(tempDir),
      m_mediaDir(mediaDir),
      m_tasksDir(tempDir + "/tasks"),
      m_nextId(0)
{
}

UploadManager::~UploadManager()
{
    m_tasks.clear();
}

bool UploadManager::initialize()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!ensureDirectories())
        return false;
    if (!loadPersistedTasks())
        return false;
    cleanupExpiredTasksUnlocked(std::time(nullptr));
    scanOrphanPartFiles();
    return true;
}

bool UploadManager::createUpload(uint64_t ownerConnectionId,
                                 const std::string &fileName,
                                 const std::string &extension,
                                 int64_t fileSize,
                                 std::string *transferId,
                                 std::string *resumeToken,
                                 int64_t *resumeOffset,
                                 std::string *finalFileName,
                                 std::string *message)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!transferId || !resumeToken || !resumeOffset || !finalFileName || !message)
        return false;
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
    task.transferId = makeTransferId();
    task.resumeToken = makeResumeToken();
    task.originalFileName = fileName;
    task.finalFileName = makeAvailableFileName(fileName);
    task.extension = ext;
    task.tempPath = m_tempDir + "/" + task.transferId + ".part";
    task.expectedSize = fileSize;
    task.receivedSize = 0;
    task.createdTime = std::time(nullptr);
    task.updatedTime = task.createdTime;
    task.status = "uploading";
    task.activeOwnerConnectionId = ownerConnectionId;

    const int partFd = open(task.tempPath.c_str(),
                            O_WRONLY | O_CREAT | O_EXCL,
                            0600);
    if (partFd < 0) {
        *message = "failed to create temporary file";
        return false;
    }
    close(partFd);

    if (!persistTask(task)) {
        std::remove(task.tempPath.c_str());
        *message = "failed to persist upload task";
        return false;
    }

    m_tasks[task.transferId] = task;
    *transferId = task.transferId;
    *resumeToken = task.resumeToken;
    *resumeOffset = 0;
    *finalFileName = task.finalFileName;
    *message = "upload initialized";
    return true;
}

bool UploadManager::resumeUpload(uint64_t ownerConnectionId,
                                 const std::string &transferId,
                                 const std::string &resumeToken,
                                 const std::string &fileName,
                                 int64_t expectedSize,
                                 int64_t *resumeOffset,
                                 std::string *finalFileName,
                                 std::string *message)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!resumeOffset || !finalFileName || !message)
        return false;
    *resumeOffset = 0;
    finalFileName->clear();

    std::map<std::string, UploadTask>::iterator it = m_tasks.find(transferId);
    if (it == m_tasks.end()) {
        *message = "task not found";
        return false;
    }

    UploadTask &task = it->second;
    if (!secureEquals(task.resumeToken, resumeToken)) {
        *message = "token invalid";
        return false;
    }
    if (task.originalFileName != fileName || task.expectedSize != expectedSize) {
        *message = "file metadata mismatch";
        return false;
    }
    if (task.status != "uploading") {
        *message = "task is not resumable";
        return false;
    }

    const std::time_t now = std::time(nullptr);
    if (task.activeOwnerConnectionId == 0 &&
            now >= task.updatedTime &&
            now - task.updatedTime > kTaskTtlSeconds) {
        const int64_t expiredSize = task.receivedSize;
        removeTaskFiles(task);
        m_tasks.erase(it);
        std::printf("expired upload removed transfer_id=%s bytes=%lld\n",
                    transferId.c_str(),
                    static_cast<long long>(expiredSize));
        *message = "task expired";
        return false;
    }
    if (task.activeOwnerConnectionId != 0 &&
            task.activeOwnerConnectionId != ownerConnectionId) {
        *message = "task already active";
        return false;
    }
    if (!reconcileTask(&task, true)) {
        *message = "temporary file is unavailable";
        return false;
    }

    task.activeOwnerConnectionId = ownerConnectionId;
    task.updatedTime = now;
    if (!persistTask(task)) {
        task.activeOwnerConnectionId = 0;
        *message = "failed to persist resumed task";
        return false;
    }

    *resumeOffset = task.receivedSize;
    *finalFileName = task.finalFileName;
    *message = "upload task resumed";
    return true;
}

bool UploadManager::writeBlock(uint64_t ownerConnectionId,
                               const std::string &transferId,
                               int64_t offset,
                               const char *data,
                               int32_t dataSize,
                               int64_t *receivedOffset,
                               std::string *message)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!receivedOffset || !message)
        return false;

    std::map<std::string, UploadTask>::iterator it = m_tasks.find(transferId);
    if (it == m_tasks.end()) {
        *receivedOffset = 0;
        *message = "task not found";
        return false;
    }

    UploadTask &task = it->second;
    *receivedOffset = task.receivedSize;
    if (task.status != "uploading") {
        *message = "task is not uploading";
        return false;
    }
    if (task.activeOwnerConnectionId != ownerConnectionId) {
        *message = "task is not bound to this connection";
        return false;
    }
    if (!data || dataSize <= 0 || dataSize > 64 * 1024) {
        *message = "invalid block size";
        return false;
    }
    if (offset < task.receivedSize) {
        if (offset >= 0 && offset + dataSize <= task.receivedSize) {
            *message = "block already confirmed";
            return true;
        }
        *message = "offset mismatch";
        return false;
    }
    if (offset > task.receivedSize) {
        *message = "offset mismatch";
        return false;
    }
    if (offset > task.expectedSize ||
            static_cast<int64_t>(dataSize) > task.expectedSize - offset) {
        *message = "block exceeds expected file size";
        return false;
    }

    const int64_t previousSize = task.receivedSize;
    const int partFd = open(task.tempPath.c_str(), O_WRONLY);
    if (partFd < 0) {
        *message = "failed to open temporary file";
        return false;
    }

    int32_t written = 0;
    bool writeOk = true;
    while (written < dataSize) {
        const ssize_t result = pwrite(partFd,
                                      data + written,
                                      static_cast<size_t>(dataSize - written),
                                      static_cast<off_t>(offset + written));
        if (result > 0) {
            written += static_cast<int32_t>(result);
            continue;
        }
        if (result < 0 && errno == EINTR)
            continue;
        writeOk = false;
        break;
    }
    if (writeOk && fsync(partFd) != 0)
        writeOk = false;
    close(partFd);

    struct stat st;
    const int64_t confirmedSize = offset + dataSize;
    if (!writeOk || stat(task.tempPath.c_str(), &st) != 0 ||
            !S_ISREG(st.st_mode) ||
            static_cast<int64_t>(st.st_size) != confirmedSize) {
        truncate(task.tempPath.c_str(), static_cast<off_t>(previousSize));
        *message = "failed to write upload block";
        return false;
    }

    task.receivedSize = confirmedSize;
    task.updatedTime = std::time(nullptr);
    if (!persistTask(task)) {
        truncate(task.tempPath.c_str(), static_cast<off_t>(previousSize));
        task.receivedSize = previousSize;
        *receivedOffset = previousSize;
        *message = "failed to persist upload progress";
        return false;
    }

    *receivedOffset = task.receivedSize;
    *message = "block accepted";
    return true;
}

bool UploadManager::finishUpload(uint64_t ownerConnectionId,
                                 const std::string &transferId,
                                 const std::string &fileName,
                                 int64_t fileSize,
                                 std::string *savedFileName,
                                 std::string *message)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!savedFileName || !message)
        return false;
    std::map<std::string, UploadTask>::iterator it = m_tasks.find(transferId);
    if (it == m_tasks.end()) {
        *message = "task not found";
        return false;
    }

    UploadTask &task = it->second;
    if (task.activeOwnerConnectionId != ownerConnectionId) {
        *message = "task is not bound to this connection";
        return false;
    }
    if (fileName != task.originalFileName || fileSize != task.expectedSize) {
        *message = "file metadata mismatch";
        return false;
    }

    struct stat st;
    if (task.receivedSize != task.expectedSize ||
            stat(task.tempPath.c_str(), &st) != 0 ||
            !S_ISREG(st.st_mode) ||
            static_cast<int64_t>(st.st_size) != task.expectedSize) {
        *message = "uploaded file size does not match";
        return false;
    }

    if (pathExists(m_mediaDir + "/" + task.finalFileName)) {
        task.finalFileName = makeAvailableFileName(task.originalFileName,
                                                   task.transferId);
        task.updatedTime = std::time(nullptr);
        if (!persistTask(task)) {
            *message = "failed to persist final file name";
            return false;
        }
    }

    const std::string finalPath = m_mediaDir + "/" + task.finalFileName;
    if (std::rename(task.tempPath.c_str(), finalPath.c_str()) != 0) {
        *message = "failed to move file to media directory";
        return false;
    }

    const std::string completedName = task.finalFileName;
    if (std::remove(taskPath(task.transferId).c_str()) != 0 && errno != ENOENT) {
        std::printf("warning: failed to remove completed task metadata transfer_id=%s errno=%d\n",
                    task.transferId.c_str(), errno);
    }
    m_tasks.erase(it);
    *savedFileName = completedName;
    *message = "upload completed";
    return true;
}

size_t UploadManager::unbindConnection(uint64_t ownerConnectionId)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    size_t count = 0;
    for (std::map<std::string, UploadTask>::iterator it = m_tasks.begin();
         it != m_tasks.end(); ++it) {
        if (it->second.activeOwnerConnectionId == ownerConnectionId) {
            it->second.activeOwnerConnectionId = 0;
            ++count;
        }
    }
    return count;
}

size_t UploadManager::cleanupExpiredTasks(std::time_t now)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return cleanupExpiredTasksUnlocked(now);
}

size_t UploadManager::cleanupExpiredTasksUnlocked(std::time_t now)
{
    size_t removed = 0;
    std::map<std::string, UploadTask>::iterator it = m_tasks.begin();
    while (it != m_tasks.end()) {
        UploadTask &task = it->second;
        if (task.status == "uploading" &&
                task.activeOwnerConnectionId == 0 &&
                now >= task.updatedTime &&
                now - task.updatedTime > kTaskTtlSeconds) {
            std::printf("expired upload removed transfer_id=%s bytes=%lld\n",
                        task.transferId.c_str(),
                        static_cast<long long>(task.receivedSize));
            removeTaskFiles(task);
            it = m_tasks.erase(it);
            ++removed;
        } else {
            ++it;
        }
    }
    return removed;
}

const std::string &UploadManager::tempDir() const
{
    return m_tempDir;
}

const std::string &UploadManager::tasksDir() const
{
    return m_tasksDir;
}

bool UploadManager::ensureDirectories()
{
    return ensureDirectory(m_tempDir, 0755) &&
            ensureDirectory(m_mediaDir, 0755) &&
            ensureDirectory(m_tasksDir, 0700);
}

bool UploadManager::ensureDirectory(const std::string &path, mode_t mode) const
{
    struct stat st;
    if (stat(path.c_str(), &st) == 0)
        return S_ISDIR(st.st_mode);
    return mkdir(path.c_str(), mode) == 0;
}

bool UploadManager::loadPersistedTasks()
{
    DIR *directory = opendir(m_tasksDir.c_str());
    if (!directory) {
        std::printf("failed to scan upload task directory: %s\n", m_tasksDir.c_str());
        return false;
    }

    size_t loaded = 0;
    dirent *entry = nullptr;
    while ((entry = readdir(directory)) != nullptr) {
        const std::string name(entry->d_name);
        if (!hasSuffix(name, ".task"))
            continue;

        UploadTask task;
        std::string error;
        const std::string path = m_tasksDir + "/" + name;
        if (!loadTaskFile(path, &task, &error)) {
            std::printf("ignored invalid upload task file=%s reason=%s\n",
                        path.c_str(), error.c_str());
            continue;
        }
        const std::string fileTransferId = name.substr(0, name.size() - 5);
        if (fileTransferId != task.transferId ||
                m_tasks.find(task.transferId) != m_tasks.end()) {
            std::printf("ignored upload task with mismatched or duplicate id file=%s\n",
                        path.c_str());
            continue;
        }
        if (!reconcileTask(&task, true)) {
            std::printf("ignored upload task without valid part transfer_id=%s\n",
                        task.transferId.c_str());
            continue;
        }

        task.activeOwnerConnectionId = 0;
        m_tasks[task.transferId] = task;
        ++loaded;
        std::printf("restored upload task transfer_id=%s offset=%lld file=%s\n",
                    task.transferId.c_str(),
                    static_cast<long long>(task.receivedSize),
                    task.originalFileName.c_str());
    }
    closedir(directory);
    std::printf("upload tasks restored count=%zu\n", loaded);
    return true;
}

bool UploadManager::loadTaskFile(const std::string &path,
                                 UploadTask *task,
                                 std::string *error) const
{
    if (!task || !error)
        return false;
    std::ifstream input(path.c_str(), std::ios::binary);
    if (!input) {
        *error = "cannot open metadata";
        return false;
    }

    std::map<std::string, std::string> fields;
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line[line.size() - 1] == '\r')
            line.erase(line.size() - 1);
        const std::string::size_type equal = line.find('=');
        if (equal == std::string::npos || equal == 0)
            continue;
        fields[line.substr(0, equal)] = line.substr(equal + 1);
    }

    int64_t created = 0;
    int64_t updated = 0;
    task->transferId = fields["transfer_id"];
    task->resumeToken = fields["resume_token"];
    task->originalFileName = fields["original_filename"];
    task->finalFileName = fields["final_filename"];
    task->tempPath = fields["temp_path"];
    task->status = fields["status"];
    if (!parseInt64(fields["expected_size"], &task->expectedSize) ||
            !parseInt64(fields["received_size"], &task->receivedSize) ||
            !parseInt64(fields["created_time"], &created) ||
            !parseInt64(fields["updated_time"], &updated)) {
        *error = "invalid numeric metadata";
        return false;
    }
    task->createdTime = static_cast<std::time_t>(created);
    task->updatedTime = static_cast<std::time_t>(updated);
    task->extension = extensionOf(task->originalFileName);
    task->activeOwnerConnectionId = 0;

    const std::string expectedTempPath = m_tempDir + "/" + task->transferId + ".part";
    if (!isSafeIdentifier(task->transferId) ||
            !isValidResumeToken(task->resumeToken) ||
            !isSafeFileName(task->originalFileName) ||
            !isSafeFileName(task->finalFileName) ||
            !isSupportedExtension(task->extension) ||
            task->tempPath != expectedTempPath ||
            task->expectedSize <= 0 ||
            task->receivedSize < 0 ||
            task->receivedSize > task->expectedSize ||
            task->createdTime <= 0 ||
            task->updatedTime <= 0 ||
            task->status != "uploading") {
        *error = "unsafe or incomplete metadata";
        return false;
    }
    return true;
}

bool UploadManager::persistTask(const UploadTask &task) const
{
    std::ostringstream content;
    content << "transfer_id=" << task.transferId << "\n"
            << "resume_token=" << task.resumeToken << "\n"
            << "original_filename=" << task.originalFileName << "\n"
            << "final_filename=" << task.finalFileName << "\n"
            << "expected_size=" << task.expectedSize << "\n"
            << "received_size=" << task.receivedSize << "\n"
            << "temp_path=" << task.tempPath << "\n"
            << "created_time=" << static_cast<long long>(task.createdTime) << "\n"
            << "updated_time=" << static_cast<long long>(task.updatedTime) << "\n"
            << "status=" << task.status << "\n";

    const std::string finalPath = taskPath(task.transferId);
    const std::string temporaryPath = finalPath + ".tmp";
    const std::string bytes = content.str();
    const int fd = open(temporaryPath.c_str(),
                        O_WRONLY | O_CREAT | O_TRUNC,
                        0600);
    if (fd < 0)
        return false;
    const bool written = writeAll(fd, bytes.data(), bytes.size());
    const bool synced = written && fsync(fd) == 0;
    close(fd);
    if (!synced || std::rename(temporaryPath.c_str(), finalPath.c_str()) != 0) {
        std::remove(temporaryPath.c_str());
        return false;
    }
    return true;
}

bool UploadManager::reconcileTask(UploadTask *task, bool persistCorrection) const
{
    if (!task)
        return false;
    struct stat st;
    if (stat(task->tempPath.c_str(), &st) != 0 || !S_ISREG(st.st_mode))
        return false;

    const int64_t actualSize = static_cast<int64_t>(st.st_size);
    const int64_t safeSize = std::min(task->expectedSize,
                                      std::min(task->receivedSize, actualSize));
    if (actualSize == task->receivedSize && actualSize <= task->expectedSize)
        return true;

    std::printf("corrected upload offset transfer_id=%s metadata=%lld actual=%lld safe=%lld\n",
                task->transferId.c_str(),
                static_cast<long long>(task->receivedSize),
                static_cast<long long>(actualSize),
                static_cast<long long>(safeSize));
    if (actualSize != safeSize &&
            truncate(task->tempPath.c_str(), static_cast<off_t>(safeSize)) != 0) {
        return false;
    }
    task->receivedSize = safeSize;
    task->updatedTime = std::time(nullptr);
    return !persistCorrection || persistTask(*task);
}

void UploadManager::scanOrphanPartFiles() const
{
    DIR *directory = opendir(m_tempDir.c_str());
    if (!directory)
        return;
    dirent *entry = nullptr;
    while ((entry = readdir(directory)) != nullptr) {
        const std::string name(entry->d_name);
        if (!hasSuffix(name, ".part"))
            continue;
        const std::string transferId = name.substr(0, name.size() - 5);
        if (m_tasks.find(transferId) == m_tasks.end()) {
            std::printf("orphan upload part detected path=%s/%s\n",
                        m_tempDir.c_str(), name.c_str());
        }
    }
    closedir(directory);
}

void UploadManager::removeTaskFiles(const UploadTask &task) const
{
    std::remove(task.tempPath.c_str());
    std::remove(taskPath(task.transferId).c_str());
    std::remove((taskPath(task.transferId) + ".tmp").c_str());
}

bool UploadManager::isSafeFileName(const std::string &fileName) const
{
    if (fileName.empty() || fileName == "." || fileName == ".." ||
            fileName.size() >= 256)
        return false;
    if (fileName.find('/') != std::string::npos ||
            fileName.find('\\') != std::string::npos ||
            fileName.find("..") != std::string::npos ||
            fileName.find('=') != std::string::npos) {
        return false;
    }
    for (std::string::const_iterator it = fileName.begin(); it != fileName.end(); ++it) {
        if (static_cast<unsigned char>(*it) < 0x20)
            return false;
    }
    return true;
}

bool UploadManager::isSafeIdentifier(const std::string &value) const
{
    if (value.empty() || value.size() >= 96)
        return false;
    for (std::string::const_iterator it = value.begin(); it != value.end(); ++it) {
        const unsigned char ch = static_cast<unsigned char>(*it);
        if (!std::isalnum(ch) && ch != '_' && ch != '-')
            return false;
    }
    return true;
}

bool UploadManager::isValidResumeToken(const std::string &value) const
{
    if (value.size() != 64)
        return false;
    for (std::string::const_iterator it = value.begin(); it != value.end(); ++it) {
        if (!std::isxdigit(static_cast<unsigned char>(*it)))
            return false;
    }
    return true;
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

std::string UploadManager::makeTransferId()
{
    while (true) {
        std::ostringstream value;
        value << std::hex << static_cast<unsigned long long>(std::time(nullptr))
              << "_" << static_cast<unsigned long>(getpid())
              << "_" << ++m_nextId
              << "_" << randomHex(12);
        const std::string candidate = value.str();
        if (m_tasks.find(candidate) == m_tasks.end() &&
                !pathExists(taskPath(candidate)) &&
                !pathExists(m_tempDir + "/" + candidate + ".part")) {
            return candidate;
        }
    }
}

std::string UploadManager::makeResumeToken() const
{
    return randomHex(32);
}

std::string UploadManager::randomHex(size_t byteCount) const
{
    std::vector<unsigned char> bytes(byteCount, 0);
    bool ready = false;
    std::ifstream randomFile("/dev/urandom", std::ios::binary);
    if (randomFile) {
        randomFile.read(reinterpret_cast<char *>(bytes.data()),
                        static_cast<std::streamsize>(bytes.size()));
        ready = randomFile.good() || randomFile.gcount() ==
                static_cast<std::streamsize>(bytes.size());
    }
    if (!ready) {
        std::random_device randomDevice;
        for (size_t index = 0; index < bytes.size(); ++index)
            bytes[index] = static_cast<unsigned char>(randomDevice());
    }

    std::ostringstream text;
    text << std::hex << std::setfill('0');
    for (size_t index = 0; index < bytes.size(); ++index)
        text << std::setw(2) << static_cast<unsigned>(bytes[index]);
    return text.str();
}

std::string UploadManager::makeAvailableFileName(
        const std::string &fileName,
        const std::string &exceptTransferId) const
{
    if (!pathExists(m_mediaDir + "/" + fileName) &&
            !finalNameReserved(fileName, exceptTransferId)) {
        return fileName;
    }

    const std::string::size_type dot = fileName.find_last_of('.');
    const std::string base = dot == std::string::npos ? fileName : fileName.substr(0, dot);
    const std::string suffix = dot == std::string::npos ? std::string() : fileName.substr(dot);
    for (unsigned int index = 1; ; ++index) {
        std::ostringstream candidate;
        candidate << base << "_" << index << suffix;
        if (!pathExists(m_mediaDir + "/" + candidate.str()) &&
                !finalNameReserved(candidate.str(), exceptTransferId)) {
            return candidate.str();
        }
    }
}

bool UploadManager::pathExists(const std::string &path) const
{
    struct stat st;
    return stat(path.c_str(), &st) == 0;
}

bool UploadManager::finalNameReserved(const std::string &fileName,
                                      const std::string &exceptTransferId) const
{
    for (std::map<std::string, UploadTask>::const_iterator it = m_tasks.begin();
         it != m_tasks.end(); ++it) {
        if (it->first != exceptTransferId &&
                it->second.status == "uploading" &&
                it->second.finalFileName == fileName) {
            return true;
        }
    }
    return false;
}

std::string UploadManager::taskPath(const std::string &transferId) const
{
    return m_tasksDir + "/" + transferId + ".task";
}

bool UploadManager::secureEquals(const std::string &left,
                                 const std::string &right) const
{
    if (left.size() != right.size())
        return false;
    unsigned char difference = 0;
    for (size_t index = 0; index < left.size(); ++index)
        difference |= static_cast<unsigned char>(left[index] ^ right[index]);
    return difference == 0;
}
