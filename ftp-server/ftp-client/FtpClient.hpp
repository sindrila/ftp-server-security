#pragma once
#include <string>
#include <vector>
#include <WS2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")

class FtpClient
{
private:
    SOCKET controlSocket;
    bool isConnected;

    std::string ReceiveResponse();
    bool SendCommand(const std::string& command);

public:
    FtpClient();
    ~FtpClient();

    void Start(std::istream& in, std::ostream& out);
    bool Connect(const std::string& serverIP);
    void SendUser(const std::string& username);
    void SendPassword(const std::string& password);
    void ListFiles();
    void DownloadFile(const std::string& fileName);
    bool EnterPassiveMode(std::string& serverIP, int& serverPort);
    void Disconnect(bool waitForResponse);
};