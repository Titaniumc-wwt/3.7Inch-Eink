// char sta_ssid[32] = {0};
// char sta_password[64] = {0};
//调用下面两个
void initBasic();   //设置wifi名称
void connectNewWifi();  //尝试连接已保存的wifi，未成功连接的话就开启配网
//连上后调用这个
void WifiConnect();
//退出配网模式调用这个
bool WifiDisConnect();
//获取已保存wifi账号
char* TransportSSID();
//获取已保存wifi账号下的密码
char* TransportKeyWord();
//返回是否成功连上网络（用于判断wifi账号密码，或者信号是否稳定是使用）
bool IsConnectOK();
//普通联网，需要提前判断是否进行过配网 返回值false->连接失败 true->连接成功
bool WifiEasyConnect();
//简单的获取网络时间
void SyncTime();

void initSoftAP();  //初始化AP
void initWebServer();   //初始化websever
void initDNS();    //初始化DNS


//如果配网模式连接上wifi需要让这两个函数持续跑
////   server.handleClient();
//   dnsServer.processNextRequest();

//获取SSID和Keywords
void GetWIFISSIDandKEY(char SSID[32], char KEY[64]);