#pragma once
#include <string>
#include <vector>
#include <iostream>
#include <WS2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")

#define DEFAULT_FTP_PORT "21"

class FtpClient
{
private:
    std::istream& input;
    std::ostream& output;
    std::ostream& error;
    std::string serverIP;
    std::string serverPort;
    SOCKET controlSocket;
    SOCKET dataSocket;
    bool isConnected;
    bool isBinaryTransfer;

    std::string ReceiveResponse(SOCKET socket);
    bool SendCommand(const std::string& command);
    std::vector<std::string> ParseArguments(const std::string& commandLine);
    void CleanupDataSocket();

public:
    FtpClient(std::istream& input = std::cin, std::ostream& output = std::cout, std::ostream& error = std::cerr);
    ~FtpClient();

    void Start();
    bool Connect(const std::string& serverIP, const std::string& port = DEFAULT_FTP_PORT);
    void SendUser(const std::string& username);
    void SendPassword(const std::string& password);
    void ListFiles();
    void DownloadFile(const std::string& fileName, const std::string& localFileName);
    void UploadFile(const std::string& fileName, const std::string& remoteFileName);
    bool EnterPassiveMode();
    void SetTransferMode(bool isBinary);
    void Disconnect(bool waitForResponse);
};