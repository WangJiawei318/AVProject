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

static const int AV_NAME_SIZE = 32;
static const int AV_TEXT_SIZE = 128;

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

#pragma pack(pop)

#endif // AV_PROTOCOL_H
