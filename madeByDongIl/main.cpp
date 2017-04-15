// TCP/IP Project: HyperTansfer No.4
// Used: only UDP
#include "hypertransfer.h"
#pragma comment(lib, "winmm.lib") 

int main()
{
	HYPERTRANSFER transfer;
	HYPERTRANSFER_ATTRIBUTE ua;
	char ip[128];
	char filename[256];
	DWORD TCPPort, UDPPort;
	DWORD prev, now;

	ua.bCompress = false;
	ua.bSend = true;
	ua.bServer = true;
	ua.iPacketSize = 1500 - 20 - 4 - 4;

	ua.nAckPeriod = 1024;

	cout << "--- Server ---" << endl;
	cout << "IP: ";
	cin >> ip;
	cout << "TCP Port: ";
	cin >> TCPPort;
	cout << "UDP Port: ";
	cin >> UDPPort;
	cout << "File name: ";
	cin >> filename;

	strcpy(ua.strDestIP, ip);
	ua.iTCPPort = TCPPort;
	ua.iUDPPort = UDPPort;

	transfer.SetFileName(filename);
	transfer.SetAttribute(&ua);

	prev = timeGetTime();
	transfer.Transfer();
	now = timeGetTime();

	FILE *fp;
	fp = fopen(filename, "rb");
	if (fp == NULL)
	{
		cout << "File don't exist: " << filename << endl;
		exit(1);
	}
	fseek(fp, 0, SEEK_END);

	cout << "Time: " << (float) (now - prev) / 1000 << endl;
	cout << "Filesize: " << ftell(fp) << endl;
	cout << "Transfer rate: " << (float) (ftell(fp) / ((float) (now - prev) / 1000)) / 1024 / 1024 << " MB/s" << endl;
	return 0;
}
