#include "FtpClient.hpp"
#include <iostream>
#include <sstream>
#include "Utils.hpp"

#define DEFAULT_PORT "21"
#define DEFAULT_BUFLEN 512
#define CONNECT_COMMAND "CONNECT"
#define DISCONNECT_COMMAND "DISCONNECT"
#define USER_COMMAND "USER"
#define PASS_COMMAND "PASS"
#define PASV_COMMAND "PASV"
#define RETR_COMMAND "RETR"
#define LIST_COMMAND "LIST"
#define QUIT_COMMAND "QUIT"
#define PASV_RESPONSE "227"
#define GET_COMMAND "GET"
#define EXIT "EXIT"
#define HELP "HELP"

FtpClient::FtpClient() : controlSocket(INVALID_SOCKET), isConnected(false) {}

FtpClient::~FtpClient()
{
    if (controlSocket != INVALID_SOCKET)
    {
        closesocket(controlSocket);
    }

    WSACleanup();
}

bool FtpClient::Connect(const std::string& serverIP)
{
    if (isConnected)
    {
        std::cerr << "Already connected to a server.\n";
        return false;
    }

    WSADATA wsaData;
    int status = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (status)
    {
        std::cerr << "WSAStartup failed with status: " << status << std::endl;
        return false;
    }

    ADDRINFOA hints = { 0 };
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    ADDRINFOA* result = nullptr;
    status = getaddrinfo(serverIP.c_str(), DEFAULT_PORT, &hints, &result);
    if (status != 0)
    {
        std::cerr << "getaddrinfo failed with status: " << status << std::endl;
        WSACleanup();
        return false;
    }

    controlSocket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (controlSocket == INVALID_SOCKET)
    {
        std::cerr << "Socket creation failed with error: " << WSAGetLastError() << std::endl;
        freeaddrinfo(result);
        WSACleanup();
        return false;
    }

    status = connect(controlSocket, result->ai_addr, (int)result->ai_addrlen);
    freeaddrinfo(result);
    if (status == SOCKET_ERROR)
    {
        std::cerr << "Connection to server failed with error: " << WSAGetLastError() << std::endl;
        closesocket(controlSocket);
        WSACleanup();
        return false;
    }

    std::cout << ReceiveResponse();
    isConnected = true;
    return true;
}

void FtpClient::SendUser(const std::string& username)
{
    if (!isConnected)
    {
		std::cerr << "Not connected to any server.\n";
		return;
	}

    SendCommand("USER " + username);
    std::cout << ReceiveResponse();
}

void FtpClient::SendPassword(const std::string& password)
{
    if (!isConnected)
    {
        std::cerr << "Not connected to any server.\n";
        return;
    }

    SendCommand("PASS " + password);
    std::cout << ReceiveResponse();
}

bool FtpClient::EnterPassiveMode(std::string& serverIP, int& serverPort)
{
    // TODO: test this function
    if (!SendCommand("PASV"))
    {
        std::cerr << "Failed to send PASV command.\n";
        return false;
    }

    std::string response = ReceiveResponse();
    std::cout << "Server Response: " << response;

    if (response.find(PASV_RESPONSE) == std::string::npos)
    {
        std::cerr << "PASV failed. Server response: " << response << std::endl;
        return false;
    }

    size_t start = response.find('(');
    size_t end = response.find(')');
    if (start == std::string::npos || end == std::string::npos || start >= end)
    {
        std::cerr << "Malformed PASV response: " << response << std::endl;
        return false;
    }

    std::string data = response.substr(start + 1, end - start - 1);
    std::vector<int> numbers;
    std::stringstream ss(data);
    std::string token;
    while (std::getline(ss, token, ','))
    {
        numbers.push_back(std::stoi(token));
    }

    if (numbers.size() != 6)
    {
        std::cerr << "Invalid PASV response format: " << data << std::endl;
        return false;
    }

    serverIP = std::to_string(numbers[0]) + "." + std::to_string(numbers[1]) + "." +
        std::to_string(numbers[2]) + "." + std::to_string(numbers[3]);
    serverPort = (numbers[4] << 8) + numbers[5];

    std::cout << "Passive Mode IP: " << serverIP << ", Port: " << serverPort << std::endl;
    return true;
}

void FtpClient::ListFiles()
{
    if (!isConnected)
    {
        std::cerr << "Not connected to any server.\n";
        return;
    }

    SendCommand(PASV_COMMAND);
    std::cout << ReceiveResponse();

    SendCommand(LIST_COMMAND);
    std::cout << ReceiveResponse();
}

void FtpClient::DownloadFile(const std::string& fileName)
{
    if (!isConnected)
    {
        std::cerr << "Not connected to any server.\n";
        return;
    }

    SendCommand(PASV_COMMAND);
    std::cout << ReceiveResponse();

    std::string command = std::string(RETR_COMMAND) + " " + fileName;
    SendCommand(command);
    std::cout << ReceiveResponse();
}

void FtpClient::Disconnect(bool waitForResponse = true)
{
    if (!isConnected)
    {
        std::cerr << "Not connected to any server.\n";
        return;
    }

    if (!SendCommand(QUIT_COMMAND)) 
    {
        std::cerr << "Failed to send QUIT command.\n";
        return;
    }

    if (waitForResponse) 
    {
        std::cout << ReceiveResponse();
    }

    closesocket(controlSocket);
    controlSocket = INVALID_SOCKET;
    isConnected = false;

    std::cout << "Disconnected from the server.\n";
}

std::string FtpClient::ReceiveResponse()
{
    char buffer[DEFAULT_BUFLEN] = { 0 };
    int bytesReceived = recv(controlSocket, buffer, DEFAULT_BUFLEN, 0);
    if (bytesReceived > 0)
    {
        return std::string(buffer, bytesReceived);
    }
    else if (bytesReceived == 0)
    {
        return "Connection closed by server.\n";
    }
    else
    {
        return "Receive failed with error: " + std::to_string(WSAGetLastError()) + "\n";
    }
}

bool FtpClient::SendCommand(const std::string& command)
{
    if (!isConnected)
    {
        std::cerr << "Not connected to any server.\n";
        return false;
    }

    std::string formattedCommand = command + "\r\n";
    int status = send(controlSocket, formattedCommand.c_str(), static_cast<int>(formattedCommand.size()), 0);
    if (status == SOCKET_ERROR)
    {
        std::cerr << "Send failed with error: " << WSAGetLastError() << std::endl;
        return false;
    }

    return true;
}

void FtpClient::Start(std::istream& in, std::ostream& out)
{
    std::string command, argument;

    out << "FTP Client started. Type 'help' for available commands.\n";
    while (true)
    {
        out << "FTP > ";
        std::getline(in, command);

        std::istringstream iss(command);
        std::string action;
        iss >> action;
        action = Utils::toUpperCase(action);

        if (action == CONNECT_COMMAND)
        {
            iss >> argument;
            if (argument.empty())
            {
                out << "Usage: connect <serverIP>\n";
            }
            else
            {
                Connect(argument);
            }
        }
        else if (action == USER_COMMAND)
        {
            iss >> argument;
            if (argument.empty())
            {
                out << "Usage: user <username>\n";
            }
            else
            {
                SendUser(argument);
            }
        }
        else if (action == PASS_COMMAND)
        {
            iss >> argument;
            if (argument.empty())
            {
                out << "Usage: pass <password>\n";
            }
            else
            {
                SendPassword(argument);
            }
        }
        else if (action == LIST_COMMAND)
        {
            ListFiles();
        }
        else if (action == GET_COMMAND)
        {
            iss >> argument;
            if (argument.empty())
            {
                out << "Usage: get <fileName>\n";
            }
            else
            {
                DownloadFile(argument);
            }
        }
        else if (action == DISCONNECT_COMMAND)
        {
			Disconnect();
		}
        else if (action == QUIT_COMMAND || action == EXIT)
        {
            Disconnect(false);
            break;
        }
        else if (action == HELP)
        {
            out << "Available commands:\n"
                << "  connect <serverIP> - Connect to the FTP server\n"
                << "  user <username>    - Send username\n"
                << "  pass <password>    - Send password\n"
                << "  list               - List files in directory\n"
                << "  get <fileName>     - Download a file\n"
                << "  disconnect         - Disconnects from the FTP server\n"
                << "  quit               - Exit the client\n";
        }
        else
        {
            out << "Unknown command. Type 'help' for available commands.\n";
        }
    }
}