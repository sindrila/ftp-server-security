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
#define STOR_COMMAND "STOR"
#define LIST_COMMAND "LIST"
#define QUIT_COMMAND "QUIT"
#define PASV_RESPONSE "227"
#define GET_COMMAND "GET"
#define EXIT "EXIT"
#define HELP "HELP"

FtpClient::FtpClient(std::istream& in, std::ostream& out, std::ostream& err)
	: controlSocket(INVALID_SOCKET), dataSocket(INVALID_SOCKET), isConnected(false), input(in), output(out), error(err) {}

FtpClient::~FtpClient()
{
	if (controlSocket != INVALID_SOCKET)
	{
		closesocket(controlSocket);
	}

	WSACleanup();
}

bool FtpClient::Connect(const std::string& serverIP, const std::string& port)
{
	if (isConnected)
	{
		error << "Already connected to a server.\n";
		return false;
	}

	WSADATA wsaData;
	int status = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (status)
	{
		error << "WSAStartup failed with status: " << status << std::endl;
		return false;
	}

	ADDRINFOA hints = { 0 };
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;

	ADDRINFOA* result = nullptr;
	status = getaddrinfo(serverIP.c_str(), port.c_str(), &hints, &result);
	if (status != 0)
	{
		error << "getaddrinfo failed with status: " << status << std::endl;
		WSACleanup();
		return false;
	}

	this->controlSocket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
	if (controlSocket == INVALID_SOCKET)
	{
		error << "Socket creation failed with error: " << WSAGetLastError() << std::endl;
		freeaddrinfo(result);
		WSACleanup();
		return false;
	}

	status = connect(controlSocket, result->ai_addr, (int)result->ai_addrlen);
	freeaddrinfo(result);
	if (status == SOCKET_ERROR)
	{
		error << "Connection to server failed with error: " << WSAGetLastError() << std::endl;
		closesocket(controlSocket);
		WSACleanup();
		return false;
	}

	output << ReceiveResponse();
	this->isConnected = true;
	this->serverIP = serverIP;
	this->serverPort = port;
	return true;
}

void FtpClient::SendUser(const std::string& username)
{
	if (!isConnected)
	{
		error << "Not connected to any server.\n";
		return;
	}

	SendCommand("USER " + username);
	output << ReceiveResponse();
}

void FtpClient::SendPassword(const std::string& password)
{
	if (!isConnected)
	{
		error << "Not connected to any server.\n";
		return;
	}

	SendCommand("PASS " + password);
	output << ReceiveResponse();
}

bool FtpClient::EnterPassiveMode()
{
	if (!SendCommand(PASV_COMMAND))
	{
		error << "Failed to send PASV command.\n";
		return false;
	}

	std::string response = ReceiveResponse();
	output << "Server Response: " << response;

	if (response.find(PASV_RESPONSE) == std::string::npos)
	{
		error << "PASV failed. Server response: " << response << std::endl;
		return false;
	}

	size_t start = response.find('(');
	size_t end = response.find(')');
	if (start == std::string::npos || end == std::string::npos || start >= end)
	{
		error << "Malformed PASV response: " << response << std::endl;
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
		error << "Invalid PASV response format: " << data << std::endl;
		return false;
	}

	std::string pasvServerIP = std::to_string(numbers[0]) + "." + std::to_string(numbers[1]) + "." +
		std::to_string(numbers[2]) + "." + std::to_string(numbers[3]);
	int pasvServerPort = (numbers[4] << 8) + numbers[5];

	output << "Passive Mode at: " << pasvServerIP << ":" << pasvServerPort << std::endl;

	this->dataSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (dataSocket == INVALID_SOCKET)
	{
		error << "Data socket creation failed with error: " << WSAGetLastError() << std::endl;
		return false;
	}

	sockaddr_in dataAddr = { 0 };
	dataAddr.sin_family = AF_INET;
	inet_pton(AF_INET, pasvServerIP.c_str(), &dataAddr.sin_addr);
	dataAddr.sin_port = htons(pasvServerPort);

	if (connect(dataSocket, reinterpret_cast<sockaddr*>(&dataAddr), sizeof(dataAddr)) == SOCKET_ERROR)
	{
		error << "Data socket connection failed with error: " << WSAGetLastError() << std::endl;
		closesocket(dataSocket);
		this->dataSocket = INVALID_SOCKET;
		return false;
	}

	output << "Data connection established to " << pasvServerIP << ":" << pasvServerPort << std::endl;
	return true;
}

void FtpClient::ListFiles()
{
	if (!isConnected)
	{
		error << "Not connected to any server.\n";
		return;
	}

	if (!EnterPassiveMode())
	{
		error << "Failed to enter passive mode.\n";
		return;
	}

	if (!SendCommand(LIST_COMMAND))
	{
		error << "Failed to send LIST command.\n";
		return;
	}

	output << "Control Response: " << ReceiveResponse();

	char buffer[DEFAULT_BUFLEN] = { 0 };
	int bytesReceived = recv(dataSocket, buffer, DEFAULT_BUFLEN - 1, 0);
	while (bytesReceived > 0)
	{
		output << std::string(buffer, bytesReceived);
		bytesReceived = recv(dataSocket, buffer, DEFAULT_BUFLEN - 1, 0);
	}

	if (bytesReceived == SOCKET_ERROR)
	{
		error << "Error receiving data on the data socket: " << WSAGetLastError() << std::endl;
	}

	closesocket(dataSocket);
	this->dataSocket = INVALID_SOCKET;
}


void FtpClient::DownloadFile(const std::string& fileName)
{
	if (!isConnected)
	{
		error << "Not connected to any server.\n";
		return;
	}

	if (!EnterPassiveMode())
	{
		error << "Failed to enter passive mode.\n";
		return;
	}

	std::string command = std::string(RETR_COMMAND) + " " + fileName;
	SendCommand(command);
	output << ReceiveResponse();
}

void FtpClient::UploadFile(const std::string& fileName)
{
	if (!isConnected)
	{
		error << "Not connected to any server.\n";
		return;
	}

	EnterPassiveMode();

	std::string command = std::string(STOR_COMMAND) + " " + fileName;
	SendCommand(command);
	output << ReceiveResponse();
}

void FtpClient::Disconnect(bool waitForResponse = true)
{
	if (!isConnected)
	{
		error << "Not connected to any server.\n";
		return;
	}

	if (!SendCommand(QUIT_COMMAND))
	{
		error << "Failed to send QUIT command.\n";
		return;
	}

	if (waitForResponse)
	{
		output << ReceiveResponse();
	}

	closesocket(controlSocket);
	this->controlSocket = INVALID_SOCKET;
	closesocket(dataSocket);
	this->dataSocket = INVALID_SOCKET;
	this->isConnected = false;

	output << "Disconnected from the server.\n";
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
		error << "Not connected to any server.\n";
		return false;
	}

	std::string formattedCommand = command + "\r\n";
	int status = send(controlSocket, formattedCommand.c_str(), static_cast<int>(formattedCommand.size()), 0);
	if (status == SOCKET_ERROR)
	{
		error << "Send failed with error: " << WSAGetLastError() << " " << command << std::endl;
		return false;
	}

	return true;
}

void FtpClient::Start()
{
	std::string command, argument;

	output << "FTP Client started. Type 'help' for available commands.\n";
	while (true)
	{
		output << "FTP > ";
		std::getline(input, command);

		std::istringstream iss(command);
		std::string action;
		iss >> action;
		action = Utils::toUpperCase(action);

		if (action == CONNECT_COMMAND)
		{
			iss >> argument;
			if (argument.empty())
			{
				output << "Usage: connect <serverIP>\n";
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
				output << "Usage: user <username>\n";
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
				output << "Usage: pass <password>\n";
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
				output << "Usage: get <fileName>\n";
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
			output << "Available commands:\n"
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
			output << "Unknown command. Type 'help' for available commands.\n";
		}
	}
}