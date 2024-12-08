#pragma once
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <iostream>
#include <string>
#include <sstream>
#include <fstream>
#include "BS_thread_pool_light.hpp"

#define DEFAULT_BUFLEN  512
#define DEFAULT_PORT    "21"
#define USERNAME_MAX_LENGTH         25
#define PASSWORD_MAX_LENGTH         32
#define MESSAGE_MAX_LENGTH          256
#define HARDCODED_USER              "user"
#define HARDCODED_PASSWORD          "pass"

typedef enum class _CLIENT_ACCESS : BYTE
{
    Unknown = 0,
    NotLoggedIn = 1,
    ReadOnly = 2,
    CreateNew = 3,
    Full = 4,

    MaxClientAccess
} CLIENT_ACCESS, * PCLIENT_ACCESS;

typedef enum class _DATASOCKET_TYPE : BYTE
{
    Unknown = 0,
    Normal = 1,
    Passive = 2,

    MaxDataSockektType
} DATASOCKET_TYPE, * PDATASOCKET_TYPE;

typedef struct _CLIENT_CONTEXT
{
    SOCKET          Socket = { 0 };
    CHAR            UserName[USERNAME_MAX_LENGTH] = { 0 };
    CHAR            CurrentDir[MAX_PATH] = { 0 };
    CLIENT_ACCESS   Access = CLIENT_ACCESS::NotLoggedIn;
    IN_ADDR         IPv4 = { 0 };
    IN_ADDR         DataIPv4 = { 0 };
    USHORT          DataPort = 0UL;
    SOCKET          DataSocket = { 0 };
    DATASOCKET_TYPE DataSocketType = DATASOCKET_TYPE::Unknown;
} CLIENT_CONTEXT, * PCLIENT_CONTEXT;

class FtpServer
{
    std::unique_ptr<BS::thread_pool_light> threadPool;
    SOCKET listenSocket = { 0 };

public:
    FtpServer();
    ~FtpServer();

    FtpServer(_In_ const FtpServer& Other) = delete;
    FtpServer& operator=(_In_ const FtpServer& Other) = delete;

    FtpServer(_Inout_ FtpServer&& Other) = delete;
    FtpServer& operator=(_In_ FtpServer&& Other) = delete;

    VOID Start();

private:
    VOID HandleConnections();

    VOID HandleConnection(CLIENT_CONTEXT& ClientContext);

    bool SendString(const CLIENT_CONTEXT& ClientSocket, const std::string& Message);
    bool SendString(const SOCKET& Socket, const std::string& Message);

    void ProcessCommand(const std::string& Command, CLIENT_CONTEXT& ClientContext);

    bool HandleUser(CLIENT_CONTEXT& ClientContext, const std::string& Argument);
    bool HandleOpts(CLIENT_CONTEXT& ClientContext, const std::string& Argument);
    bool HandlePass(CLIENT_CONTEXT& ClientContext, const std::string& Argument);
    bool HandlePasv(CLIENT_CONTEXT& ClientContext);
    bool HandleQuit(CLIENT_CONTEXT& ClientContext);
    bool HandleList(CLIENT_CONTEXT& ClientContext, const std::string& Argument);
    bool HandlePort(CLIENT_CONTEXT& ClientContext, const std::string& Argument);
    bool HandleRetr(CLIENT_CONTEXT& ClientContext, const std::string& Argument);
    bool HandleType(CLIENT_CONTEXT& ClientContext, const std::string& Argument);
    bool HandleStor(CLIENT_CONTEXT& ClientContext, const std::string& Argument);
    bool HandleNlst(CLIENT_CONTEXT& ClientContext, const std::string& Argument);
};

