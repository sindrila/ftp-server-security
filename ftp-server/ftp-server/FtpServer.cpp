#include "FtpServer.h"


FtpServer::FtpServer()
{
    //srand(static_cast<ULONG>(time(nullptr)));

    this->threadPool = std::make_unique<BS::thread_pool_light>(16);

    WSADATA wsaData = { 0 };
    int status = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (status)
    {
        const std::string& message = "WSAStartup failed with status " + std::to_string(status);
        throw std::exception(message.c_str());
    }
}

FtpServer::~FtpServer()
{
    int status = closesocket(this->listenSocket);
    if (status == SOCKET_ERROR)
    {
        const std::string& message = "closesocket failed with status " + std::to_string(WSAGetLastError());
        std::cout << message.c_str() << std::endl;
    }

    status = WSACleanup();
    if (status)
    {
        const std::string& message = "WSACleanup failed with status " + std::to_string(WSAGetLastError());
        std::cout << message.c_str() << std::endl;
    }
}

VOID
FtpServer::Start()
{
    ADDRINFOA hints = { 0 };
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_PASSIVE;

    PADDRINFOA result = nullptr;
    int status = getaddrinfo(NULL, DEFAULT_PORT, &hints, &result);
    if (status)
    {
        std::cout << "getaddrinfo failed with status " << status << std::endl;
        return;
    }

    this->listenSocket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (this->listenSocket == INVALID_SOCKET)
    {
        std::cout << "socket failed with status " << WSAGetLastError() << std::endl;
        freeaddrinfo(result);
        return;
    }

    status = bind(this->listenSocket, result->ai_addr, (int)result->ai_addrlen);
    if (status == SOCKET_ERROR)
    {
        std::cout << "bind failed with status " << WSAGetLastError() << std::endl;
        freeaddrinfo(result);
        return;
    }

    freeaddrinfo(result);

    status = listen(this->listenSocket, SOMAXCONN);
    if (status == SOCKET_ERROR)
    {
        std::cout << "listen failed with status " << WSAGetLastError() << std::endl;
        return;
    }

    std::cout << "Server is ready and listening on port " << DEFAULT_PORT << std::endl;

    this->HandleConnections();
}

VOID
FtpServer::HandleConnections()
{
    while (true)
    {
        SOCKADDR_IN clientInfo = { 0 };
        int clientInfoSize = sizeof(clientInfo);
        SOCKET* clientSocket = new SOCKET(accept(this->listenSocket, reinterpret_cast<PSOCKADDR>(&clientInfo), &clientInfoSize));
        if (*clientSocket == INVALID_SOCKET)
        {
            std::cout << "accept failed with status " << WSAGetLastError() << std::endl;
            closesocket(*clientSocket);
            delete clientSocket;
        }
        else
        {
            this->threadPool->push_task([&, clientInfo]
                {
                    CHAR clientIP[INET_ADDRSTRLEN] = { 0 };
                    inet_ntop(AF_INET, &clientInfo.sin_addr, clientIP, INET_ADDRSTRLEN);
                    std::cout << "Client connected from IP: " << clientIP << std::endl;

                    CLIENT_CONTEXT clientContext = { .Socket = *clientSocket, .CurrentDir = R"(D:\facultate-repo)", .IPv4 = clientInfo.sin_addr };
                    this->HandleConnection(clientContext);

                    closesocket(*clientSocket);
                    delete clientSocket;
                });
        }
    }
}

VOID
FtpServer::HandleConnection(CLIENT_CONTEXT& ClientContext)
{
    this->SendString(ClientContext, "220 FTP Server Ready");

    int status = 0;
    do
    {
        char buffer[DEFAULT_BUFLEN] = { 0 };
        int bufferSize = sizeof(buffer);

        status = recv(ClientContext.Socket, buffer, bufferSize, 0);;
        std::cout << "Command received: " << buffer;
        if (status > 0)
        {
            std::string command(buffer, status);
            this->ProcessCommand(command, ClientContext);
        }
        else if (!status)
        {
            std::cout << "Connection closing..." << std::endl;
        }
        else
        {
            std::cout << "recv failed " << WSAGetLastError() << std::endl;
            return;
        }
    } while (status > 0);
}

bool FtpServer::SendString(const CLIENT_CONTEXT& ClientContext, const std::string& Message)
{
    return this->SendString(ClientContext.Socket, Message);
}

bool FtpServer::SendString(const SOCKET& Socket, const std::string& Message)
{
    std::string message = Message;
    if (!message.ends_with("\r\n"))
    {
        message += "\r\n";
    }
    return send(Socket, message.c_str(), static_cast<int>(message.size()), 0) != SOCKET_ERROR;
}

void FtpServer::ProcessCommand(const std::string& Command, CLIENT_CONTEXT& ClientContext)
{
    if (!Command.ends_with("\r\n"))
    {
        return;
    }

    const std::string& processedCommand = Command.substr(0, Command.size() - 2);
    size_t separatorPosition = processedCommand.find_first_of(" ");
    const std::string& command = processedCommand.substr(0, separatorPosition);
    const std::string& argument = processedCommand.substr((separatorPosition != std::string::npos ? separatorPosition + 1 : processedCommand.size()));

    if (!command.compare("USER"))
    {
        this->HandleUser(ClientContext, argument);
    }
    else if (!command.compare("PASS"))
    {
        this->HandlePass(ClientContext, argument);
    }
    else if (!command.compare("OPTS"))
    {
        this->HandleOpts(ClientContext, argument);
    }
    else if (!command.compare("PASV"))
    {
        this->HandlePasv(ClientContext);
    }
    else if (!command.compare("QUIT"))
    {
        this->HandleQuit(ClientContext);
    }
    else if (!command.compare("LIST"))
    {
        this->HandleList(ClientContext, argument);
    }
    else if (!command.compare("PORT"))
    {
        this->HandlePort(ClientContext, argument);
    }
    else
    {
        std::cout << "Unsupported command: " << command << std::endl;
    }
}

bool FtpServer::HandleUser(CLIENT_CONTEXT& ClientContext, const std::string& Argument)
{
    if (Argument.size() == 0)
    {
        return this->SendString(ClientContext, "501 Syntax error in parameters or arguments.");
    }

    ClientContext.Access = CLIENT_ACCESS::NotLoggedIn;

    CHAR message[MESSAGE_MAX_LENGTH] = { 0 };
    _snprintf_s(message, sizeof(message), _TRUNCATE, "331 User %s OK. Password required", Argument.c_str());
    strcpy_s(ClientContext.UserName, sizeof(ClientContext.UserName), Argument.c_str());
    return this->SendString(ClientContext, message);

}

bool FtpServer::HandlePass(CLIENT_CONTEXT& ClientContext, const std::string& Argument)
{
    if (Argument.size() == 0)
    {
        return this->SendString(ClientContext, "501 Pass command with syntax error.");
    }
    const std::string& username = ClientContext.UserName;
    if (username.size() == 0 || username.compare(HARDCODED_USER) || Argument.compare(HARDCODED_PASSWORD))
    {
        return this->SendString(ClientContext, "530 Invalid username or password");
    }

    ClientContext.Access = CLIENT_ACCESS::Full;

    return this->SendString(ClientContext, "230 User logged in.");
}

bool FtpServer::HandleOpts(CLIENT_CONTEXT& ClientContext, const std::string& Argument)
{
    if (Argument == "UTF8 ON")
    {
        return this->SendString(ClientContext, "200 UTF8 mode enabled");
    }
    else
    {
        return this->SendString(ClientContext, "501 Opts command with syntax error.");
    }
}

bool FtpServer::HandlePasv(CLIENT_CONTEXT& ClientContext)
{
    if (ClientContext.Access == CLIENT_ACCESS::NotLoggedIn)
    {
        return this->SendString(ClientContext, "530 Please login with USER and PASS.");
    }

    SOCKET passiveSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (passiveSocket == INVALID_SOCKET)
    {
        closesocket(passiveSocket);
        return this->SendString(ClientContext, "451 Requested action aborted. Local error in processing.");
    }

    SOCKADDR_IN serverAddr = { .sin_family = AF_INET, .sin_port = htons((rand() % 5000) + 60001), .sin_addr = 0UL };
    int status = bind(passiveSocket, reinterpret_cast<PSOCKADDR>(&serverAddr), sizeof(serverAddr));
    if (status == SOCKET_ERROR)
    {
        closesocket(passiveSocket);
        return this->SendString(ClientContext, "451 Requested action aborted. Local error in processing.");
    }

    status = listen(passiveSocket, SOMAXCONN);
    if (status == SOCKET_ERROR)
    {
        closesocket(passiveSocket);
        return this->SendString(ClientContext, "451 Requested action aborted. Local error in processing.");
    }

    ClientContext.DataIPv4 = ClientContext.IPv4;
    ClientContext.DataPort = serverAddr.sin_port;
    ClientContext.DataSocket = passiveSocket;
    ClientContext.DataSocketType = DATASOCKET_TYPE::Passive;

    CHAR message[MESSAGE_MAX_LENGTH] = { 0 };
    _snprintf_s(message, sizeof(message), _TRUNCATE,
        "227 Entering Passive Mode (%u,%u,%u,%u,%u,%u).",
        (ClientContext.DataIPv4.s_addr) & 0xFF,
        (ClientContext.DataIPv4.s_addr >> 8) & 0xFF,
        (ClientContext.DataIPv4.s_addr >> 16) & 0xFF,
        (ClientContext.DataIPv4.s_addr >> 24) & 0xFF,
        (ClientContext.DataPort) & 0xFF,
        (ClientContext.DataPort >> 8) & 0xFF);
    return this->SendString(ClientContext, message);
}

bool FtpServer::HandleQuit(CLIENT_CONTEXT& ClientContext)
{
    return this->SendString(ClientContext, "221 Quit.");
}

std::string FiletimeToTimestamp(FILETIME& Filetime)
{
    SYSTEMTIME systemTime = { 0 };
    FileTimeToSystemTime(&Filetime, &systemTime);
    CHAR buffer[24] = { 0 };
    _snprintf_s(buffer, 24, _TRUNCATE, "%04d-%02d-%02d %02d:%02d:%02d.%03d", systemTime.wYear, systemTime.wMonth, systemTime.wDay, systemTime.wHour, systemTime.wMinute, systemTime.wSecond, systemTime.wMilliseconds);
    buffer[24 - 1] = ANSI_NULL;
    return buffer;
}

bool FtpServer::HandleList(CLIENT_CONTEXT& ClientContext, const std::string& Argument)
{
    if (ClientContext.Access == CLIENT_ACCESS::NotLoggedIn)
    {
        return this->SendString(ClientContext, "530 Please login with user and pass.");
    }

    std::string listDir = std::string(ClientContext.CurrentDir);
    if (Argument.size() > 0 && !Argument.starts_with("-a") && !Argument.starts_with("-1"))
    {
        if (Argument.contains(".."))
        {
            return this->SendString(ClientContext, "501 Syntax error in parmeters or arguments");
        }
        listDir += "\\" + Argument;
    }

    WIN32_FIND_DATAA fileData = { 0 };
    HANDLE fileHandle = FindFirstFileA((listDir + "\\*").c_str(), &fileData);
    std::stringstream listStream;
    do
    {
        if (strcmp(fileData.cFileName, ".") && strcmp(fileData.cFileName, ".."))
        {
            listStream << ((fileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? "d" : "-")
                << "rw-r--r-- 1 owner group "
                << fileData.nFileSizeLow << " "
                << FiletimeToTimestamp(fileData.ftLastWriteTime) << " "
                << fileData.cFileName << "\r\n";
        }
    } while (FindNextFileA(fileHandle, &fileData));
    FindClose(fileHandle);

    this->SendString(ClientContext, "150 Opening data connection.");

    SOCKET dataSocket = { 0 };
    if (ClientContext.DataSocketType == DATASOCKET_TYPE::Passive)
    {
        dataSocket = accept(ClientContext.DataSocket, NULL, NULL);
        closesocket(ClientContext.DataSocket);
        ClientContext.DataSocket = dataSocket;
        if (dataSocket == INVALID_SOCKET)
        {
            closesocket(dataSocket);
            return this->SendString(ClientContext, "451 Requested action aborted. Local error in processing.");
        }
    }
    else if (ClientContext.DataSocketType == DATASOCKET_TYPE::Normal)
    {
        dataSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        SOCKADDR_IN clientAddr = { .sin_family = AF_INET, .sin_port = ClientContext.DataPort, .sin_addr = ClientContext.DataIPv4 };
        int status = connect(dataSocket, reinterpret_cast<PSOCKADDR>(&clientAddr), sizeof(clientAddr));
        if (status == SOCKET_ERROR)
        {
            closesocket(dataSocket);
            return this->SendString(ClientContext, "550 File or directory unavailable.");
        }
    }

    this->SendString(dataSocket, listStream.str());
    closesocket(dataSocket);
    return this->SendString(ClientContext, "226 Transfer complete.");
}

bool FtpServer::HandlePort(CLIENT_CONTEXT& ClientContext, const std::string& Argument)
{
    if (ClientContext.Access == CLIENT_ACCESS::NotLoggedIn)
    {
        return this->SendString(ClientContext, "530 Please login with USER and PASS.");
    }

    if (Argument.size() == 0)
    {
        return this->SendString(ClientContext, "501 Syntax error in parameters or arguments.");
    }

    int c;
    PCHAR p = const_cast<PCHAR>(Argument.c_str());
    ULONG dataAddr[4] = { 0UL };
    ULONG dataPort[2] = { 0UL };
    for (c = 0; c < 4; ++c)
    {
        dataAddr[c] = strtoul(p, &p, 10);
        if (*p != ',' && *p != 0)
        {
            break;
        }
        if (*p == 0)
        {
            return this->SendString(ClientContext, "501 Syntax error in parameters or arguments.");
        }
        ++p;
    }

    for (c = 0; c < 2; ++c)
    {
        dataPort[c] = strtoul(p, &p, 10);
        if (*p != ',' && *p != 0)
        {
            break;
        }
        if (*p == 0)
        {
            break;
        }
        ++p;
    }

    IN_ADDR dataIPv4 = { 0 };
    dataIPv4.S_un.S_un_b.s_b1 = static_cast<BYTE>(dataAddr[0]);
    dataIPv4.S_un.S_un_b.s_b2 = static_cast<BYTE>(dataAddr[1]);
    dataIPv4.S_un.S_un_b.s_b3 = static_cast<BYTE>(dataAddr[2]);
    dataIPv4.S_un.S_un_b.s_b4 = static_cast<BYTE>(dataAddr[3]);
    if (dataIPv4.S_un.S_addr != ClientContext.IPv4.S_un.S_addr)
    {
        return this->SendString(ClientContext, "501 Syntax error in parameters or arguments.");
    }

    ClientContext.DataIPv4.S_un.S_addr = dataIPv4.S_un.S_addr;
    ClientContext.DataPort = static_cast<USHORT>((dataPort[1] << 8) + dataPort[0]);
    ClientContext.DataSocketType = DATASOCKET_TYPE::Normal;

    return this->SendString(ClientContext, "200 Transfer complete.");
}



