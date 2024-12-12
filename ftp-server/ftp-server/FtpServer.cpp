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

                    CLIENT_CONTEXT clientContext = { .Socket = *clientSocket, .CurrentDir = R"(C:\Users\Alex)", .IPv4 = clientInfo.sin_addr };
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
    else if (!command.compare("RETR"))
    {
        this->HandleRetr(ClientContext, argument);
    }
    else if (!command.compare("TYPE"))
    {
        this->HandleType(ClientContext, argument);
    }
    else if (!command.compare("STOR"))
    {
        this->HandleStor(ClientContext, argument);
    }
    else if (!command.compare("NLST"))
    {
        this->HandleNlst(ClientContext, argument);
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
//std::string GetLocalIPv4()
//{
//    char buffer[INET_ADDRSTRLEN] = { 0 };
//    PIP_ADAPTER_ADDRESSES adapterAddresses = nullptr, adapter = nullptr;
//    ULONG outBufLen = 0;
//
//    // Initial call to determine required buffer size
//    if (GetAdaptersAddresses(AF_INET, 0, nullptr, adapterAddresses, &outBufLen) == ERROR_BUFFER_OVERFLOW)
//    {
//        adapterAddresses = (PIP_ADAPTER_ADDRESSES)malloc(outBufLen);
//    }
//    else
//    {
//        return "0.0.0.0"; // Default on failure
//    }
//
//    if (GetAdaptersAddresses(AF_INET, 0, nullptr, adapterAddresses, &outBufLen) == NO_ERROR)
//    {
//        for (adapter = adapterAddresses; adapter != nullptr; adapter = adapter->Next)
//        {
//            if (adapter->OperStatus == IfOperStatusUp && adapter->FirstUnicastAddress != nullptr)
//            {
//                sockaddr_in* ipv4Addr = reinterpret_cast<sockaddr_in*>(adapter->FirstUnicastAddress->Address.lpSockaddr);
//                if (inet_ntop(AF_INET, &ipv4Addr->sin_addr, buffer, INET_ADDRSTRLEN))
//                {
//                    free(adapterAddresses);
//                    return std::string(buffer);
//                }
//            }
//        }
//    }
//
//    free(adapterAddresses);
//    return "0.0.0.0"; // Default on failure

struct IPv4 {
    unsigned char b1, b2, b3, b4;
};

bool getMyIP(IPv4& myIP)
{
    char szBuffer[1024];

#ifdef WIN32
    WSADATA wsaData;
    WORD wVersionRequested = MAKEWORD(2, 0);
    if (::WSAStartup(wVersionRequested, &wsaData) != 0)
        return false;
#endif

    // Get the hostname
    if (gethostname(szBuffer, sizeof(szBuffer)) != 0)
    {
#ifdef WIN32
        WSACleanup();
#endif
        return false;
    }

    // Use getaddrinfo to retrieve the IP address
    struct addrinfo hints = {};
    struct addrinfo* result = nullptr;

    hints.ai_family = AF_INET; // IPv4
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    if (getaddrinfo(szBuffer, nullptr, &hints, &result) != 0)
    {
#ifdef WIN32
        WSACleanup();
#endif
        return false;
    }

    // Extract the first IPv4 address
    for (struct addrinfo* ptr = result; ptr != nullptr; ptr = ptr->ai_next)
    {
        if (ptr->ai_family == AF_INET) // Ensure it's IPv4
        {
            struct sockaddr_in* sockaddr_ipv4 = (struct sockaddr_in*)ptr->ai_addr;
            myIP.b1 = sockaddr_ipv4->sin_addr.S_un.S_un_b.s_b1;
            myIP.b2 = sockaddr_ipv4->sin_addr.S_un.S_un_b.s_b2;
            myIP.b3 = sockaddr_ipv4->sin_addr.S_un.S_un_b.s_b3;
            myIP.b4 = sockaddr_ipv4->sin_addr.S_un.S_un_b.s_b4;

            freeaddrinfo(result);
#ifdef WIN32
            WSACleanup();
#endif
            return true;
        }
    }

    freeaddrinfo(result);
#ifdef WIN32
    WSACleanup();
#endif
    return false;
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
        std::cout << "Passive socket == invalid socket" << std::endl;
        closesocket(passiveSocket);
        return this->SendString(ClientContext, "451 Requested action aborted. Local error in processing.");
    }

    SOCKADDR_IN serverAddr = { .sin_family = AF_INET, .sin_port = htons((rand() % 5000) + 60001), .sin_addr = 0UL };
    int status = bind(passiveSocket, reinterpret_cast<PSOCKADDR>(&serverAddr), sizeof(serverAddr));
    if (status == SOCKET_ERROR)
    {
        std::cout << "Binding error == sockket erorr" << std::endl;
        closesocket(passiveSocket);
        return this->SendString(ClientContext, "451 Requested action aborted. Local error in processing.");
    }

    status = listen(passiveSocket, SOMAXCONN);
    if (status == SOCKET_ERROR)
    {
        std::cout << "Listen status == socket errorr" << std::endl;
        closesocket(passiveSocket);
        return this->SendString(ClientContext, "451 Requested action aborted. Local error in processing.");
    }

    IPv4 ipv4 = { 0 };
    getMyIP(ipv4);
    ClientContext.DataIPv4 = serverAddr.sin_addr;
    ClientContext.DataPort = serverAddr.sin_port;
    ClientContext.DataSocket = passiveSocket;
    ClientContext.DataSocketType = DATASOCKET_TYPE::Passive;

    CHAR message[MESSAGE_MAX_LENGTH] = { 0 };
        _snprintf_s(message, sizeof(message), _TRUNCATE,
        "227 Entering Passive Mode (%u,%u,%u,%u,%u,%u).",
        ipv4.b1 & 0xFF,
        ipv4.b2 & 0xFF,
        ipv4.b3 & 0xFF,
        ipv4.b4 & 0xFF,
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

bool FtpServer::HandleRetr(CLIENT_CONTEXT& ClientContext, const std::string& Argument)
{
    if (ClientContext.Access == CLIENT_ACCESS::NotLoggedIn)
    {
        return this->SendString(ClientContext, "530 Please login with USER and PASS.");
    }

    if (Argument.size() == 0)
    {
        return this->SendString(ClientContext, "501 Syntax error in parameters or arguments.");
    }

    bool status = false;
    bool fileFound = false;
    WIN32_FIND_DATAA fileData = { 0 };
    HANDLE fileHandle = FindFirstFileA((std::string(ClientContext.CurrentDir) + "\\*").c_str(), &fileData);
    do
    {
        if (!strcmp(Argument.c_str(), fileData.cFileName))
        {
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
                int socketStatus = connect(dataSocket, reinterpret_cast<PSOCKADDR>(&clientAddr), sizeof(clientAddr));
                if (socketStatus == SOCKET_ERROR)
                {
                    closesocket(dataSocket);
                    return this->SendString(ClientContext, "550 File or directory unavailable.");
                }
            }

            std::ifstream file(std::string(ClientContext.CurrentDir) + "\\" + fileData.cFileName, std::ios::binary);
            if (!file.is_open())
            {
                closesocket(dataSocket);
                return this->SendString(ClientContext, "550 File not found or access denied.");
            }

            CHAR buffer[DEFAULT_BUFLEN] = { 0 };
            while (file.read(buffer, sizeof(buffer)) || file.gcount())
            {
                if (send(dataSocket, buffer, static_cast<int>(file.gcount()), 0) == SOCKET_ERROR)
                {
                    file.close();
                    closesocket(dataSocket);
                    return this->SendString(ClientContext, "426 Connection closed; transfer aborted.");
                }
            }

            file.close();
            closesocket(dataSocket);
            status = this->SendString(ClientContext, "226 Transfer complete.");
            fileFound = true;

            break;
        }
    } while (FindNextFileA(fileHandle, &fileData));
    FindClose(fileHandle);

    if (!fileFound)
    {
        status = this->SendString(ClientContext, "550 File or directory unavailable.");
    }

    return status;
}

bool FtpServer::HandleType(CLIENT_CONTEXT& ClientContext, const std::string& Argument)
{
    if (ClientContext.Access == CLIENT_ACCESS::NotLoggedIn)
    {
        return this->SendString(ClientContext, "530 Please login with USER and PASS.");
    }

    if (Argument.size() == 0)
    {
        return this->SendString(ClientContext, "501 Syntax error in parameters or arguments.");
    }

    switch (Argument.c_str()[0])
    {
    case 'A':
    case 'a':
        return this->SendString(ClientContext, "200 Type set to A.");

    case 'I':
    case 'i':
        return this->SendString(ClientContext, "200 Type set to I.");

    default:
        return this->SendString(ClientContext, "501 Syntax error in parameters or argument.");
    }
}

bool FtpServer::HandleStor(CLIENT_CONTEXT& ClientContext, const std::string& Argument)
{
    if (ClientContext.Access == CLIENT_ACCESS::NotLoggedIn)
    {
        return this->SendString(ClientContext, "530 Please login with USER and PASS.");
    }

    if (ClientContext.Access == CLIENT_ACCESS::ReadOnly)
    {
        return this->SendString(ClientContext, "550 Permission denied.");
    }

    if (Argument.size() == 0)
    {
        return this->SendString(ClientContext, "501 Syntax error in parameters or arguments.");
    }

    std::ofstream file(std::string(ClientContext.CurrentDir) + "\\" + Argument, std::ios::binary);
    if (!file.is_open())
    {
        return this->SendString(ClientContext, "550 Cannot open file for writing.");
    }

    this->SendString(ClientContext, "150 Opening data connection.");

    SOCKET dataSocket = { 0 };
    if (ClientContext.DataSocketType == DATASOCKET_TYPE::Passive)
    {
        dataSocket = accept(ClientContext.DataSocket, NULL, NULL);
        closesocket(ClientContext.DataSocket);
        ClientContext.DataSocket = dataSocket;
        if (dataSocket == INVALID_SOCKET)
        {
            file.close();
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
            file.close();
            closesocket(dataSocket);
            return this->SendString(ClientContext, "550 File or directory unavailable.");
        }
    }

    int bytesRead;
    CHAR buffer[DEFAULT_BUFLEN] = { 0 };
    while ((bytesRead = recv(dataSocket, buffer, sizeof(buffer), 0)) > 0)
    {
        file.write(buffer, bytesRead);
    }

    file.close();
    closesocket(dataSocket);

    if (bytesRead < 0)
    {
        return this->SendString(ClientContext, "426 Connection closed; transfer aborted.");
    }

    return this->SendString(ClientContext, "226 Transfer complete.");
}

bool FtpServer::HandleNlst(CLIENT_CONTEXT& ClientContext, const std::string& Argument)
{
    return this->HandleList(ClientContext, Argument);

    /*
    if (ClientContext.Access == CLIENT_ACCESS::NotLoggedIn) {
        return this->SendString(ClientContext, "530 Please login with USER and PASS.");
    }

    std::string dirPath = ClientContext.CurrentDir;
    if (!Argument.empty() && !Argument.starts_with("-")) {
        dirPath += "\\" + Argument;
    }

    WIN32_FIND_DATAA fileData;
    HANDLE hFind = FindFirstFileA((dirPath + "\\*").c_str(), &fileData);
    if (hFind == INVALID_HANDLE_VALUE) {
        return this->SendString(ClientContext, "450 Requested file action not taken. Directory unavailable.");
    }

    std::stringstream fileList;
    do {
        if (!(fileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            fileList << fileData.cFileName << "\r\n";
        }
    } while (FindNextFileA(hFind, &fileData));
    FindClose(hFind);

    return this->SendString(ClientContext.DataSocket, fileList.str()) &&
        this->SendString(ClientContext, "226 Transfer complete."); */
}



