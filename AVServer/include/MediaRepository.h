#ifndef MEDIAREPOSITORY_H
#define MEDIAREPOSITORY_H

#include <stdint.h>
#include <string>
#include <vector>

#include "DatabaseConnectionPool.h"

struct MediaRecord
{
    MediaRecord();

    uint64_t mediaId;
    uint64_t ownerUserId;
    std::string ownerName;
    std::string originalName;
    std::string storedName;
    std::string storagePath;
    std::string extension;
    int64_t fileSize;
    std::string status;
    std::string createdAt;
};

class MediaRepository
{
public:
    explicit MediaRepository(DatabaseConnectionPool &pool);

    bool insertMedia(const MediaRecord &media,
                     uint64_t *mediaId,
                     std::string *message);
    bool listMedia(int scope,
                   uint64_t currentUserId,
                   int page,
                   int pageSize,
                   const std::string &keyword,
                   std::vector<MediaRecord> *records,
                   std::string *message);
    bool findMediaById(uint64_t mediaId,
                       MediaRecord *record,
                       std::string *message);

private:
    DatabaseConnectionPool &m_pool;
};

#endif // MEDIAREPOSITORY_H

