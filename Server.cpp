#include <iostream>
#include <string>
#include <algorithm>
#include "Server.h"
#include "WinsockEnv.h"
#include "Config.h"
#include <winsock2.h>
#pragma comment(lib, "Ws2_32.lib")

//构造函数，初始化指针变量
Server::Server(void){
	this->recvBuf = new char[Config::BUFFERLENGTH];		//初始化接收到缓冲区
	memset(this->recvBuf, '\0', Config::BUFFERLENGTH);

	this->rcvedMessages = new list<string>();
	this->sessions = new list<SOCKET>();
	this->closedSessions = new list<SOCKET>();
	this->clientAddrMaps = new map<SOCKET, string>();
}

//析构函数
Server::~Server(void){
	//关闭server socket
	if (this->srvSocket != NULL) {
		closesocket(this->srvSocket);
		this->srvSocket = NULL;
	}

	//释放接受缓冲区
	if (this->recvBuf != NULL) {
		delete this->recvBuf;
		this->recvBuf = NULL;
	}

	//关闭所有会话socket并释放会话队列
	if (this->sessions != NULL) {
		for (list<SOCKET>::iterator itor = this->sessions->begin(); itor != this->sessions->end(); itor++)
			closesocket(*itor);			//关闭会话
		delete this->sessions;			//释放队列
		this->sessions = NULL;			//置空指针  ##三连！
	}
	//释放失效会话队列
	if (this->closedSessions != NULL) {
		for (list<SOCKET>::iterator itor = this->closedSessions->begin(); itor != this->closedSessions->end(); itor++)
			closesocket(*itor);			//关闭会话
		delete this->closedSessions;	//释放队列
		this->closedSessions = NULL;
	}

	//释放接受消息队列
	if (this->rcvedMessages != NULL) {
		this->rcvedMessages->clear();	//清除消息队列中的消息
		delete this->rcvedMessages;		// 释放消息队列
		this->rcvedMessages = NULL;
	}

	//释放客户端地址列表
	if (this->clientAddrMaps != NULL) {
		this->clientAddrMaps->clear();
		delete this->clientAddrMaps;
		this->clientAddrMaps = NULL;
	}

	WSACleanup();						//清理winsock 运行环境
}

//初始化Winsock
int Server::WinsockStartup() {
	if (WinsockEnv::Startup() == -1) return -1;	//初始化Winsock
	return 0;
}

//初始化Server，包括创建sockect，绑定到IP和PORT
int Server::ServerStartup() {
	//创建 TCP socket
	this->srvSocket = socket(AF_INET, SOCK_STREAM, 0);	//address family, type of socket, specific protocol
	if (this->srvSocket == INVALID_SOCKET) {
		cout << "Server socket creare error !\n";
		WSACleanup();
		return -1;
	}
	cout << "Server socket create ok!\n";

	//设置服务器IP地址和端口号
	this->srvAddr.sin_family = AF_INET;
	this->srvAddr.sin_port = htons(Config::PORT);	//
	this->srvAddr.sin_addr.S_un.S_addr = htonl(INADDR_ANY);//会自动找到服务器合适的IP地址，一台机器可能多个网卡
		// this->srvAddr.sin_addr.S_un.S_addr = inet_addr(Config::SERVERADDRESS.c_str()); //这是另外一种设置IP地址的方法

	//绑定 socket to Server's IP and port
	int rtn = bind(this->srvSocket, (LPSOCKADDR) & (this->srvAddr), sizeof(this->srvAddr));
	if (rtn == SOCKET_ERROR) {
		cout << "Server socket bind error!\n";
		closesocket(this->srvSocket);	//关掉当前socket
		WSACleanup();	//清空winsocket环境
		return -1;
	}
	cout << "Server socket bind ok!\n";
	return 0;
}

//开始监听,等待客户的连接请求
int Server::ListenStartup() {
	int rtn = listen(this->srvSocket, Config::MAXCONNECTION);	// MAXCONNECTION 设置最大的申请排队个数
	if (rtn == SOCKET_ERROR) {
		cout << "Server socket listen error!\n";
		closesocket(this->srvSocket);
		WSACleanup();
		return -1;
	}
	cout << "Server socket listen ok!\n";
	return 0;
}

//接受客户端发来的请求和数据并转发
int Server::Loop() {
	//将srvSock设为非阻塞模式以监听客户连接请求
	u_long blockMode = Config::BLOCKMODE;		
	int rtn;
	if ((rtn = ioctlsocket(this->srvSocket, FIONBIO, &blockMode) == SOCKET_ERROR)) { //FIONBIO：允许或禁止套接口s的非阻塞模式。
		cout << "ioctlsocket() failed with error!\n";
		return -1;
	}
	cout << "ioctlsocket() for server socket ok!Waiting for client connection and data\n";

	//等待客户的连接请求
	while (true) { 
		//首先从会话SOCKET队列中删除掉已经关闭的会话socket
		this->RemoveClosedSession();

		//Prepare the read and write socket sets for network I/O notification.
		//清空read, write 套接字集合
		FD_ZERO(&this->rfds);
		FD_ZERO(&this->wfds);

		//把srvSocket加入到rfds，等待用户连接请求
		FD_SET(this->srvSocket, &this->rfds);

		//把当前的所有会话socket加入到rfds,等待用户数据的到来;加到wfds，等待socket可发送数据
		for (list<SOCKET>::iterator itor = this->sessions->begin(); itor != this->sessions->end(); itor++) {
			FD_SET(*itor, &rfds);
			FD_SET(*itor, &wfds);
		}

		//等待用户连接请求或用户数据到来或会话socket可发送数据
		if ((this->numOfSocketSignaled = select(0, &this->rfds, &this->wfds, NULL, NULL)) == SOCKET_ERROR) { //select函数返回有可读或可写的socket的总数，保存在rtn里.最后一个参数设定等待时间，如为NULL则为阻塞模式
			cout << "select() failed with error!\n";
			return -1;
		}

		//select设置的是阻塞模式
		//当程序运行到这里，意味着有用户连接请求到来，或有用户数据到来，或有会话socket可以发送数据

		//首先检查是否有客户请求连接到来
		if (this->AcceptRequestionFromClient() != 0) return -1;

		//然后向客户端发送数据
		this->ForwardMessage();

		//最后接受客户端发来的数据
		this->ReceieveMessageFromClients();
	}

	return 0;
}


/* 一下是一些自定义的操作 */

//将收到的客户端消息保存到消息队列
void Server::AddRecvMessage(string str) {

}

//将新的会话socket加入队列
void Server::AddSession(SOCKET session) {

}

//将失效的会话socket加入队列
void Server::AddClosedSession(SOCKET session) {

}

//将失效的SOCKET从会话socket队列删除
void Server::RemoveClosedSession(SOCKET closedSession) {

}

//将失效的SOCKET从会话socket队列删除
void Server::RemoveClosedSession() {

}

//向其他客户转发信息
void Server::ForwardMessage() {
}

//从SOCKET s接受消息
void Server::recvMessage(SOCKET s) {

}

//向SOCKET s发送消息
void Server::sendMessage(SOCKET s, string msg) {

}

//得到客户端IP地址
string  Server::GetClientAddress(SOCKET s) {
	return "000";
}

//得到客户端IP地址
string  Server::GetClientAddress(map<SOCKET, string>* maps, SOCKET s) {
	return "000";
}

//接受客户端发来的信息
void  Server::ReceieveMessageFromClients() {

}

//等待客户端连接请求
int Server::AcceptRequestionFromClient() {
	return 0;
}