#include "MediaRepository.h"

#include "av_protocol.h"

#include <mysql/mysql.h>

#include <algorithm>
#include <cstring>
#include <memory>

namespace {

struct StatementCloser
{
    void operator()(MYSQL_STMT *statement) const
    { if (statement) mysql_stmt_close(statement); }
};
typedef std::unique_ptr<MYSQL_STMT, StatementCloser> StatementPtr;

StatementPtr prepare(MYSQL *connection, const char *sql, std::string *message)
{
    StatementPtr statement(mysql_stmt_init(connection));
    if (!statement || mysql_stmt_prepare(
                statement.get(), sql,
                static_cast<unsigned long>(std::strlen(sql))) != 0) {
        *message = "database temporarily unavailable";
        return StatementPtr();
    }
    return statement;
}

void bindString(MYSQL_BIND *binding, const std::string &value,
                unsigned long *length)
{
    std::memset(binding, 0, sizeof(*binding));
    *length = static_cast<unsigned long>(value.size());
    binding->buffer_type = MYSQL_TYPE_STRING;
    binding->buffer = const_cast<char *>(value.data());
    binding->buffer_length = *length;
    binding->length = length;
}

bool fetchMediaRows(MYSQL_STMT *statement,
                    std::vector<MediaRecord> *records,
                    std::string *message)
{
    unsigned long long mediaId = 0;
    unsigned long long ownerId = 0;
    long long fileSize = 0;
    char owner[64] = {0}; char original[256] = {0}; char stored[256] = {0};
    char path[512] = {0}; char extension[16] = {0}; char status[32] = {0};
    char created[32] = {0};
    unsigned long lengths[7] = {0};
    MYSQL_BIND output[10];
    std::memset(output, 0, sizeof(output));
    output[0].buffer_type = MYSQL_TYPE_LONGLONG; output[0].buffer = &mediaId; output[0].is_unsigned = 1;
    output[1].buffer_type = MYSQL_TYPE_LONGLONG; output[1].buffer = &ownerId; output[1].is_unsigned = 1;
    char *buffers[7] = {owner, original, stored, path, extension, status, created};
    unsigned long capacities[7] = {sizeof(owner), sizeof(original), sizeof(stored), sizeof(path), sizeof(extension), sizeof(status), sizeof(created)};
    for (int index = 0; index < 5; ++index) {
        output[index + 2].buffer_type = MYSQL_TYPE_STRING;
        output[index + 2].buffer = buffers[index];
        output[index + 2].buffer_length = capacities[index] - 1;
        output[index + 2].length = &lengths[index];
    }
    output[7].buffer_type = MYSQL_TYPE_LONGLONG; output[7].buffer = &fileSize;
    for (int index = 5; index < 7; ++index) {
        output[index + 3].buffer_type = MYSQL_TYPE_STRING;
        output[index + 3].buffer = buffers[index];
        output[index + 3].buffer_length = capacities[index] - 1;
        output[index + 3].length = &lengths[index];
    }
    if (mysql_stmt_bind_result(statement, output) != 0 ||
            mysql_stmt_store_result(statement) != 0) {
        *message = "database temporarily unavailable";
        return false;
    }

    while (true) {
        const int fetched = mysql_stmt_fetch(statement);
        if (fetched == MYSQL_NO_DATA)
            break;
        if (fetched != 0 && fetched != MYSQL_DATA_TRUNCATED) {
            *message = "database temporarily unavailable";
            return false;
        }
        bool truncated = false;
        for (int index = 0; index < 7; ++index) {
            if (lengths[index] >= capacities[index])
                truncated = true;
            else
                buffers[index][lengths[index]] = '\0';
        }
        if (truncated) {
            *message = "database media metadata is invalid";
            return false;
        }
        MediaRecord record;
        record.mediaId = static_cast<uint64_t>(mediaId);
        record.ownerUserId = static_cast<uint64_t>(ownerId);
        record.ownerName = owner;
        record.originalName = original;
        record.storedName = stored;
        record.storagePath = path;
        record.extension = extension;
        record.fileSize = static_cast<int64_t>(fileSize);
        record.status = status;
        record.createdAt = created;
        records->push_back(record);
    }
    return true;
}

} // namespace

MediaRecord::MediaRecord()
    : mediaId(0), ownerUserId(0), fileSize(0)
{
}

MediaRepository::MediaRepository(DatabaseConnectionPool &pool)
    : m_pool(pool)
{
}

bool MediaRepository::insertMedia(const MediaRecord &media,
                                  uint64_t *mediaId,
                                  std::string *message)
{
    std::string poolError;
    DatabaseConnectionPool::ConnectionLease lease = m_pool.acquire(&poolError);
    if (!lease) { *message = "database temporarily unavailable"; return false; }
    StatementPtr statement = prepare(
                lease.get(),
                "INSERT INTO media(owner_user_id,original_name,stored_name,storage_path,extension,file_size,status) VALUES(?,?,?,?,?,?,'published')",
                message);
    if (!statement)
        return false;

    MYSQL_BIND parameters[6];
    std::memset(parameters, 0, sizeof(parameters));
    unsigned long long ownerId = media.ownerUserId;
    long long size = media.fileSize;
    unsigned long lengths[4] = {0};
    parameters[0].buffer_type = MYSQL_TYPE_LONGLONG;
    parameters[0].buffer = &ownerId;
    parameters[0].is_unsigned = 1;
    bindString(&parameters[1], media.originalName, &lengths[0]);
    bindString(&parameters[2], media.storedName, &lengths[1]);
    bindString(&parameters[3], media.storagePath, &lengths[2]);
    bindString(&parameters[4], media.extension, &lengths[3]);
    parameters[5].buffer_type = MYSQL_TYPE_LONGLONG;
    parameters[5].buffer = &size;
    if (mysql_stmt_bind_param(statement.get(), parameters) != 0 ||
            mysql_stmt_execute(statement.get()) != 0) {
        *message = "database temporarily unavailable";
        return false;
    }
    *mediaId = static_cast<uint64_t>(mysql_stmt_insert_id(statement.get()));
    *message = "media metadata published";
    return true;
}

bool MediaRepository::listMedia(int scope,
                                uint64_t currentUserId,
                                int page,
                                int pageSize,
                                const std::string &keyword,
                                std::vector<MediaRecord> *records,
                                std::string *message)
{
    records->clear();
    if ((scope != AV_MEDIA_SCOPE_PUBLIC && scope != AV_MEDIA_SCOPE_MINE) ||
            page < 1 || pageSize < 1 || pageSize > 100 || keyword.size() >= AV_KEYWORD_SIZE) {
        *message = "invalid media list request";
        return false;
    }
    std::string poolError;
    DatabaseConnectionPool::ConnectionLease lease = m_pool.acquire(&poolError);
    if (!lease) { *message = "database temporarily unavailable"; return false; }
    const char *publicSql =
            "SELECT m.id,m.owner_user_id,u.username,m.original_name,m.stored_name,m.storage_path,m.extension,m.file_size,m.status,DATE_FORMAT(m.created_at,'%Y-%m-%d %H:%i:%s') FROM media m JOIN users u ON u.id=m.owner_user_id WHERE m.status='published' AND m.original_name LIKE ? ORDER BY m.created_at DESC,m.id DESC LIMIT ? OFFSET ?";
    const char *mineSql =
            "SELECT m.id,m.owner_user_id,u.username,m.original_name,m.stored_name,m.storage_path,m.extension,m.file_size,m.status,DATE_FORMAT(m.created_at,'%Y-%m-%d %H:%i:%s') FROM media m JOIN users u ON u.id=m.owner_user_id WHERE m.status='published' AND m.owner_user_id=? AND m.original_name LIKE ? ORDER BY m.created_at DESC,m.id DESC LIMIT ? OFFSET ?";
    StatementPtr statement = prepare(lease.get(),
                                     scope == AV_MEDIA_SCOPE_MINE ? mineSql : publicSql,
                                     message);
    if (!statement)
        return false;
    const std::string pattern = "%" + keyword + "%";
    unsigned long patternLength = 0;
    unsigned long long ownerId = currentUserId;
    unsigned long long limit = static_cast<unsigned long long>(pageSize);
    unsigned long long offset = static_cast<unsigned long long>(page - 1) * limit;
    MYSQL_BIND parameters[4];
    std::memset(parameters, 0, sizeof(parameters));
    int index = 0;
    if (scope == AV_MEDIA_SCOPE_MINE) {
        parameters[index].buffer_type = MYSQL_TYPE_LONGLONG;
        parameters[index].buffer = &ownerId;
        parameters[index].is_unsigned = 1;
        ++index;
    }
    bindString(&parameters[index++], pattern, &patternLength);
    parameters[index].buffer_type = MYSQL_TYPE_LONGLONG;
    parameters[index].buffer = &limit;
    parameters[index].is_unsigned = 1;
    ++index;
    parameters[index].buffer_type = MYSQL_TYPE_LONGLONG;
    parameters[index].buffer = &offset;
    parameters[index].is_unsigned = 1;
    if (mysql_stmt_bind_param(statement.get(), parameters) != 0 ||
            mysql_stmt_execute(statement.get()) != 0) {
        *message = "database temporarily unavailable";
        return false;
    }
    if (!fetchMediaRows(statement.get(), records, message))
        return false;
    *message = "media list ready";
    return true;
}

bool MediaRepository::findMediaById(uint64_t mediaId,
                                    MediaRecord *record,
                                    std::string *message)
{
    if (!record || mediaId == 0) { *message = "invalid media id"; return false; }
    std::string poolError;
    DatabaseConnectionPool::ConnectionLease lease = m_pool.acquire(&poolError);
    if (!lease) { *message = "database temporarily unavailable"; return false; }
    StatementPtr statement = prepare(
                lease.get(),
                "SELECT m.id,m.owner_user_id,u.username,m.original_name,m.stored_name,m.storage_path,m.extension,m.file_size,m.status,DATE_FORMAT(m.created_at,'%Y-%m-%d %H:%i:%s') FROM media m JOIN users u ON u.id=m.owner_user_id WHERE m.id=? AND m.status='published' LIMIT 1",
                message);
    if (!statement)
        return false;
    unsigned long long id = mediaId;
    MYSQL_BIND parameter;
    std::memset(&parameter, 0, sizeof(parameter));
    parameter.buffer_type = MYSQL_TYPE_LONGLONG;
    parameter.buffer = &id;
    parameter.is_unsigned = 1;
    if (mysql_stmt_bind_param(statement.get(), &parameter) != 0 ||
            mysql_stmt_execute(statement.get()) != 0) {
        *message = "database temporarily unavailable";
        return false;
    }
    std::vector<MediaRecord> records;
    if (!fetchMediaRows(statement.get(), &records, message))
        return false;
    if (records.empty()) { *message = "media not found"; return false; }
    *record = records.front();
    *message = "media found";
    return true;
}

