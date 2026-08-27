#ifndef AV_PROTOCOL_H
#define AV_PROTOCOL_H

#include <stdint.h>
#include <string.h>

static const int32_t DEF_PACK_BASE = 20000;
static const int32_t DEF_PACK_PING_RQ = DEF_PACK_BASE + 1;
static const int32_t DEF_PACK_PING_RS = DEF_PACK_BASE + 2;
static const int32_t DEF_PACK_LOGIN_RQ = DEF_PACK_BASE + 3;
static const int32_t DEF_PACK_LOGIN_RS = DEF_PACK_BASE + 4;
static const int32_t DEF_PACK_MEDIA_LIST_RQ = DEF_PACK_BASE + 5;
static const int32_t DEF_PACK_MEDIA_LIST_RS = DEF_PACK_BASE + 6;
static const int32_t DEF_PACK_UPLOAD_INIT_RQ = DEF_PACK_BASE + 7;
static const int32_t DEF_PACK_UPLOAD_INIT_RS = DEF_PACK_BASE + 8;
static const int32_t DEF_PACK_UPLOAD_BLOCK_RQ = DEF_PACK_BASE + 9;
static const int32_t DEF_PACK_UPLOAD_BLOCK_RS = DEF_PACK_BASE + 10;
static const int32_t DEF_PACK_UPLOAD_FINISH_RQ = DEF_PACK_BASE + 11;
static const int32_t DEF_PACK_UPLOAD_FINISH_RS = DEF_PACK_BASE + 12;
static const int32_t DEF_PACK_DOWNLOAD_INIT_RQ = DEF_PACK_BASE + 13;
static const int32_t DEF_PACK_DOWNLOAD_INIT_RS = DEF_PACK_BASE + 14;
static const int32_t DEF_PACK_DOWNLOAD_BLOCK_RQ = DEF_PACK_BASE + 15;
static const int32_t DEF_PACK_DOWNLOAD_BLOCK_RS = DEF_PACK_BASE + 16;
static const int32_t DEF_PACK_DOWNLOAD_FINISH_RQ = DEF_PACK_BASE + 17;
static const int32_t DEF_PACK_DOWNLOAD_FINISH_RS = DEF_PACK_BASE + 18;
static const int32_t DEF_PACK_UPLOAD_RESUME_RQ = DEF_PACK_BASE + 19;
static const int32_t DEF_PACK_UPLOAD_RESUME_RS = DEF_PACK_BASE + 20;
static const int32_t DEF_PACK_REGISTER_RQ = DEF_PACK_BASE + 21;
static const int32_t DEF_PACK_REGISTER_RS = DEF_PACK_BASE + 22;

static const int AV_NAME_SIZE = 33;
static const int AV_PASSWORD_SIZE = 65;
static const int AV_TEXT_SIZE = 128;
static const int AV_FILE_NAME_SIZE = 256;
static const int AV_EXTENSION_SIZE = 16;
static const int AV_TRANSFER_ID_SIZE = 96;
static const int AV_RESUME_TOKEN_SIZE = 128;
static const int AV_KEYWORD_SIZE = 128;
static const int AV_UPLOAD_BLOCK_SIZE = 64 * 1024;

typedef int32_t PackType;

enum AVErrorCode
{
    AV_ERROR_SUCCESS = 0,
    AV_ERROR_USERNAME_EXISTS = 1,
    AV_ERROR_INVALID_USERNAME_OR_PASSWORD = 2,
    AV_ERROR_USER_DISABLED = 3,
    AV_ERROR_AUTH_REQUIRED = 4,
    AV_ERROR_DATABASE_UNAVAILABLE = 5,
    AV_ERROR_SERVER_BUSY = 6,
    AV_ERROR_INVALID_REQUEST = 7,
    AV_ERROR_PERMISSION_DENIED = 8,
    AV_ERROR_INTERNAL = 9
};

enum AVMediaScope
{
    AV_MEDIA_SCOPE_PUBLIC = 1,
    AV_MEDIA_SCOPE_MINE = 2
};

#pragma pack(push, 1)

struct STRU_PING_RQ
{
    STRU_PING_RQ() : type(DEF_PACK_PING_RQ)
    { memset(message, 0, sizeof(message)); strcpy(message, "ping from AVClient"); }
    PackType type;
    char message[AV_TEXT_SIZE];
};

struct STRU_PING_RS
{
    STRU_PING_RS() : type(DEF_PACK_PING_RS)
    { memset(message, 0, sizeof(message)); strcpy(message, "pong from AVServer"); }
    PackType type;
    char message[AV_TEXT_SIZE];
};

struct STRU_REGISTER_RQ
{
    STRU_REGISTER_RQ() : type(DEF_PACK_REGISTER_RQ)
    { memset(username, 0, sizeof(username)); memset(password, 0, sizeof(password)); }
    PackType type;
    char username[AV_NAME_SIZE];
    char password[AV_PASSWORD_SIZE];
};

struct STRU_REGISTER_RS
{
    STRU_REGISTER_RS() : type(DEF_PACK_REGISTER_RS), result(0),
        errorCode(AV_ERROR_INVALID_REQUEST), userId(0)
    { memset(message, 0, sizeof(message)); }
    PackType type;
    int32_t result;
    int32_t errorCode;
    uint64_t userId;
    char message[AV_TEXT_SIZE];
};

struct STRU_LOGIN_RQ
{
    STRU_LOGIN_RQ() : type(DEF_PACK_LOGIN_RQ)
    { memset(username, 0, sizeof(username)); memset(password, 0, sizeof(password)); }
    PackType type;
    char username[AV_NAME_SIZE];
    char password[AV_PASSWORD_SIZE];
};

struct STRU_LOGIN_RS
{
    STRU_LOGIN_RS() : type(DEF_PACK_LOGIN_RS), result(0),
        errorCode(AV_ERROR_INVALID_USERNAME_OR_PASSWORD), userId(0)
    { memset(username, 0, sizeof(username)); memset(message, 0, sizeof(message)); }
    PackType type;
    int32_t result;
    int32_t errorCode;
    uint64_t userId;
    char username[AV_NAME_SIZE];
    char message[AV_TEXT_SIZE];
};

struct STRU_MEDIA_LIST_RQ
{
    STRU_MEDIA_LIST_RQ() : type(DEF_PACK_MEDIA_LIST_RQ),
        scope(AV_MEDIA_SCOPE_PUBLIC), page(1), pageSize(100)
    { memset(keyword, 0, sizeof(keyword)); }
    PackType type;
    int32_t scope;
    int32_t page;
    int32_t pageSize;
    char keyword[AV_KEYWORD_SIZE];
};

struct STRU_MEDIA_LIST_RS_HEADER
{
    STRU_MEDIA_LIST_RS_HEADER() : type(DEF_PACK_MEDIA_LIST_RS), result(0),
        errorCode(AV_ERROR_INVALID_REQUEST), payloadSize(0)
    { memset(message, 0, sizeof(message)); }
    PackType type;
    int32_t result;
    int32_t errorCode;
    int32_t payloadSize;
    char message[AV_TEXT_SIZE];
};

struct STRU_UPLOAD_INIT_RQ
{
    STRU_UPLOAD_INIT_RQ() : type(DEF_PACK_UPLOAD_INIT_RQ), fileSize(0)
    { memset(fileName, 0, sizeof(fileName)); memset(extension, 0, sizeof(extension)); }
    PackType type;
    int64_t fileSize;
    char fileName[AV_FILE_NAME_SIZE];
    char extension[AV_EXTENSION_SIZE];
};

struct STRU_UPLOAD_INIT_RS
{
    STRU_UPLOAD_INIT_RS() : type(DEF_PACK_UPLOAD_INIT_RS), result(0),
        errorCode(AV_ERROR_INVALID_REQUEST), resumeOffset(0)
    { memset(transferId, 0, sizeof(transferId)); memset(resumeToken, 0, sizeof(resumeToken));
      memset(finalFileName, 0, sizeof(finalFileName)); memset(message, 0, sizeof(message)); }
    PackType type;
    int32_t result;
    int32_t errorCode;
    char transferId[AV_TRANSFER_ID_SIZE];
    char resumeToken[AV_RESUME_TOKEN_SIZE];
    int64_t resumeOffset;
    char finalFileName[AV_FILE_NAME_SIZE];
    char message[AV_TEXT_SIZE];
};

struct STRU_UPLOAD_RESUME_RQ
{
    STRU_UPLOAD_RESUME_RQ() : type(DEF_PACK_UPLOAD_RESUME_RQ), expectedSize(0)
    { memset(transferId, 0, sizeof(transferId)); memset(resumeToken, 0, sizeof(resumeToken));
      memset(fileName, 0, sizeof(fileName)); }
    PackType type;
    char transferId[AV_TRANSFER_ID_SIZE];
    char resumeToken[AV_RESUME_TOKEN_SIZE];
    char fileName[AV_FILE_NAME_SIZE];
    int64_t expectedSize;
};

struct STRU_UPLOAD_RESUME_RS
{
    STRU_UPLOAD_RESUME_RS() : type(DEF_PACK_UPLOAD_RESUME_RS), result(0),
        errorCode(AV_ERROR_INVALID_REQUEST), resumeOffset(0)
    { memset(transferId, 0, sizeof(transferId)); memset(finalFileName, 0, sizeof(finalFileName));
      memset(message, 0, sizeof(message)); }
    PackType type;
    int32_t result;
    int32_t errorCode;
    char transferId[AV_TRANSFER_ID_SIZE];
    int64_t resumeOffset;
    char finalFileName[AV_FILE_NAME_SIZE];
    char message[AV_TEXT_SIZE];
};

struct STRU_UPLOAD_BLOCK_RQ_HEADER
{
    STRU_UPLOAD_BLOCK_RQ_HEADER() : type(DEF_PACK_UPLOAD_BLOCK_RQ), offset(0), dataSize(0)
    { memset(transferId, 0, sizeof(transferId)); }
    PackType type;
    char transferId[AV_TRANSFER_ID_SIZE];
    int64_t offset;
    int32_t dataSize;
};

struct STRU_UPLOAD_BLOCK_RS
{
    STRU_UPLOAD_BLOCK_RS() : type(DEF_PACK_UPLOAD_BLOCK_RS), result(0),
        errorCode(AV_ERROR_INVALID_REQUEST), receivedOffset(0)
    { memset(transferId, 0, sizeof(transferId)); memset(message, 0, sizeof(message)); }
    PackType type;
    int32_t result;
    int32_t errorCode;
    char transferId[AV_TRANSFER_ID_SIZE];
    int64_t receivedOffset;
    char message[AV_TEXT_SIZE];
};

struct STRU_UPLOAD_FINISH_RQ
{
    STRU_UPLOAD_FINISH_RQ() : type(DEF_PACK_UPLOAD_FINISH_RQ), fileSize(0)
    { memset(transferId, 0, sizeof(transferId)); memset(fileName, 0, sizeof(fileName)); }
    PackType type;
    char transferId[AV_TRANSFER_ID_SIZE];
    char fileName[AV_FILE_NAME_SIZE];
    int64_t fileSize;
};

struct STRU_UPLOAD_FINISH_RS
{
    STRU_UPLOAD_FINISH_RS() : type(DEF_PACK_UPLOAD_FINISH_RS), result(0),
        errorCode(AV_ERROR_INVALID_REQUEST), mediaId(0)
    { memset(fileName, 0, sizeof(fileName)); memset(message, 0, sizeof(message)); }
    PackType type;
    int32_t result;
    int32_t errorCode;
    uint64_t mediaId;
    char fileName[AV_FILE_NAME_SIZE];
    char message[AV_TEXT_SIZE];
};

struct STRU_DOWNLOAD_INIT_RQ
{
    STRU_DOWNLOAD_INIT_RQ() : type(DEF_PACK_DOWNLOAD_INIT_RQ), mediaId(0),
        resumeOffset(0), expectedFileSize(0), expectedModifiedTime(0) {}
    PackType type;
    uint64_t mediaId;
    int64_t resumeOffset;
    int64_t expectedFileSize;
    int64_t expectedModifiedTime;
};

struct STRU_DOWNLOAD_INIT_RS
{
    STRU_DOWNLOAD_INIT_RS() : type(DEF_PACK_DOWNLOAD_INIT_RS), result(0),
        errorCode(AV_ERROR_INVALID_REQUEST), mediaId(0), fileSize(0),
        modifiedTime(0), acceptedOffset(0)
    { memset(fileName, 0, sizeof(fileName)); memset(message, 0, sizeof(message)); }
    PackType type;
    int32_t result;
    int32_t errorCode;
    uint64_t mediaId;
    char fileName[AV_FILE_NAME_SIZE];
    int64_t fileSize;
    int64_t modifiedTime;
    int64_t acceptedOffset;
    char message[AV_TEXT_SIZE];
};

struct STRU_DOWNLOAD_BLOCK_RQ
{
    STRU_DOWNLOAD_BLOCK_RQ() : type(DEF_PACK_DOWNLOAD_BLOCK_RQ), mediaId(0),
        offset(0), requestSize(0) {}
    PackType type;
    uint64_t mediaId;
    int64_t offset;
    int32_t requestSize;
};

struct STRU_DOWNLOAD_BLOCK_RS_HEADER
{
    STRU_DOWNLOAD_BLOCK_RS_HEADER() : type(DEF_PACK_DOWNLOAD_BLOCK_RS), result(0),
        errorCode(AV_ERROR_INVALID_REQUEST), mediaId(0), offset(0), dataSize(0)
    { memset(fileName, 0, sizeof(fileName)); memset(message, 0, sizeof(message)); }
    PackType type;
    int32_t result;
    int32_t errorCode;
    uint64_t mediaId;
    char fileName[AV_FILE_NAME_SIZE];
    int64_t offset;
    int32_t dataSize;
    char message[AV_TEXT_SIZE];
};

struct STRU_DOWNLOAD_FINISH_RQ
{
    STRU_DOWNLOAD_FINISH_RQ() : type(DEF_PACK_DOWNLOAD_FINISH_RQ), mediaId(0), fileSize(0) {}
    PackType type;
    uint64_t mediaId;
    int64_t fileSize;
};

struct STRU_DOWNLOAD_FINISH_RS
{
    STRU_DOWNLOAD_FINISH_RS() : type(DEF_PACK_DOWNLOAD_FINISH_RS), result(0),
        errorCode(AV_ERROR_INVALID_REQUEST), mediaId(0)
    { memset(fileName, 0, sizeof(fileName)); memset(message, 0, sizeof(message)); }
    PackType type;
    int32_t result;
    int32_t errorCode;
    uint64_t mediaId;
    char fileName[AV_FILE_NAME_SIZE];
    char message[AV_TEXT_SIZE];
};

#pragma pack(pop)

#endif // AV_PROTOCOL_H
