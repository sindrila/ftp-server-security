#include <iostream>
#include "FtpServer.h"
int main()
{
	try
	{
		FtpServer ftpServer;
		ftpServer.Start();

	}
	catch (const std::exception& exception)
	{
		std::cout << exception.what();
	}

	return 0;
}
