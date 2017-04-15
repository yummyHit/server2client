
#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include <string.h>
#include <list>
#include <winsock2.h>
#include <Windows.h> // winsock2 는 무조건 windows.h 보다 먼저 include 해야한다.
#include <mutex>

using namespace std;

#pragma comment(lib, "ws2_32.lib")

#define HYPERTRANSFER_PACKETSIZE_DEFAULT		1024
#define HYPERTRANSFER_ACKPERIOD_DEFAULT			1024
#define HYPERTRANSFER_THREAD_NUMBER				3
#define HYPERTRANSFER_TCP_PORT					2845
#define HYPERTRANSFER_UDP_PORT					2846
#define HYPERTRANSFER_BUFFERSIZE_MAX			2

#define HYPERTRANSFER_ACK_READY					0
#define HYPERTRANSFER_ACK_DONE					1
#define HYPERTRANSFER_ACK_UNKOWN				2


typedef struct stHYPERTRANSFER_ATTRIBUTE
{
	bool bServer;
	bool bSend;
	bool bCompress;
	unsigned int iPacketSize; // For transfer using UDP (send use only)
	unsigned short nAckPeriod; // Ack period
	unsigned int iTCPPort; // To use TCP port number
	unsigned int iUDPPort; // To use UDP port number
	char strDestIP[256]; // To communicate destination IP
} HYPERTRANSFER_ATTRIBUTE;



class HYPERTRANSFER
{
private:
#pragma pack(push, 1)
	// Structure
	// For TCP - To transfer data information
	typedef struct stDATAINFO
	{
		bool bCompress; // Check compress
		unsigned int iFileSize; // This file size(byte)
		unsigned int iPacketSize; // Transfer one UDP packet size
		unsigned short nAckPeriod; // Ack period (counting bits)
		char strFileName[256]; // File name
	} DATAINFO;

	// For TCP - To check packet loss and completion
	typedef struct stACKINFO
	{
		unsigned short nClusterNumber; // To re-transfer cluster number (Start: 1 ~ 65535)
		unsigned char cAttribute;
		char* pAck; // To re-transfer ack
	} ACKINFO;

	// For UDP
	typedef struct stPACKETDATA
	{
		unsigned short nClusterNumber; // 1 ~ 65535
		unsigned short iPacketNumber; // Using 2 bytes(0 ~ 65535)
		char* pData; // Packet data(size: DATAINFO::iPacketSize)
	} PACKETDATA;
#pragma pack(pop)



	HYPERTRANSFER_ATTRIBUTE m_TransferAttr;
	bool bDoneTransfer; // All transfer are done(TCP-UDP-TCP) (ON/OFF: TCP)
	bool bDoTransfer; // Do transfer (ON/OFF: TCP/UDP)
	bool bFileEnd; // Read/Write end (ON/OFF: I/O)
	bool bAck;
	char m_strFileName[256]; // To send or receive file name
	int m_iLastPacketSize;
	DATAINFO* m_pDataInfo;
	ACKINFO** m_pAckInfo; // Packet info array (size: HYPERTRANSFER_BUFFERSIZE_MAX)
	HANDLE m_pthread[HYPERTRANSFER_THREAD_NUMBER];
	HANDLE hMutex;
	int nLastClusterNumber;
	PACKETDATA*** m_pppRetPacketData; // For re-transfer data
	list <PACKETDATA*> m_PacketList; // For receiving
	unsigned char nDataSize;
	int Ackidx;

protected:
	void DoACK();
	void DoUDP();
	void DoIO();
	int ReadFileAndAddPacketList(FILE* fp, int pos, int ReadSize);
	inline int GetBit(int x, int n);
	inline void SetBit(char* x, char n, unsigned char data);

public:
	HYPERTRANSFER();
	HYPERTRANSFER(HYPERTRANSFER_ATTRIBUTE* ta);
	~HYPERTRANSFER();

	static DWORD WINAPI ThreadACK(LPVOID lpParam);
	static DWORD WINAPI ThreadUDP(LPVOID lpParam);
	static void* ThreadCompression(void* arg);
	static DWORD WINAPI ThreadIO(LPVOID lpParam);

	// Set up saving destination filename or transfer source filename
	void SetFileName(char* strFilename);

	// Set transfer attributes
	void SetAttribute(HYPERTRANSFER_ATTRIBUTE* ta);

	int Transfer();
};


