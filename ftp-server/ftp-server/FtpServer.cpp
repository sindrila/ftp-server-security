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
    else if (!command.compare("OPTS"))
    {
        this->HandleOpts(ClientContext, argument);
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
