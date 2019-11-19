#pragma once

//设置WinSocket环境并初始化
class WinsockEnv{
private:
	WinsockEnv(void);
	~WinsockEnv(void);
public:
	static int Startup();
};

