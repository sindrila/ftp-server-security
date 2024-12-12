#include "FtpClient.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include "Utils.hpp"
#include <iomanip>

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
#define TRANSFER_MODE_BINARY "BINARY"
#define TRANSFER_MODE_ASCII "ASCII"
#define QUIT_COMMAND "QUIT"
#define PASV_RESPONSE "227"
#define GET_COMMAND "GET"
#define PUT_COMMAND "PUT"
#define EXIT "EXIT"
#define HELP "HELP"

FtpClient::FtpClient(std::istream& in, std::ostream& out, std::ostream& err)
	: controlSocket(INVALID_SOCKET), dataSocket(INVALID_SOCKET), isConnected(false), isBinaryTransfer(false), input(in), output(out), error(err) {}

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

	output << ReceiveResponse(controlSocket);
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
	output << ReceiveResponse(controlSocket);
}

void FtpClient::SendPassword(const std::string& password)
{
	if (!isConnected)
	{
		error << "Not connected to any server.\n";
		return;
	}

	SendCommand("PASS " + password);
	output << ReceiveResponse(controlSocket);
}

bool FtpClient::EnterPassiveMode()
{
	if (!SendCommand(PASV_COMMAND))
	{
		error << "Failed to send PASV command.\n";
		return false;
	}

	std::string response = ReceiveResponse(controlSocket);
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
		CleanupSocket(dataSocket);
		return false;
	}

	output << "Data connection established to " << pasvServerIP << ":" << pasvServerPort << std::endl;
	return true;
}

void FtpClient::SetTransferMode(bool isBinary)
{
	if (!isConnected)
	{
		error << "Not connected to any server.\n";
		return;
	}

	std::string modeCommand = isBinary ? "TYPE I" : "TYPE A";
	if (!SendCommand(modeCommand))
	{
		error << "Failed to set transfer mode.\n";
		return;
	}

	output << "Transfer mode set to " << (isBinary ? "binary" : "ASCII") << ".\n";
	output << ReceiveResponse(controlSocket);
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

	SendCommand(LIST_COMMAND);
	std::string response = ReceiveResponse(controlSocket);
	if (response[0] != '1')
	{
		error << "LIST command failed. Server response: " << response << std::endl;
		return;
	}

	char buffer[DEFAULT_BUFLEN];
	int bytesRead;
	while ((bytesRead = recv(dataSocket, buffer, sizeof(buffer), 0)) > 0)
	{
		output.write(buffer, bytesRead);
	}

	if (bytesRead == SOCKET_ERROR)
	{
		error << "Error reading data socket: " << WSAGetLastError() << std::endl;
	}

	CleanupSocket(dataSocket);

	response = ReceiveResponse(controlSocket);
	if (response[0] != '2')
	{
		error << "Error completing LIST command. Server response: " << response << std::endl;
	}

	output << "List command completed.\n";
}


void FtpClient::DownloadFile(const std::string& fileName, const std::string& localFileName)
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
	std::string response = ReceiveResponse(controlSocket);

	if (response.substr(0, 3) != "150" && response.substr(0, 3) != "125")
	{
		error << "Error initiating file download. Server response: " << response << std::endl;
		return;
	}

	std::ofstream outFile(localFileName, std::ios::binary | std::ios::trunc);
	if (!outFile.is_open())
	{
		error << "Failed to open local file for writing: " << localFileName << std::endl;
		return;
	}

	char buffer[DEFAULT_BUFLEN];
	int bytesRead;
	while ((bytesRead = recv(dataSocket, buffer, sizeof(buffer), 0)) > 0)
	{
		outFile.write(buffer, bytesRead);
	}

	if (bytesRead == SOCKET_ERROR)
	{
		error << "Error reading data socket: " << WSAGetLastError() << std::endl;
	}

	outFile.close();
	CleanupSocket(dataSocket);

	response = ReceiveResponse(controlSocket);
	if (response.substr(0, 3) != "226")
	{
		error << "Error completing file download. Server response: " << response << std::endl;
	}
	else
	{
		output << "Download completed successfully.\n";
	}
}

void FtpClient::UploadFile(const std::string& fileName, const std::string& remoteFileName)
{
	if (!isConnected)
	{
		error << "Not connected to any server.\n";
		return;
	}

	std::ifstream inFile(fileName, this->isBinaryTransfer ? std::ios::binary : std::ios::in);
	if (!inFile.is_open())
	{
		error << "Failed to open local file for reading: " << fileName << std::endl;
		return;
	}

	if (!EnterPassiveMode())
	{
		error << "Failed to enter passive mode.\n";
		return;
	}

	std::string command = std::string(STOR_COMMAND) + " " + remoteFileName;
	SendCommand(command);
	std::string response = ReceiveResponse(controlSocket);

	if (response.substr(0, 3) != "150" && response.substr(0, 3) != "125")
	{
		error << "Error initiating file upload. Server response: " << response << std::endl;
		inFile.close();
		return;
	}

	char buffer[DEFAULT_BUFLEN];
	while (inFile.read(buffer, sizeof(buffer)).gcount() > 0)
	{
		int bytesSent = send(dataSocket, buffer, static_cast<int>(inFile.gcount()), 0);
		if (bytesSent == SOCKET_ERROR)
		{
			error << "Error sending file data: " << WSAGetLastError() << std::endl;
			inFile.close();
			closesocket(dataSocket);
			dataSocket = INVALID_SOCKET;
			return;
		}
	}

	inFile.close();
	CleanupSocket(dataSocket);

	response = ReceiveResponse(controlSocket);
	if (response.substr(0, 3) != "226")
	{
		error << "Error completing file upload. Server response: " << response << std::endl;
	}
	else
	{
		output << "Upload completed successfully.\n";
	}
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
		output << ReceiveResponse(controlSocket);
	}

	CleanupSocket(dataSocket);
	CleanupSocket(controlSocket);
	this->isConnected = false;

	output << "Disconnected from the server.\n";
}

std::string FtpClient::ReceiveResponse(SOCKET socket)
{
	std::string response;
	char buffer[DEFAULT_BUFLEN] = { 0 };
	int bytesReceived = 0;

	do {
		bytesReceived = recv(socket, buffer, DEFAULT_BUFLEN - 1, 0);
		if (bytesReceived > 0) {
			response.append(buffer, bytesReceived);
		}
		else if (bytesReceived < 0) {
			return "Receive failed with error: " + std::to_string(WSAGetLastError()) + "\n";
		}
	} while (bytesReceived > 0 && response.find("\r\n") == std::string::npos);

	return response;
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

void FtpClient::CleanupSocket(SOCKET socket)
{
	if (socket != INVALID_SOCKET)
	{
		closesocket(socket);
		socket = INVALID_SOCKET;
	}
}

void FtpClient::Start()
{
	std::string command;

	output << "FTP Client started. Type 'help' for available commands.\n";
	while (true)
	{
		output << "FTP >> ";
		std::getline(input, command);

		std::istringstream iss(command);
		std::string action, arg1, arg2;
		iss >> action;
		action = Utils::toUpperCase(action);

		if (iss >> std::quoted(arg1))
		{
			iss >> std::quoted(arg2);
		}

		if (action == CONNECT_COMMAND)
		{
			if (arg1.empty())
			{
				output << "Usage: connect <serverIP>\n";
			}
			else
			{
				Connect(arg1);
			}
		}
		else if (action == USER_COMMAND)
		{
			if (arg1.empty())
			{
				output << "Usage: user <username>\n";
			}
			else
			{
				SendUser(arg1);
			}
		}
		else if (action == PASS_COMMAND)
		{
			if (arg1.empty())
			{
				output << "Usage: pass <password>\n";
			}
			else
			{
				SendPassword(arg1);
			}
		}
		else if (action == LIST_COMMAND)
		{
			ListFiles();
		}
		else if (action == GET_COMMAND)
		{
			if (arg1.empty() || arg2.empty())
			{
				output << "Usage: get <remoteFileName> <localFileName>\n";
			}
			else
			{
				DownloadFile(arg1, arg2);
			}
		}
		else if (action == PUT_COMMAND)
		{
			if (arg1.empty() || arg2.empty())
			{
				output << "Usage: put <localFileName> <remoteFileName>\n";
			}
			else
			{
				UploadFile(arg1, arg2);
			}
		}
		else if (action == TRANSFER_MODE_BINARY)
		{
			SetTransferMode(true);
		}
		else if (action == TRANSFER_MODE_ASCII)
		{
			SetTransferMode(false);
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
				<< "  get <remoteFileName> <localFileName> - Download a file\n"
				<< "  put <localFileName> <remoteFileName> - Upload a file\n"
				<< "  binary             - Switch file transfer mode to binary\n"
				<< "  ascii              - Switch file transfer mode to ASCII\n"
				<< "  disconnect         - Disconnects from the FTP server\n"
				<< "  quit               - Exit the client\n";
		}
		else
		{
			output << "Unknown command. Type 'help' for available commands.\n";
		}
	}
}