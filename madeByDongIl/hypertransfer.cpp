
#include "hypertransfer.h"



int send_simul(SOCKET s, const char* buf, int len, int flags);
int sendto_simul(SOCKET s, const char* buf, int len, int flags, const sockaddr *to, int tolen);

HYPERTRANSFER::HYPERTRANSFER()
	: bDoneTransfer(false)
	, bDoTransfer(false)
	, bFileEnd(false)
	, bAck(false)
	, m_strFileName("")
	, m_pDataInfo(NULL)
	, m_pAckInfo(NULL)
	, m_pppRetPacketData(NULL)
	, nDataSize(0)
	, nLastClusterNumber(1)
{
	SetAttribute(NULL);
	hMutex = CreateMutex(NULL, FALSE, NULL);
}

HYPERTRANSFER::HYPERTRANSFER(HYPERTRANSFER_ATTRIBUTE* ta)
	: bDoneTransfer(false)
	, bDoTransfer(false)
	, bFileEnd(false)
	, bAck(false)
	, m_strFileName("")
	, m_pDataInfo(NULL)
	, m_pAckInfo(NULL)
	, m_pppRetPacketData(NULL)
	, nDataSize(0)
	, nLastClusterNumber(1)
{
	SetAttribute(ta);
	hMutex = CreateMutex(NULL, FALSE, NULL);
}

HYPERTRANSFER::~HYPERTRANSFER()
{
	CloseHandle(hMutex);
}

//-----------------------------------------------------------------------------//
// Set up saving destination filename or transfer source filename
//-----------------------------------------------------------------------------//
void HYPERTRANSFER::SetFileName(char * strFilename)
{
	strcpy(m_strFileName, strFilename);
}

//-----------------------------------------------------------------------------//
// Set transfer attribute
//-----------------------------------------------------------------------------//
void HYPERTRANSFER::SetAttribute(HYPERTRANSFER_ATTRIBUTE* ta)
{
	if (ta == NULL)
	{
		m_TransferAttr.bServer = false;
		m_TransferAttr.bSend = false;
		m_TransferAttr.bCompress = false;
		m_TransferAttr.iPacketSize = HYPERTRANSFER_PACKETSIZE_DEFAULT;
		m_TransferAttr.nAckPeriod = HYPERTRANSFER_ACKPERIOD_DEFAULT;
		strcpy(m_TransferAttr.strDestIP, "192.168.0.1");
		m_TransferAttr.iTCPPort = HYPERTRANSFER_TCP_PORT;
		m_TransferAttr.iUDPPort = HYPERTRANSFER_UDP_PORT;
	}
	else
	{
		memcpy(&m_TransferAttr, ta, sizeof(HYPERTRANSFER_ATTRIBUTE));
	}
}

//-----------------------------------------------------------------------------//
// Transfer using 3 ~ 4 threads
//-----------------------------------------------------------------------------//
int HYPERTRANSFER::Transfer()
{
	int status;
	int thr_id;

	cout << "HyperTransfer No.3" << endl;
	srand((unsigned)time(NULL));

	// Start TCP thread
	m_pthread[0] = CreateThread(NULL, 0, HYPERTRANSFER::ThreadACK, (void*) this, 0, NULL);
	if (m_pthread[0] < 0)
	{
		perror("ACK thread create error : ");
		exit(0);
	}
	// Start UDP thread
	m_pthread[1] = CreateThread(NULL, 0, HYPERTRANSFER::ThreadUDP, (void*) this, 0, NULL);
	if (m_pthread[0] < 0)
	{
		perror("UDP thread create error : ");
		exit(0);
	}

	// Start I/O thread
	m_pthread[2] = CreateThread(NULL, 0, HYPERTRANSFER::ThreadIO, (void*) this, 0, NULL);
	if (m_pthread[0] < 0)
	{
		perror("I/O thread create error : ");
		exit(0);
	}
	// Start compression thread
	if (m_TransferAttr.bCompress)
	{
		//ThreadCompression
	}

	WaitForMultipleObjects(3, m_pthread, TRUE, INFINITE);
	cout << "Thread end." << endl;
	return 0;
}

//-----------------------------------------------------------------------------//
// TCP thread - TCP communication for ack
//-----------------------------------------------------------------------------//
DWORD WINAPI HYPERTRANSFER::ThreadACK(LPVOID lpParam)
{
	HYPERTRANSFER* transfer;
	transfer = (HYPERTRANSFER*)lpParam;

	cout << "Start ACK thread." << endl;
	transfer->DoACK();
	cout << "End ACK thread." << endl;

	return 0;
}

//-----------------------------------------------------------------------------//
// TCP process - Check ack
//-----------------------------------------------------------------------------//
void  HYPERTRANSFER::DoACK()
{
	WSADATA wsaData;
	SOCKET sock;
	sockaddr_in server_addr, client_addr;
	int clen;
	int iPacketSize;
	int tmp;
	char* buf;
	bool bAckComplete = false;
	int acksize = 0;
	ACKINFO* pData = NULL;
	int len = 0;
	unsigned int nLastCluster = 0; // Last cluster number for receiving
	unsigned int nLastPacket = 0;
	int sendlen = 0;
	int state = 0;

	iPacketSize = sizeof(DATAINFO);
	buf = new char[iPacketSize];
	clen = sizeof(client_addr);
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
	{
		perror("[ACK] WSAStartup error: ");
		return;
	}

	//-------------------------------------------------------//
	// Server socket
	//-------------------------------------------------------//
	if (m_TransferAttr.bServer)
	{
		// Create socket
		if ((sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0)
		{
			perror("[ACK] Socket error: ");
			exit(1);
		}

		len = sizeof(tmp);
		tmp = 1024 * 1472;
		state = setsockopt(sock, SOL_SOCKET, SO_RCVBUF, (char*)&tmp, len);
		state = getsockopt(sock, SOL_SOCKET, SO_RCVBUF, (char*)&tmp, &len);

		tmp = 1024 * 1472;
		state = setsockopt(sock, SOL_SOCKET, SO_SNDBUF, (char*)&tmp, len);
		state = getsockopt(sock, SOL_SOCKET, SO_SNDBUF, (char*)&tmp, &len);

		// Server setting
		memset(&server_addr, 0, sizeof(server_addr));
		server_addr.sin_family = AF_INET;
		server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
		server_addr.sin_port = htons(m_TransferAttr.iTCPPort);

		if (::bind(sock, (struct sockaddr*) &server_addr, sizeof(server_addr)) < 0)
		{
			perror("[ACK] Bind error: ");
			exit(1);
		}

		cout << "[ACK] Ready.\n";

		// Receive message (client info)
		if (recvfrom(sock, buf, iPacketSize, 0, (sockaddr*)&client_addr, &clen) <= 0)
		{
			perror("[ACK] Cannot read data size from client: ");
			return;
		}

		cout << "ACK Connected.\n";
		cout << "[ACK Client Information]\nIP: " << inet_ntoa(client_addr.sin_addr) << "\nPort: " << ntohs(client_addr.sin_port) << endl;
	}
	//-------------------------------------------------------//
	// Client socket
	//-------------------------------------------------------//
	else
	{
		// Create socket
		if ((sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0)
		{
			perror("[ACK] Socket error: ");
			exit(1);
		}

		len = sizeof(tmp);
		tmp = 1024 * 1472;
		state = setsockopt(sock, SOL_SOCKET, SO_RCVBUF, (char*)&tmp, len);
		state = getsockopt(sock, SOL_SOCKET, SO_RCVBUF, (char*)&tmp, &len);
		//cout << "sock recv bufsize: " << tmp << endl;

		tmp = 1024 * 1472;
		state = setsockopt(sock, SOL_SOCKET, SO_SNDBUF, (char*)&tmp, len);
		state = getsockopt(sock, SOL_SOCKET, SO_SNDBUF, (char*)&tmp, &len);
		//cout << "sock send bufsize: " << tmp << endl;

		// Server setting
		client_addr.sin_family = AF_INET;
		client_addr.sin_addr.s_addr = inet_addr(m_TransferAttr.strDestIP);
		client_addr.sin_port = htons(m_TransferAttr.iTCPPort);

		if (connect(sock, (struct sockaddr*)&client_addr, sizeof(client_addr)) < 0)
		{
			perror("[ACK] Connect error: ");
			exit(1);
		}

		// Send message (client info)
		while (send_simul(sock, buf, iPacketSize, 0) <= 0)
		{

		}

		cout << "[ACK] Connected.\n";
		cout << "[ACK Server Information]\nIP: " << inet_ntoa(client_addr.sin_addr) << "\nPort: " << ntohs(client_addr.sin_port) << endl;
	}

	//-------------------------------------------------------//
	// Allocate ack array
	//-------------------------------------------------------//
	m_pAckInfo = new ACKINFO*[HYPERTRANSFER_BUFFERSIZE_MAX];

	Ackidx = 0;

	// First information packet
	//-------------------------------------------------------//
	// Send
	//-------------------------------------------------------//
	if (m_TransferAttr.bSend)
	{
		// *** use m_Mutex
		// Wait if I/O thread is not ready
		if (m_pDataInfo == NULL)
		{
			SuspendThread(m_pthread[0]);
		}

		//-------------------------------------------//
		// Send data information to receiver
		//-------------------------------------------//
		WaitForSingleObject(hMutex, INFINITE);
		while (sendto_simul(sock, (const char*)m_pDataInfo, iPacketSize, 0, (struct sockaddr*) &client_addr, sizeof(client_addr)) <= 0)
		{
		}

		//-------------------------------------------//
		// Wait when UDP communication and send transfered ack
		//-------------------------------------------//
		iPacketSize = (m_pDataInfo->nAckPeriod / 8);
		if (m_pDataInfo->nAckPeriod % 8 != 0)
		{
			++iPacketSize;
		}

		//-------------------------------------------//
		// Initialize ack
		//-------------------------------------------//
		acksize = iPacketSize;
		iPacketSize += sizeof(ACKINFO::nClusterNumber) + sizeof(ACKINFO::cAttribute); // Plus char size(nClusterNumber)

		for (int i = 0; i < HYPERTRANSFER_BUFFERSIZE_MAX; ++i)
		{
			m_pAckInfo[i] = new ACKINFO;
			m_pAckInfo[i]->pAck = new char[acksize];
			nLastClusterNumber = i + 1;
			m_pAckInfo[i]->nClusterNumber = nLastClusterNumber;
			m_pAckInfo[i]->cAttribute = HYPERTRANSFER_ACK_READY;
			memset(&m_pAckInfo[i]->pAck[0], 0x00, acksize);
		}

		// Start transfer
		bDoTransfer = true;
		ResumeThread(m_pthread[1]);
		ResumeThread(m_pthread[2]);

		nLastCluster = (m_pDataInfo->iFileSize / (m_pDataInfo->iPacketSize * m_pDataInfo->nAckPeriod));
		if ((m_pDataInfo->iFileSize % (m_pDataInfo->iPacketSize * m_pDataInfo->nAckPeriod)) != 0)
		{
			++nLastCluster;
		}
		nLastPacket = (m_pDataInfo->iFileSize / m_pDataInfo->iPacketSize);
		if ((m_pDataInfo->iFileSize % m_pDataInfo->iPacketSize) != 0)
		{
			++nLastPacket;
		}
		nLastPacket = (nLastPacket % m_pDataInfo->nAckPeriod) - 1;

		/////////////////////////////////////////////////////////////////
		m_pppRetPacketData = new PACKETDATA**[HYPERTRANSFER_BUFFERSIZE_MAX];
		for (int i = 0; i < HYPERTRANSFER_BUFFERSIZE_MAX; i++)
		{
			m_pppRetPacketData[i] = new PACKETDATA*[m_pDataInfo->nAckPeriod];
			for (int j = 0; j < m_pDataInfo->nAckPeriod; j++)
			{
				m_pppRetPacketData[i][j] = new PACKETDATA;
				m_pppRetPacketData[i][j]->nClusterNumber = 0;
				m_pppRetPacketData[i][j]->iPacketNumber = 0;
				m_pppRetPacketData[i][j]->pData = NULL;
			}
		}
		nDataSize = 0;
		//////////////////////////////////////////////////////////////////////

		ReleaseMutex(hMutex);

		buf = new char[iPacketSize];
		pData = new ACKINFO;
		pData->pAck = new char[acksize];

		while (1)
		{
			// Read missing data information from receiver
			memset(buf, 0x00, iPacketSize);
			tmp = 0;
			while ((iPacketSize - tmp) > 0)
			{
				tmp = recvfrom(sock, buf + tmp, iPacketSize - tmp, 0, (struct sockaddr*) &client_addr, &clen);
				if (tmp < 0)
				{
					perror("Cannot read missing data information from receiver: ");
					closesocket(sock);
					return;
				}
			}

			// Set to ack info
			memcpy(pData, buf, sizeof(ACKINFO) - sizeof(ACKINFO::pAck));
			memcpy(&pData->pAck[0], buf + (sizeof(ACKINFO) - sizeof(ACKINFO::pAck)), acksize);

			WaitForSingleObject(hMutex, INFINITE);

			for (int i = 0; i < HYPERTRANSFER_BUFFERSIZE_MAX; ++i)
			{
				if (m_pAckInfo[i]->nClusterNumber == pData->nClusterNumber)
				{
					Ackidx = i;
					memcpy(m_pAckInfo[i], pData, sizeof(ACKINFO) - sizeof(ACKINFO::pAck));
					memcpy(&m_pAckInfo[i]->pAck[0], &pData->pAck[0], acksize);

					bAck = false;

					if (bAckComplete)
					{
						cout << "[ACK] Deleted packet cluster number: [" << i << ":" << m_pAckInfo[i]->nClusterNumber << "]" << endl;

						WaitForSingleObject(hMutex, INFINITE);

						--nDataSize;
						// Delete packet
						for (int j = 0; j < m_pDataInfo->nAckPeriod; ++j)
						{
							m_pppRetPacketData[i][j]->nClusterNumber = 0;
						}

						// Delete ack
						nLastClusterNumber++;
						m_pAckInfo[i]->nClusterNumber = nLastClusterNumber;
						m_pAckInfo[i]->cAttribute = HYPERTRANSFER_ACK_READY;
						memset(m_pAckInfo[i]->pAck, 0x00, acksize);
						cout << "nLastClusterNumber: " << nLastClusterNumber << endl;

						ReleaseMutex(hMutex);

						ResumeThread(m_pthread[2]);
					}
					else
					{
						bAck = true;
					}
					break;
				}
			}

			if (nLastCluster <= nLastClusterNumber)
			{
				bAckComplete = true;
				for (int i = 0; i < HYPERTRANSFER_BUFFERSIZE_MAX; ++i)
				{
					// Check done
					if (m_pppRetPacketData[i][0]->nClusterNumber != 0)
					{
						bAckComplete = false;
						break;
					}
				}

				if (bAckComplete)
				{
					bDoneTransfer = true;
					cout << "END" << endl;
					TerminateThread(m_pthread[0], 0);
					TerminateThread(m_pthread[1], 0);
					TerminateThread(m_pthread[2], 0);
				}
			}

			ReleaseMutex(hMutex);

			bDoTransfer = true;
			ResumeThread(m_pthread[1]);
		}
	}

	// Receive
	else
	{
		// Read data information from sender
		WaitForSingleObject(hMutex, INFINITE);
		m_pDataInfo = new DATAINFO; // Create m_pDataInfo of DATAINFO size

		cout << "[ACK] Reading...\n";
		if (recv(sock, (char*)m_pDataInfo, iPacketSize, 0) <= 0)
		{
			perror("[ACK] Cannot read data info from server: ");
			closesocket(sock);
			return;
		}

		// Start transfer
		ReleaseMutex(hMutex);

		nDataSize = 0;

		cout << "ACK: Get DATAINFO" << endl;
		cout << "File name: " << m_pDataInfo->strFileName << endl;
		cout << "File size: " << m_pDataInfo->iFileSize << endl;
		cout << "Packet numbers: " << (m_pDataInfo->iFileSize / m_pDataInfo->iPacketSize) << endl;

		iPacketSize = (m_pDataInfo->nAckPeriod / 8);
		if (m_pDataInfo->nAckPeriod % 8 != 0)
		{
			++iPacketSize;
		}

		nLastCluster = (m_pDataInfo->iFileSize / (m_pDataInfo->iPacketSize * m_pDataInfo->nAckPeriod));
		if ((m_pDataInfo->iFileSize % (m_pDataInfo->iPacketSize * m_pDataInfo->nAckPeriod)) != 0)
		{
			++nLastCluster;
		}
		nLastPacket = (m_pDataInfo->iFileSize / m_pDataInfo->iPacketSize);
		if ((m_pDataInfo->iFileSize % m_pDataInfo->iPacketSize) != 0)
		{
			++nLastPacket;
		}
		nLastPacket = (nLastPacket % m_pDataInfo->nAckPeriod) - 1;

		// Initialize ack
		acksize = iPacketSize;
		iPacketSize += sizeof(ACKINFO::nClusterNumber) + sizeof(ACKINFO::cAttribute); // Plus char size(nClusterNumber)

		for (int i = 0; i < HYPERTRANSFER_BUFFERSIZE_MAX; ++i)
		{
			m_pAckInfo[i] = new ACKINFO;
			m_pAckInfo[i]->pAck = new char[acksize];
			nLastClusterNumber = i + 1;
			m_pAckInfo[i]->nClusterNumber = nLastClusterNumber;
			m_pAckInfo[i]->cAttribute = HYPERTRANSFER_ACK_READY;
			//m_pAckInfo[i]->pAck = new char[iPacketSize];
			memset(&m_pAckInfo[i]->pAck[0], 0x00, acksize);
		}

		bDoTransfer = true;
		ResumeThread(m_pthread[1]);
		ResumeThread(m_pthread[2]);

		// Wait when UDP communication and receive transfered result (if UDP sender has done, send checking TCP)
		while (1)
		{
			if (bDoneTransfer)
			{
				break;
			}
			if (bAck == false)
			{
				SuspendThread(m_pthread[0]);
				continue;
			}

			// Send ack to sender

			bAckComplete = true;

			WaitForSingleObject(hMutex, INFINITE);
			if (m_pAckInfo[Ackidx]->nClusterNumber == nLastCluster)
			{
				tmp = nLastPacket + 1;
			}
			else
			{
				tmp = m_pDataInfo->nAckPeriod;
			}
			m_pAckInfo[Ackidx]->cAttribute = HYPERTRANSFER_ACK_DONE;
			ReleaseMutex(hMutex);

			WaitForSingleObject(hMutex, INFINITE);
			for (int i = 0; i < tmp; ++i)
			{
				if (GetBit(m_pAckInfo[Ackidx]->pAck[i / 8], (i % 8)) == 0)
				{
					bAckComplete = false;
					m_pAckInfo[Ackidx]->cAttribute = HYPERTRANSFER_ACK_READY;
					break;
				}
			}
			ReleaseMutex(hMutex);

			// Set to ack info
			buf = new char[iPacketSize];

			WaitForSingleObject(hMutex, INFINITE);
			memcpy(buf, m_pAckInfo[Ackidx], sizeof(ACKINFO) - sizeof(ACKINFO::pAck));
			memcpy(&buf[sizeof(ACKINFO) - sizeof(ACKINFO::pAck)], &m_pAckInfo[Ackidx]->pAck[0], acksize);
			ReleaseMutex(hMutex);

			while (send_simul(sock, buf, iPacketSize, 0) <= 0)
			{
			}

			delete[] buf;
			bAck = false;
		}
	}

	closesocket(sock);

	WSACleanup();
	return;
}

// UDP thread - UDP for sending/receiving data (size: DATAINFO::iPacketSize)
DWORD WINAPI HYPERTRANSFER::ThreadUDP(LPVOID lpParam)
{
	HYPERTRANSFER* transfer;
	transfer = (HYPERTRANSFER*)lpParam;

	cout << "Start UDP thread.\n";
	transfer->DoUDP();
	cout << "End UDP thread.\n";
	return 0;
}

// UDP process - Transfer data
void HYPERTRANSFER::DoUDP()
{
	WSADATA wsaData;
	SOCKET sock;
	int clen;
	sockaddr_in client_addr, server_addr;
	char* buf;
	int sendlen; // send length
	PACKETDATA* pData = NULL;
	int packsize = 0;
	int acksize = 0;
	bool bExist = false;
	bool bAckComplete = false;
	int AckNumber = 1;
	int tmp = 0;
	int tmpAckidx = 0;
	unsigned int nLastCluster = 0; // Last cluster number for receiving
	unsigned int nLastPacket = 0;
	int thiscluster = 0;
	int thispacket = 0;
	static unsigned int idx1 = 0;
	static unsigned int idx2 = 0;
	int len, state;

	while (m_pDataInfo == NULL)
	{
		SuspendThread(m_pthread[1]);
	};

	WaitForSingleObject(hMutex, INFINITE);
	packsize = m_pDataInfo->iPacketSize + sizeof(PACKETDATA::nClusterNumber) + sizeof(PACKETDATA::iPacketNumber);
	buf = new char[packsize];
	clen = sizeof(client_addr);
	acksize = (m_pDataInfo->nAckPeriod / 8);
	if (m_pDataInfo->nAckPeriod % 8 != 0)
	{
		++acksize;
	}
	ReleaseMutex(hMutex);

	nLastCluster = (m_pDataInfo->iFileSize / (m_pDataInfo->iPacketSize * m_pDataInfo->nAckPeriod));
	if ((m_pDataInfo->iFileSize % (m_pDataInfo->iPacketSize * m_pDataInfo->nAckPeriod)) != 0)
	{
		++nLastCluster;
	}
	nLastPacket = (m_pDataInfo->iFileSize / m_pDataInfo->iPacketSize);
	if ((m_pDataInfo->iFileSize % m_pDataInfo->iPacketSize) != 0)
	{
		++nLastPacket;
	}
	nLastPacket = (nLastPacket % m_pDataInfo->nAckPeriod) - 1;

	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
	{
		perror("[UDP] WSAStartup error: ");
		return;
	}

	// Server socket
	if (m_TransferAttr.bServer)
	{
		// Create socket
		if ((sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0)
		{
			perror("[UDP] Socket error: ");
			exit(1);
		}

		len = sizeof(tmp);
		tmp = 1024 * 1472;
		state = setsockopt(sock, SOL_SOCKET, SO_RCVBUF, (char*)&tmp, len);
		state = getsockopt(sock, SOL_SOCKET, SO_RCVBUF, (char*)&tmp, &len);

		tmp = 1024 * 1472;
		state = setsockopt(sock, SOL_SOCKET, SO_SNDBUF, (char*)&tmp, len);
		state = getsockopt(sock, SOL_SOCKET, SO_SNDBUF, (char*)&tmp, &len);

		// Server setting
		memset(&server_addr, 0, sizeof(server_addr));
		server_addr.sin_family = AF_INET;
		server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
		server_addr.sin_port = htons(m_TransferAttr.iUDPPort);

		if (::bind(sock, (struct sockaddr*) &server_addr, sizeof(server_addr)) < 0)
		{
			perror("[UDP] Bind error: ");
			exit(1);
		}

		cout << "[UDP] Ready.\n";

		// Receive message (client info)
		if (recvfrom(sock, (char*)buf, packsize, 0, (sockaddr*)&client_addr, &clen) <= 0)
		{
			perror("[UDP] Cannot read data size from client: ");
			return;
		}

		cout << "UDP Connected.\n";
		cout << "[UDP Client Information]\nIP: " << inet_ntoa(client_addr.sin_addr) << "\nPort: " << ntohs(client_addr.sin_port) << endl;
	}
	// Client socket
	else
	{
		// Create socket
		if ((sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0)
		{
			perror("[UDP] Socket error: ");
			exit(1);
		}

		len = sizeof(tmp);
		tmp = 1024 * 1472;
		state = setsockopt(sock, SOL_SOCKET, SO_RCVBUF, (char*)&tmp, len);
		state = getsockopt(sock, SOL_SOCKET, SO_RCVBUF, (char*)&tmp, &len);

		tmp = 1024 * 1472;
		state = setsockopt(sock, SOL_SOCKET, SO_SNDBUF, (char*)&tmp, len);
		state = getsockopt(sock, SOL_SOCKET, SO_SNDBUF, (char*)&tmp, &len);

		// Server setting
		client_addr.sin_family = AF_INET;
		client_addr.sin_addr.s_addr = inet_addr(m_TransferAttr.strDestIP);
		client_addr.sin_port = htons(m_TransferAttr.iUDPPort);

		if (connect(sock, (struct sockaddr*)&client_addr, sizeof(client_addr)) < 0)
		{
			perror("[ACK] Connect error: ");
			exit(1);
		}

		// Send message (client info)
		while (sendto_simul(sock, buf, packsize, 0, (struct sockaddr*) &client_addr, sizeof(client_addr)) <= 0)
		{

		}

		cout << sendlen << endl;
		cout << "[UDP] Connected.\n";
		cout << "[UDP Server Information]\nIP: " << inet_ntoa(client_addr.sin_addr) << "\nPort: " << ntohs(client_addr.sin_port) << endl;
	}

	// Send
	if (m_TransferAttr.bSend)
	{
		sendlen = 0;

		// Transfer data
		while (1)
		{
			// Wait TCP ack
			if (bDoTransfer == false) SuspendThread(m_pthread[1]);

			if (bDoneTransfer == true)//if (nDataSize >= HYPERTRANSFER_BUFFERSIZE_MAX)
			{
				cout << "Transfer: Done to send." << endl;
				break;
			}
			else
			{
				// Send missing data
				if (bAck && m_PacketList.size() <= 0)
				{
					idx1 = 0;
					idx2 = 0;
					WaitForSingleObject(hMutex, INFINITE);
					tmpAckidx = Ackidx;
					bAck = false;
					ReleaseMutex(hMutex);

					if (m_pAckInfo[tmpAckidx]->nClusterNumber == nLastCluster)
					{
						tmp = nLastPacket;
					}
					else
					{
						tmp = m_pDataInfo->nAckPeriod;
					}

					for (int i = 0; i < tmp; ++i)
					{
						WaitForSingleObject(hMutex, INFINITE);
						if (m_PacketList.size() > 0)
						{
							ReleaseMutex(hMutex);
							break;
						}
						if (m_pppRetPacketData[tmpAckidx][i]->nClusterNumber <= 0)
						{
							ReleaseMutex(hMutex);
							break;
						}

						if (GetBit(m_pAckInfo[tmpAckidx]->pAck[i / 8], (i % 8)) == 0)
						{
							memcpy(buf, m_pppRetPacketData[tmpAckidx][i], sizeof(PACKETDATA) - sizeof(PACKETDATA::pData));
							memcpy(&buf[sizeof(PACKETDATA) - sizeof(PACKETDATA::pData)], m_pppRetPacketData[tmpAckidx][i]->pData, m_pDataInfo->iPacketSize);

							sendlen = sendto_simul(sock, buf, packsize, 0, (struct sockaddr*) &client_addr, sizeof(client_addr));
						}
						ReleaseMutex(hMutex);
					}
					continue;
				}
				// If it don't receive acks
				else if (bAck == false && m_PacketList.size() <= 0)
				{
					if (m_pppRetPacketData[idx1][idx2]->nClusterNumber > 0)
					{
						if (GetBit(m_pAckInfo[idx1]->pAck[idx2 / 8], (idx2 % 8)) == 0)
						{
							WaitForSingleObject(hMutex, INFINITE);
							m_PacketList.push_back(m_pppRetPacketData[idx1][idx2]);
							ReleaseMutex(hMutex);
						}
					}

					idx2 = (idx2 + 1) % m_pDataInfo->nAckPeriod;
					if (idx2 == 0)
					{
						idx1 = (idx1 + 1) % HYPERTRANSFER_BUFFERSIZE_MAX;
					}
					continue;
				}

				WaitForSingleObject(hMutex, INFINITE);
				if (m_PacketList.size() <= 0)
					continue;

				memcpy(buf, m_PacketList.front(), sizeof(PACKETDATA) - sizeof(PACKETDATA::pData));
				memcpy(buf + sizeof(PACKETDATA) - sizeof(PACKETDATA::pData), m_PacketList.front()->pData, m_pDataInfo->iPacketSize);

				m_PacketList.pop_front();
				ReleaseMutex(hMutex);

				sendlen = sendto_simul(sock, buf, packsize, 0, (struct sockaddr*) &client_addr, sizeof(client_addr));
			}
		}
	}
	// Receive
	else
	{
		while (1)
		{
			if (bDoTransfer == false) SuspendThread(m_pthread[1]);

			if (recvfrom(sock, buf, packsize, 0, (struct sockaddr*) &client_addr, &clen) <= 0)
			{
				perror("[UDP] Cannot read data: ");
				continue;
			}

			WaitForSingleObject(hMutex, INFINITE);
			pData = new PACKETDATA;
			pData->pData = new char[m_pDataInfo->iPacketSize];
			memcpy(pData, buf, sizeof(PACKETDATA) - sizeof(PACKETDATA::pData));
			memcpy(&pData->pData[0], &buf[sizeof(PACKETDATA) - sizeof(PACKETDATA::pData)], m_pDataInfo->iPacketSize);
			thiscluster = pData->nClusterNumber;
			thispacket = pData->iPacketNumber;
			ReleaseMutex(hMutex);

			if (thiscluster == 0)
			{
				delete[] pData->pData;
				delete[] pData;
				continue;
			}

			// Check ack
			bExist = true;
			bAckComplete = true;

			for (int i = 0; i < HYPERTRANSFER_BUFFERSIZE_MAX; ++i)
			{
				if (m_pAckInfo[i]->nClusterNumber == pData->nClusterNumber)
				{
					bAckComplete = false;

					// Re-transfer completed ack
					if (m_pAckInfo[i]->cAttribute == HYPERTRANSFER_ACK_DONE)
					{
						WaitForSingleObject(hMutex, INFINITE);
						bAck = true;
						Ackidx = i;
						ReleaseMutex(hMutex);
						ResumeThread(m_pthread[0]);
						break;
					}
				}
			}

			while (bAck)
			{
				ResumeThread(m_pthread[0]);
			}

			// Delete ack and make new ack
			if (bAckComplete)
			{
				for (int i = 0; i < HYPERTRANSFER_BUFFERSIZE_MAX; ++i)
				{
					// Re-transfer completed ack
					if (m_pAckInfo[i]->cAttribute == HYPERTRANSFER_ACK_DONE)
					{
						WaitForSingleObject(hMutex, INFINITE);

						cout << "[ACK] Deleted packet cluster number: [" << m_pAckInfo[i]->nClusterNumber << "]" << endl;
						nLastClusterNumber++;
						m_pAckInfo[i]->nClusterNumber = nLastClusterNumber;
						m_pAckInfo[i]->cAttribute = HYPERTRANSFER_ACK_READY;
						memset(m_pAckInfo[i]->pAck, 0x00, acksize);

						ReleaseMutex(hMutex);

						break;
					}
				}
				delete[] pData->pData;
				delete[] pData;
				continue;
			}

			// If have it, throw this packet
			for (int i = 0; i < HYPERTRANSFER_BUFFERSIZE_MAX; ++i)
			{
				if (m_pAckInfo[i]->nClusterNumber == pData->nClusterNumber)
				{
					// Not received yet
					if (GetBit(m_pAckInfo[i]->pAck[pData->iPacketNumber / 8], pData->iPacketNumber % 8) == 0)
					{
						WaitForSingleObject(hMutex, INFINITE);

						// Add packet list
						m_PacketList.push_back(pData);
						// Change ack bit
						SetBit(&(m_pAckInfo[i]->pAck[pData->iPacketNumber / 8]), pData->iPacketNumber % 8, 1);

						ReleaseMutex(hMutex);

						if (m_PacketList.size() >= 1) ResumeThread(m_pthread[2]);
						bExist = false;
						break;
					}
				}
			}

			if (bExist)
			{
				delete[] pData->pData;
				delete[] pData;
			}

			// Send ack
			if ((AckNumber != thiscluster && bAck == false) || (nLastCluster == thiscluster && nLastPacket == thispacket))
			{
				WaitForSingleObject(hMutex, INFINITE);
				bAck = true;

				cout << "Sedner's cluster Number: " << thiscluster << endl;

				for (int i = 0; i < HYPERTRANSFER_BUFFERSIZE_MAX; ++i)
				{
					if (m_pAckInfo[i]->nClusterNumber == AckNumber)
					{
						Ackidx = i;
						break;
					}
				}
				AckNumber = thiscluster;
				ReleaseMutex(hMutex);
				ResumeThread(m_pthread[0]);
			}
		}
	}

	closesocket(sock);
	delete[] buf;
	WSACleanup();
	return;
}

// Compression thread - Compress memory (size: DATAINFO::iBufferSize)
void* HYPERTRANSFER::ThreadCompression(void * arg)
{
	return ((void *)0);
}

// I/O thread - File read/write (size: DATAINFO::iBufferSize)
DWORD WINAPI HYPERTRANSFER::ThreadIO(LPVOID lpParam)
{
	HYPERTRANSFER* transfer;
	transfer = (HYPERTRANSFER*)lpParam;

	cout << "Start I/O thread.\n";
	transfer->DoIO();
	cout << "End I/O thread.\n";

	return 0;
}

// I/O process - Read or write file
void HYPERTRANSFER::DoIO()
{
	int iBytes = 0;
	unsigned char* buf;
	FILE *fp;
	int tmp = 0;
	int prevNum = 0;
	int nClusterCount[HYPERTRANSFER_BUFFERSIZE_MAX] = { 0 };
	unsigned int nLastCluster = 0;
	unsigned int nLastPacket = 0;
	int idx1 = 0;
	int idx2 = 0;
	bool bInitIter = false;
	PACKETDATA* pd = NULL;

	// Send - Read file and write memory
	if (m_TransferAttr.bSend)
	{
		// Open file
		fp = fopen(m_strFileName, "rb");
		if (fp == NULL)
		{
			cout << "File don't exist: " << m_strFileName << endl;
			exit(1);
		}

		// Initialize m_pDataInfo
		WaitForSingleObject(hMutex, INFINITE);

		m_pDataInfo = new DATAINFO;

		fseek(fp, 0, SEEK_END);
		m_pDataInfo->bCompress = m_TransferAttr.bCompress;
		strcpy(m_pDataInfo->strFileName, m_strFileName);
		m_pDataInfo->iFileSize = ftell(fp);
		m_pDataInfo->iPacketSize = m_TransferAttr.iPacketSize;
		m_pDataInfo->nAckPeriod = m_TransferAttr.nAckPeriod;

		ReleaseMutex(hMutex);

		ResumeThread(m_pthread[0]);

		if (bDoTransfer == false)
		{
			SuspendThread(m_pthread[2]);
		}

		// Read file and add packet list
		for (int i = 0; i < (m_pDataInfo->iFileSize / m_pDataInfo->iPacketSize) + 1; ++i)
		{
			iBytes += ReadFileAndAddPacketList(fp, i, m_pDataInfo->iPacketSize);
		}

		bFileEnd = true;
	}
	// Receive - Read memory and write file
	else
	{
		if (bDoTransfer == false)
		{
			SuspendThread(m_pthread[2]);
		}

		nLastCluster = (m_pDataInfo->iFileSize / (m_pDataInfo->iPacketSize * m_pDataInfo->nAckPeriod));
		if ((m_pDataInfo->iFileSize % (m_pDataInfo->iPacketSize * m_pDataInfo->nAckPeriod)) != 0)
		{
			++nLastCluster;
		}
		nLastPacket = (m_pDataInfo->iFileSize / m_pDataInfo->iPacketSize);
		if ((m_pDataInfo->iFileSize % m_pDataInfo->iPacketSize) != 0)
		{
			++nLastPacket;
		}
		nLastPacket = (nLastPacket % m_pDataInfo->nAckPeriod) - 1;

		fp = fopen(m_strFileName, "wb");
		if (fp == NULL)
		{
			cout << "Can't make a file.\n";
			return;
		}

		// Write file from receiving data
		while (1)
		{
			if (m_PacketList.size() <= 0)
			{
				if (bDoneTransfer == true)
				{
					break;
				}
				else
				{
					// Wait
					SuspendThread(m_pthread[2]);
					continue;
				}
			}

			WaitForSingleObject(hMutex, INFINITE);

			// Get data from memory list
			pd = m_PacketList.front();
			m_PacketList.pop_front();
			ReleaseMutex(hMutex);

			tmp = (pd->nClusterNumber - 1) * m_pDataInfo->iPacketSize * m_pDataInfo->nAckPeriod;
			tmp += pd->iPacketNumber * m_pDataInfo->iPacketSize;

			if (fseek(fp, tmp, SEEK_SET) < 0)
			{
				cout << "fseek error: " << tmp << endl;
				break;
			}

			if ((pd->nClusterNumber == nLastCluster) && (pd->iPacketNumber == nLastPacket))
			{
				tmp = m_pDataInfo->iFileSize % m_pDataInfo->iPacketSize;
			}
			else
			{
				tmp = m_pDataInfo->iPacketSize;
			}
			iBytes += fwrite(pd->pData, sizeof(char), tmp, fp);
			if (iBytes >= m_pDataInfo->iFileSize)
			{
				bDoneTransfer = true;
				ResumeThread(m_pthread[0]);
				TerminateThread(m_pthread[1], 0);
			}

			delete[] pd->pData;
			delete[] pd;
		}
	}

	return;
}

// ReadFileAndAddPacketList (PacketNum: FILE::pos / m_pDataInfo->iPacketSize, ReadSize: To read bytes)
int HYPERTRANSFER::ReadFileAndAddPacketList(FILE * fp, int PacketNum, int ReadSize)
{
	int iReadBytes = 0;
	char* buf = NULL;
	PACKETDATA* pd;
	static unsigned int idx1 = 0;
	static unsigned int idx2 = 0;
	static unsigned int ClusterNum = 1;
	bool bFind = false;

	if (fseek(fp, PacketNum * m_pDataInfo->iPacketSize, SEEK_SET) < 0)
	{
		cout << "[I/O] fseek error: " << PacketNum << endl;
		return 0;
	}

	buf = new char[ReadSize];

	iReadBytes = fread(buf, sizeof(char), ReadSize, fp);
	if (iReadBytes <= 0)
	{
		cout << "[I/O] Read error" << endl;
		return iReadBytes;
	}
	// If we need to compress *** NOT USED ***
	if (m_pDataInfo->bCompress) // Send raw data(char*) to compression thread
	{
		// NOT USED
		return 0;
	}
	else
	{
		while (1)
		{
			// Write data to memory(m_pppRetPacketData)
			if (m_pppRetPacketData[idx1][idx2]->nClusterNumber == 0)
			{
				WaitForSingleObject(hMutex, INFINITE);

				m_pppRetPacketData[idx1][idx2]->nClusterNumber = ClusterNum;
				m_pppRetPacketData[idx1][idx2]->iPacketNumber = idx2;
				if (m_pppRetPacketData[idx1][idx2]->pData == NULL)
				{
					m_pppRetPacketData[idx1][idx2]->pData = buf;
				}
				else
				{
					memcpy(m_pppRetPacketData[idx1][idx2]->pData, buf, ReadSize);
					delete[] buf;
				}

				// Send transformed packet data(PACKETDATA*) to UDP thread
				// Add packet list
				m_PacketList.push_back(m_pppRetPacketData[idx1][idx2]);
				if (m_PacketList.size() == 1)
				{
					ResumeThread(m_pthread[1]);
				}

				ReleaseMutex(hMutex);

				idx2 = (idx2 + 1) % m_pDataInfo->nAckPeriod;
				if (idx2 == 0)
				{
					idx1 = (idx1 + 1) % HYPERTRANSFER_BUFFERSIZE_MAX;
					++ClusterNum;
				}
				break;
			}
			else
			{
				bFind = false;

				for (int i = 0; i < HYPERTRANSFER_BUFFERSIZE_MAX; i++)
				{
					if (m_pppRetPacketData[i][0]->nClusterNumber == 0)
					{
						idx1 = i;
						idx2 = 0;
						bFileEnd = false;
						bFind = true;
						break;
					}
				}

				if (bFind == false)
				{
					SuspendThread(m_pthread[2]);
					continue;
				}
			}
		}
	}
	return iReadBytes;
}

// Get bit
inline int HYPERTRANSFER::GetBit(int x, int n)
{
	return (x & (1 << n)) >> n;
}

// Set bit
inline void HYPERTRANSFER::SetBit(char* x, char n, unsigned char data)
{
	*x |= data << n;

	if (data == 0)
	{
		*x = *x & (~(1 << n));

	}
	else
	{
		*x = *x | (1 << n);
	}
}

int send_simul(SOCKET s, const char* buf, int len, int flags)
{
	int packet_len = 0;
	if (rand() % 20 != 1)
	{ // 약 95프로 
		packet_len = send(s, buf, len, flags);
	}
	return packet_len;
}

int sendto_simul(SOCKET s, const char* buf, int len, int flags, const sockaddr *to, int tolen)
{
	int packet_len = 0;
	if (rand() % 20 != 1) { // 약 95프로 
		packet_len = sendto(s, buf, len, flags, to, tolen);
	}
	return packet_len;
}
