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

static const int AV_NAME_SIZE = 32;
static const int AV_TEXT_SIZE = 128;
static const int AV_FILE_NAME_SIZE = 256;
static const int AV_EXTENSION_SIZE = 16;
static const int AV_UPLOAD_ID_SIZE = 64;
static const int AV_UPLOAD_BLOCK_SIZE = 64 * 1024;

typedef int32_t PackType;

#pragma pack(push, 1)

struct STRU_PING_RQ
{
    STRU_PING_RQ()
        : type(DEF_PACK_PING_RQ)
    {
        memset(message, 0, sizeof(message));
        strcpy(message, "ping from AVClient");
    }

    PackType type;
    char message[AV_TEXT_SIZE];
};

struct STRU_PING_RS
{
    STRU_PING_RS()
        : type(DEF_PACK_PING_RS)
    {
        memset(message, 0, sizeof(message));
        strcpy(message, "pong from AVServer");
    }

    PackType type;
    char message[AV_TEXT_SIZE];
};

struct STRU_LOGIN_RQ
{
    STRU_LOGIN_RQ()
        : type(DEF_PACK_LOGIN_RQ)
    {
        memset(username, 0, sizeof(username));
        memset(password, 0, sizeof(password));
    }

    PackType type;
    char username[AV_NAME_SIZE];
    char password[AV_NAME_SIZE];
};

struct STRU_LOGIN_RS
{
    STRU_LOGIN_RS()
        : type(DEF_PACK_LOGIN_RS),
          result(1)
    {
        memset(message, 0, sizeof(message));
        strcpy(message, "login accepted for protocol test");
    }

    PackType type;
    int32_t result;
    char message[AV_TEXT_SIZE];
};

struct STRU_MEDIA_LIST_RQ
{
    STRU_MEDIA_LIST_RQ()
        : type(DEF_PACK_MEDIA_LIST_RQ)
    {
    }

    PackType type;
};

struct STRU_MEDIA_LIST_RS_HEADER
{
    STRU_MEDIA_LIST_RS_HEADER()
        : type(DEF_PACK_MEDIA_LIST_RS),
          payloadSize(0)
    {
    }

    PackType type;
    int32_t payloadSize;
};

struct STRU_UPLOAD_INIT_RQ
{
    STRU_UPLOAD_INIT_RQ()
        : type(DEF_PACK_UPLOAD_INIT_RQ),
          fileSize(0)
    {
        memset(fileName, 0, sizeof(fileName));
        memset(extension, 0, sizeof(extension));
    }

    PackType type;
    int64_t fileSize;
    char fileName[AV_FILE_NAME_SIZE];
    char extension[AV_EXTENSION_SIZE];
};

struct STRU_UPLOAD_INIT_RS
{
    STRU_UPLOAD_INIT_RS()
        : type(DEF_PACK_UPLOAD_INIT_RS),
          result(0)
    {
        memset(uploadId, 0, sizeof(uploadId));
        memset(message, 0, sizeof(message));
    }

    PackType type;
    int32_t result;
    char uploadId[AV_UPLOAD_ID_SIZE];
    char message[AV_TEXT_SIZE];
};

struct STRU_UPLOAD_BLOCK_RQ_HEADER
{
    STRU_UPLOAD_BLOCK_RQ_HEADER()
        : type(DEF_PACK_UPLOAD_BLOCK_RQ),
          offset(0),
          dataSize(0)
    {
        memset(uploadId, 0, sizeof(uploadId));
    }

    PackType type;
    char uploadId[AV_UPLOAD_ID_SIZE];
    int64_t offset;
    int32_t dataSize;
};

struct STRU_UPLOAD_BLOCK_RS
{
    STRU_UPLOAD_BLOCK_RS()
        : type(DEF_PACK_UPLOAD_BLOCK_RS),
          result(0),
          receivedOffset(0)
    {
        memset(uploadId, 0, sizeof(uploadId));
        memset(message, 0, sizeof(message));
    }

    PackType type;
    int32_t result;
    char uploadId[AV_UPLOAD_ID_SIZE];
    int64_t receivedOffset;
    char message[AV_TEXT_SIZE];
};

struct STRU_UPLOAD_FINISH_RQ
{
    STRU_UPLOAD_FINISH_RQ()
        : type(DEF_PACK_UPLOAD_FINISH_RQ),
          fileSize(0)
    {
        memset(uploadId, 0, sizeof(uploadId));
        memset(fileName, 0, sizeof(fileName));
    }

    PackType type;
    char uploadId[AV_UPLOAD_ID_SIZE];
    char fileName[AV_FILE_NAME_SIZE];
    int64_t fileSize;
};

struct STRU_UPLOAD_FINISH_RS
{
    STRU_UPLOAD_FINISH_RS()
        : type(DEF_PACK_UPLOAD_FINISH_RS),
          result(0)
    {
        memset(fileName, 0, sizeof(fileName));
        memset(message, 0, sizeof(message));
    }

    PackType type;
    int32_t result;
    char fileName[AV_FILE_NAME_SIZE];
    char message[AV_TEXT_SIZE];
};

#pragma pack(pop)

#endif // AV_PROTOCOL_H
