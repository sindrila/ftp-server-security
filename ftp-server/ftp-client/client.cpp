#include "FtpClient.hpp"
#include <iostream>

int main(int argc, char* argv[])
{
	FtpClient ftpClient;

	if (argc > 1)
	{
		std::string serverAddress = argv[1];
		ftpClient.Connect(serverAddress);
	}

	ftpClient.Start();

	return 0;
}