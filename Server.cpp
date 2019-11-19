#include <iostream>
#include <string>
#include <algorithm>
#include "Server.h"
#include "WinsockEnv.h"
#include "Config.h"
#include <winsock2.h>
#pragma comment(lib, "Ws2_32.lib")

//���캯������ʼ��ָ�����
Server::Server(void){
	this->recvBuf = new char[Config::BUFFERLENGTH];		//��ʼ�����յ�������
	memset(this->recvBuf, '\0', Config::BUFFERLENGTH);

	this->rcvedMessages = new list<string>();
	this->sessions = new list<SOCKET>();
	this->closedSessions = new list<SOCKET>();
	this->clientAddrMaps = new map<SOCKET, string>();
}

//��������
Server::~Server(void){
	//�ر�server socket
	if (this->srvSocket != NULL) {
		closesocket(this->srvSocket);
		this->srvSocket = NULL;
	}

	//�ͷŽ��ܻ�����
	if (this->recvBuf != NULL) {
		delete this->recvBuf;
		this->recvBuf = NULL;
	}

	//�ر����лỰsocket���ͷŻỰ����
	if (this->sessions != NULL) {
		for (list<SOCKET>::iterator itor = this->sessions->begin(); itor != this->sessions->end(); itor++)
			closesocket(*itor);			//�رջỰ
		delete this->sessions;			//�ͷŶ���
		this->sessions = NULL;			//�ÿ�ָ��  ##������
	}
	//�ͷ�ʧЧ�Ự����
	if (this->closedSessions != NULL) {
		for (list<SOCKET>::iterator itor = this->closedSessions->begin(); itor != this->closedSessions->end(); itor++)
			closesocket(*itor);			//�رջỰ
		delete this->closedSessions;	//�ͷŶ���
		this->closedSessions = NULL;
	}

	//�ͷŽ�����Ϣ����
	if (this->rcvedMessages != NULL) {
		this->rcvedMessages->clear();	//�����Ϣ�����е���Ϣ
		delete this->rcvedMessages;		// �ͷ���Ϣ����
		this->rcvedMessages = NULL;
	}

	//�ͷſͻ��˵�ַ�б�
	if (this->clientAddrMaps != NULL) {
		this->clientAddrMaps->clear();
		delete this->clientAddrMaps;
		this->clientAddrMaps = NULL;
	}

	WSACleanup();						//����winsock ���л���
}

//��ʼ��Winsock
int Server::WinsockStartup() {
	if (WinsockEnv::Startup() == -1) return -1;	//��ʼ��Winsock
	return 0;
}

//��ʼ��Server����������sockect���󶨵�IP��PORT
int Server::ServerStartup() {
	//���� TCP socket
	this->srvSocket = socket(AF_INET, SOCK_STREAM, 0);	//address family, type of socket, specific protocol
	if (this->srvSocket == INVALID_SOCKET) {
		cout << "Server socket creare error !\n";
		WSACleanup();
		return -1;
	}
	cout << "Server socket create ok!\n";

	//���÷�����IP��ַ�Ͷ˿ں�
	this->srvAddr.sin_family = AF_INET;
	this->srvAddr.sin_port = htons(Config::PORT);	//
	this->srvAddr.sin_addr.S_un.S_addr = htonl(INADDR_ANY);//���Զ��ҵ����������ʵ�IP��ַ��һ̨�������ܶ������
		// this->srvAddr.sin_addr.S_un.S_addr = inet_addr(Config::SERVERADDRESS.c_str()); //��������һ������IP��ַ�ķ���

	//�� socket to Server's IP and port
	int rtn = bind(this->srvSocket, (LPSOCKADDR) & (this->srvAddr), sizeof(this->srvAddr));
	if (rtn == SOCKET_ERROR) {
		cout << "Server socket bind error!\n";
		closesocket(this->srvSocket);	//�ص���ǰsocket
		WSACleanup();	//���winsocket����
		return -1;
	}
	cout << "Server socket bind ok!\n";
	return 0;
}

//��ʼ����,�ȴ��ͻ�����������
int Server::ListenStartup() {
	int rtn = listen(this->srvSocket, Config::MAXCONNECTION);	// MAXCONNECTION �������������ŶӸ���
	if (rtn == SOCKET_ERROR) {
		cout << "Server socket listen error!\n";
		closesocket(this->srvSocket);
		WSACleanup();
		return -1;
	}
	cout << "Server socket listen ok!\n";
	return 0;
}

//���ܿͻ��˷�������������ݲ�ת��
int Server::Loop() {
	//��srvSock��Ϊ������ģʽ�Լ����ͻ���������
	u_long blockMode = Config::BLOCKMODE;		
	int rtn;
	if ((rtn = ioctlsocket(this->srvSocket, FIONBIO, &blockMode) == SOCKET_ERROR)) { //FIONBIO��������ֹ�׽ӿ�s�ķ�����ģʽ��
		cout << "ioctlsocket() failed with error!\n";
		return -1;
	}
	cout << "ioctlsocket() for server socket ok!Waiting for client connection and data\n";

	//�ȴ��ͻ�����������
	while (true) { 
		//���ȴӻỰSOCKET������ɾ�����Ѿ��رյĻỰsocket
		this->RemoveClosedSession();

		//Prepare the read and write socket sets for network I/O notification.
		//���read, write �׽��ּ���
		FD_ZERO(&this->rfds);
		FD_ZERO(&this->wfds);

		//��srvSocket���뵽rfds���ȴ��û���������
		FD_SET(this->srvSocket, &this->rfds);

		//�ѵ�ǰ�����лỰsocket���뵽rfds,�ȴ��û����ݵĵ���;�ӵ�wfds���ȴ�socket�ɷ�������
		for (list<SOCKET>::iterator itor = this->sessions->begin(); itor != this->sessions->end(); itor++) {
			FD_SET(*itor, &rfds);
			FD_SET(*itor, &wfds);
		}

		//�ȴ��û�����������û����ݵ�����Ựsocket�ɷ�������
		if ((this->numOfSocketSignaled = select(0, &this->rfds, &this->wfds, NULL, NULL)) == SOCKET_ERROR) { //select���������пɶ����д��socket��������������rtn��.���һ�������趨�ȴ�ʱ�䣬��ΪNULL��Ϊ����ģʽ
			cout << "select() failed with error!\n";
			return -1;
		}

		//select���õ�������ģʽ
		//���������е������ζ�����û������������������û����ݵ��������лỰsocket���Է�������

		//���ȼ���Ƿ��пͻ��������ӵ���
		if (this->AcceptRequestionFromClient() != 0) return -1;

		//Ȼ����ͻ��˷�������
		this->ForwardMessage();

		//�����ܿͻ��˷���������
		this->ReceieveMessageFromClients();
	}

	return 0;
}


/* һ����һЩ�Զ���Ĳ��� */

//���յ��Ŀͻ�����Ϣ���浽��Ϣ����
void Server::AddRecvMessage(string str) {

}

//���µĻỰsocket�������
void Server::AddSession(SOCKET session) {

}

//��ʧЧ�ĻỰsocket�������
void Server::AddClosedSession(SOCKET session) {

}

//��ʧЧ��SOCKET�ӻỰsocket����ɾ��
void Server::RemoveClosedSession(SOCKET closedSession) {

}

//��ʧЧ��SOCKET�ӻỰsocket����ɾ��
void Server::RemoveClosedSession() {

}

//�������ͻ�ת����Ϣ
void Server::ForwardMessage() {
}

//��SOCKET s������Ϣ
void Server::recvMessage(SOCKET s) {

}

//��SOCKET s������Ϣ
void Server::sendMessage(SOCKET s, string msg) {

}

//�õ��ͻ���IP��ַ
string  Server::GetClientAddress(SOCKET s) {
	return "000";
}

//�õ��ͻ���IP��ַ
string  Server::GetClientAddress(map<SOCKET, string>* maps, SOCKET s) {
	return "000";
}

//���ܿͻ��˷�������Ϣ
void  Server::ReceieveMessageFromClients() {

}

//�ȴ��ͻ�����������
int Server::AcceptRequestionFromClient() {
	return 0;
}