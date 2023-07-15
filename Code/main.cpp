/*
 *3.71寸墨水屏240*416
 *ADC范围4.3V-3.6V 2480-2050
 4.2v 2420
 */
#include <Arduino.h>
#include <U8g2_for_Adafruit_GFX.h>
#include <u8g2_fonts.h>
#include "../lib/GB2312.h"
#include <GxEPD2_BW.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include <Fonts/FreeSansBoldOblique24pt7b.h>
#include "../lib/u8g2_mfxuanren_60_number.h"
#include "../lib/RX8025.h"
#include "../lib/RX8025V2.h"
#include "../lib/SDcardcontrol.h"
#include "../lib/img.h"
#include "../lib/WifiConnect.h"
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <LittleFS.h>
#include "SD.h"
#include "OneButton.h"
#include "Audio.h"
#include "SPI.h"
#include <WEMOS_SHT3X.h>


#define ChipID ESP.getEfuseMac()

SHT3X sht30(0x44);

SPIClass hspi = SPIClass(HSPI);
SPIClass sdspi = SPIClass(VSPI);

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "ntp1.aliyun.com", 8 * 3600, 60000);

#define Chinatexlength 15
#define BTNIO_UP 35
#define BTNIO_CONFIRM 39
#define BTNIO_DOWN 34
#define ADC_BAT 36

OneButton BTN_UP(BTNIO_UP, false); // pin,按下为高电平false
OneButton BTN_CONFIRM(BTNIO_CONFIRM, false);
OneButton BTN_DOWM(BTNIO_DOWN, false);

// #define SD_MISO 0
// #define SD_MOSI 2
// #define SD_SCLK 15
#define SD_CS 4
#define WAKEIO 25

#define I2S_DOUT 26
#define I2S_BCLK 27
#define I2S_LRC 14
Audio audio;

// 第一层界面
#define ReadMode 1
#define ClockMode 3
#define PotatoClockMode 4
#define SetMode 5
#define MusicMode 2
// 设置模式第三层界面
#define ConnectWifiMode 1
#define SiteWordMode 2
#define TurnColorMode 3
// 第三层界面
#define ReadBookMode 4

RX8025 rx8025;
U8G2_FOR_ADAFRUIT_GFX u8g2Fonts;
// GxEPD2_BW<GxEPD2_750_YT7, GxEPD2_750_YT7::HEIGHT> display(GxEPD2_750_YT7(/*CS=5*/ 18, /*DC=*/5, /*RST=*/17, /*BUSY=*/16)); // 第二批屏幕
GxEPD2_BW<GxEPD2_371, GxEPD2_371::HEIGHT> display(GxEPD2_371(/*CS=5*/ 5, /*DC=*/19, /*RST=*/16, /*BUSY=*/17)); // 第3.71

/*全局变量声明区
 */
TaskHandle_t TaskClockHandle;
TaskHandle_t TaskGetSDCardMenuHandle;
TaskHandle_t TaskWifiloopHandle;
TaskHandle_t TaskWifiMessageupdate;
TaskHandle_t TaskUpdateTimeHandle;
TaskHandle_t TaskAudioloopHandle;
TaskHandle_t TaskPotatoClockHandle;
TaskHandle_t TaskPotatoClockRestHandle;

SemaphoreHandle_t mutex = xSemaphoreCreateMutex();

struct SetModeDisplayMenu // 设置模式菜单
{
  String Setmenu1 = ">>>\t联网设置";
  String Setmenu2 = ">>>\t阅读超时";
  String Setmenu3 = ">>>\t颜色反转";
  String Setmenu4 = ">>>\t温度显示";
  String Setmenu5 = ">>>\t刷白保存";
} SMDM;

struct ReadModeDisplayMenu // 阅读模式菜单
{
  String Readmenu1 = "----\t继续阅读----";
  String Readmenu2 = "----\t上级菜单----";
  String Readmenu3 = "----\t回主菜单----";
  // String Readmenu3 = "----\t阅读超时----";
  //  String Readmenu4 = ">>>\t按键提示";
} RMDM;

RTC_DATA_ATTR struct TimeMenuShow // 时间结构体
{
  int LastCheckMonth = 99; // 上次对时月 99是为了保证每次重启机器都可以进行一次对时（拆电池之类的）
} TimeMenuShow;

struct PotatoMenuShow // 番茄钟结构体
{
  bool PotatoStop = false;
  bool PotatoDel = false;
  bool PotatoISSuspended = false;   // 番茄钟是否已进入暂停状态
  bool PotatoISINClock = false;     // 番茄钟是否进入25分钟计时
  bool PotatoISINRest = false;      // 番茄中是否进入休息时间
  int SetPotatoPeriod = 25;         // 默认番茄时钟长度为25分钟
  int SetPotatoPeriodWeekShort = 5; // 默认番茄钟休息时长为5分钟 (短)
  int SetPotatoPeriodWeekLong = 25; // 默认番茄钟一轮休息时长为25分钟（长）
} PotatoMenuShow;

RTC_DATA_ATTR struct BatteryCaculate
{
  uint8_t weight[8] = {1, 1, 2, 2, 3, 3, 3, 4};
  uint8_t weight_sum = 19;
  int8_t BAT_Value_buf[8];
  uint8_t BAT_Value_Times = 0; // 电池采集到第几次了，一共采集8次来计算平均值，到第8次时候重新采集第一次的
  bool PowerOn = true;         // 用于查看是否刚开机，若刚开机则计算时候会将第一次采集到的值给所有 BAT_Value_buf[8]
} BatteryCaculate;

struct audioMessage
{
  uint8_t cmd;
  const char *txt;
  uint32_t value;
  uint32_t ret;
} audioTxMessage, audioRxMessage;
enum : uint8_t
{
  SET_VOLUME,
  GET_VOLUME,
  CONNECTTOHOST,
  CONNECTTOSD
};

QueueHandle_t audioSetQueue = NULL;
QueueHandle_t audioGetQueue = NULL;

void CreateQueues()
{
  audioSetQueue = xQueueCreate(10, sizeof(struct audioMessage));
  audioGetQueue = xQueueCreate(10, sizeof(struct audioMessage));
}

//uint64_t ChipID;  //ESP3的芯片ID
uint16_t HumidOffest = 0;
uint8_t layer = 1;                    // 初始化为第一层，往后依次递增 【1】：主页面  【2】：阅读模式，时间模式，配网模式  【3】：设置模式---联网设置
uint8_t layerPointer = 1;             // 初始化为第一层第一个
uint8_t layer2PointerSetMenu = 0;     // 设置菜单光标初始化在返回键上
uint8_t layer2PointerReadFile = 0;    // 阅读模式浏览文件光标初始化在返回键上
uint8_t layer2PointerClock = 0;       // 时间模式光标位置在返回按键上（不显示），0：返回 1：计时器按键
uint16_t layer2PointerMusic = 0;      // 音乐模式默认光标位置在返回按键上
uint8_t layer2PointPotatoClock = 0;   // 番茄钟模式光标
uint8_t layer2_Mode = 0;              // 第二层初始化为位置0模式（ReadMode，ClockMode，SetMode）
uint8_t layer3_Mode = 0;              // 第三层初始化为未知模式  (ConnectWifiMode，ReadBookMode其余待添加)
uint8_t layer3PointerConnectWifi = 0; // wifi连接界面初始化光标为0（可能用不上）(已作为自动框选返回使用)
uint8_t layer3PointerReadMode = 0;    // 阅读模式界面初始化光标为0（返回当前阅读）

RTC_NOINIT_ATTR uint8_t PartialRefresh_Times = 0; // 局刷次数标记，用于全刷
bool layer3_Read_Mode_Set = false;                // 标记在阅读模式是否进入了设置界面
// 时间
const char *ntpServer = "asia.pool.ntp.org";
const long gmtOffset_sec = -8 * 60 * 60;
const int daylightOffset_sec = 0;
bool Time_need_update = false; // 标记时间是否需更新
// int PowertimeMin = 0;          // 低功耗待机时长分钟
int TimeTimer = 0;         // 计时器模式时间统计
uint16_t TimeTimerSec = 0; // 计时器模式获取的秒钟数
bool CanGoSleep = false;   // 是否可以进入低功耗模式 用于已联网后的网路对时
bool SLEEP_JUMP = true;    // 是否是进入clock ，用于防止按下确认键从睡眠模式退出后，确认按键再次被另一个程序触发一次 false:未跳过 true:已跳过（是正常按键操作）
// 菜单标记选项
bool Wifistatus = false;
bool Colorturnstatus = false;
bool keyinformationstatus = false;
RTC_DATA_ATTR bool isOpenTempHumid = true; // 是否打开温湿度显示
// 阅读模式前文件管理页面
int16_t TXTFonts = 15;     // 默认阅读15号字体
int16_t TXTYebianju = 5;   // 默认页边距为5
int16_t TXTZijianju = 1;   // 默认字间距
int16_t TXTHangjianju = 3; // 默认行间距

uint16_t nowpage = 0;                        // 当前文件显示的页面
uint16_t sumpage = 0;                        // 总共文件的页面
uint8_t sumpagefileremain = 0;               // 分割页面后最后一页剩余的文件数量
uint8_t PageChange = 0;                      // 换页 0-无 1-下一页 2-上一页
uint32_t pageCurrent = 0;                    // 当前页数(上次阅读到的页数，从文件系统直接读取) 在阅读前调用该函数获得ReadTXT_GET_LAST_STORAGE_PAGE();
uint8_t pageUpdataCount = 0;                 // 页局刷计数
int GetData;                                 // 书籍文件夹内文件数量
uint32_t txt_zys = 0;                        // 本次的txt总页数，由索引计算得出
bool IS_File_Open = true;                    // 是否需要打开新txt时后偏移（每次打开TXT该值都应该为true）
RTC_DATA_ATTR File txtFile;                  // 本次打开的txt文件系统对象
RTC_DATA_ATTR File indexesFile;              // 本次打开的索引文件系统对象
String savedFile;                            // 获取文件夹内所有文件的名称
String TXTFilename[17];                      // 文件夹内当前页面所有文件名称
RTC_DATA_ATTR String NowOpenTXTname;         // 当前准备打开的TXT文件名称
String FilePath;                             // 当前打开的索引文件地址
RTC_DATA_ATTR uint16_t TXT_ReadTimeOut = 10; // 阅读超时时长min
size_t TXT_overTime = 0;                     // 阅读界面超时时间            ---进入阅读时获取一次时间      ---每次上下页操作获取一次时间（）指未操作的时间
size_t TXT_nowTime = 0;                      // 阅读界面未进行操作经过的时间 ---每次上下页等操作获取一次时间---在loop中循环获取值   指现在的时间
                                             // 计算方法：loop中判断 nowTime-overTime>OverTime
uint16_t PagePercentage = 0;                 // 当前页面进度百分比
uint8_t CharPagePercentage[2] = {};          // 计算百分比的个位和十位
char PagePercentageSelect = '<';             // 选择个位和十位

String Filename[17];       // 音乐每页最大显示量
int MusicSUM;              // 音乐文件夹内文件数量
uint16_t Musicnowpage = 0; // 当前音乐文件显示的页面
uint16_t Musicsumpage = 0; // 总共音乐文件的页面

/*
 *函数声明区
 */
// 主页面显示
void MianScreen();
// 主页面选择鼠标指针
void SelectPointer();
// layer2界面选择鼠标指针
void SelectPionterSetMenu();
// 按键操作扫描函数
void ButtonScan();
// layer2 Mode：ClockMode大时钟展示函数 返回下次定时延迟时间用于同步秒钟刷新
int TimeShow();
// layer2 Mode: SetMode设置模式战术函数
void SetModeShow();
// 用于扫描WIfi并显示到下一个页面之中
void WifiScanDisplay();
// 用于显示OtherData文件夹内用户存放的小说等数据
void OtherDataShow();
// 用于在layer2 阅读模式 界面下 文件浏览 绘制光标方框
void SelectPointerReadMode_File(uint16_t p = 0);
// 用于分割传递的字符串
String fenge(String str, String fen, int index);
// 用于获取当前光标选择的txt文件
String GET_OPEN_TXT_NAME();
// 解析txt文件
void TXTDecode(String bookname);

// RTOS 时间刷新定时器函数
void RTOS_TimeShow(void *RTOS_TimeShow);
// 点击阅读模式获取菜单扫描SD卡文件函数
void GetSDCardMenu();
// 用于显示阅读模式用户文件夹不同页数  int页数 bool 光标显示在上一页 true 下一页 false
void OtherDataShowPage(int page, bool nownext);
// 定次刷新,每5次刷黑白一次，每15次正式全局刷黑白
void FixedRefresh();
// 用于显示阅读书界面
void DisplayTXT(String *txt);
// 翻页之上一页
void ReadTXT_UP_PAGE();
// 翻页调用这个，true下一页，false上一页
void PageDecode(bool PAGE);

// 用于显示进入阅读界面后长按确认键打开的菜单
void ReadModeReadShowMenu();
// 用于显示进入阅读界面选框
void ReadModeReadShowMenu_Rectangle();
// 用于显示系统休眠操作
void SystemSleep();
// 解码TXT文件
// void RECodeTXT(SdFat32 SDFAT, const char *path, const char *bookSUOYIN);

// 用于设置audio的使能
void audioInit();
// 用于设置audio的关闭
void audioEject();
// 用于播放aduio 传入音乐地址
bool audioConnecttoSD(const char *filename);
void audio_info(const char *info);
void audioSetVolume(uint8_t vol);
// 用于显示音乐模式内页面 now:0显示上一页 next:0显示下一页
void MucicPlayerShow(uint32_t page = 0, uint32_t nowORnext = 0);
// 用于绘制音乐模式选择指针
void SelectPointer_Musicplayer(uint16_t p = 0);

// RTOS 用于创建配网模式之后处理配网 退出配网后应结束该任务
void RTOS_SetInternetLoop(void *RTOS_SetInternetLoop);
// RTOS 用于在配网模式下动态显示当前配网信息（结束配网应杀掉该任务）
void WifiMessageupdate(void *WifiMessageupdate);
// RTOS 用于在点击时钟模式之后从网上更新同步时间
void UpdateTime(void *UpdateTime);

// 用于向SD卡某文件位置，写入某些信息 FilePlace:文件位置 FileName:文件名称 FileContent：文件内容
void WriteSDCardMessage(String FilePlace, String Filename, String FileContent);

// 用于显示番茄钟内页面
void PotatoClockPage();
// 用于显示番茄钟界面的光标
void SelectPointer_PotatoClockPage(uint16_t p = 0);
// 用于番茄钟 点击开始番茄钟计时后界面刷新部分
void PotatoClockStartTick_RefreshScreen();
// 番茄钟计时部分
void RTOS_PotatoClockClock(void *RTOS_PotatoClockClock);

// 电量测量函数 返回0-100之间的值
int BATCaculate();

// 按键回调函数 向上
void CLICK_UP()
{
  Serial.println("Button UP click.");
  if (layer == 1) // 如果处于第一层级（主界面）
  {
    if (layerPointer != 1) // 如果当前层的鼠标指针不等于1（不处于最左边的那个选项下）
    {
      layerPointer--; // 图标左移一位
      // Serial.println("按下了:向上");
      SelectPointer();
    }
    else
    {
      layerPointer = 5;
      SelectPointer();
    }
  }
  else if (layer == 2) // 如果在第二层
  {
    if (layer2_Mode == SetMode) // 如果在第二层的SetMode下
    {
      if (layer2PointerSetMenu != 0) // 如果在第二层SetMode下的光标所处位置不在最后一项下
      {
        layer2PointerSetMenu--;
        // 调用第二层光标显示函数
        SelectPionterSetMenu(); // 显示改变后的光标位置
      }
    }
    else if (layer2_Mode == ReadMode) // 如果第二层在ReadMode模式下
    {
      if (layer2PointerReadFile != 0) // 如果在第二层ReadMode下的光标所处位置不在第一项下
      {
        layer2PointerReadFile--;
        // 更新显示的光标位置
        SelectPointerReadMode_File(layer2PointerReadFile);
      }
    }
    else if (layer2_Mode == MusicMode)
    {
      if (layer2PointerMusic != 0)
      {
        layer2PointerMusic--;
        SelectPointer_Musicplayer(layer2PointerMusic);
      }
    }
    else if (layer2_Mode == PotatoClockMode)
    {
      if (layer2PointPotatoClock != 0)
      {
        layer2PointPotatoClock--;
        SelectPointer_PotatoClockPage(layer2PointPotatoClock);
      }
    }
  }
  else if (layer == 3) // 如果在第三层按下向下按键（暂定为已进入小说书籍界面）
  {
    if (layer3_Mode == ReadBookMode && layer3_Read_Mode_Set == false) // 如果在阅读书模式（非阅读设置界面）
    {
      // 进行书籍内的向前翻页显示
      // Serial.println("已向前翻页");
      // PageChange = 2;
      PageDecode(false);
      // ReadTXT_NEXT_PAGE();
      // ReadTXT_UP_PAGE();   // 向前翻页
      // ReadTXT_NEXT_PAGE(); // 显示翻页
    }
    else if (layer3_Mode == ReadBookMode && layer3_Read_Mode_Set == true) // 如果在阅读书模式阅读设置界面
    {
      TXT_overTime = millis(); // 更新一次暂停操作时间
      // 向上移动光标
      if (layer3PointerReadMode > 0)
      {
        layer3PointerReadMode--;
        ReadModeReadShowMenu_Rectangle();
      }
    }
  }
}
// 按键回调函数 确认
void CLICK_CONFIRM()
{
  Serial.println("Button CON click.");
  // 选择了某一选项 将进入另一层界面
  // 判断选择的第一层的哪一个选项
  // 第一层级
  if (layer == 1 && SLEEP_JUMP == true)
  {
    if (layerPointer == 1) // 如果点击的是阅读模式
    {
      digitalWrite(WAKEIO, HIGH); // 使能
      GetSDCardMenu();
      // xTaskCreate(RTOS_GetSDCardMenu, "RTOS_GetSDCardMenu", 1024 * 10, NULL, 1, &TaskGetSDCardMenuHandle);
    }
    else if (layerPointer == 2) // 如果点击的是音乐模式
    {
      layer = 2;
      layer2_Mode = MusicMode;
      pinMode(25, OUTPUT);
      digitalWrite(25, HIGH);
      // 打开音乐界面
      MucicPlayerShow();
    }
    else if (layerPointer == 3) // 如果点击的是时钟模式，并且已跳过睡眠苏醒后的按键扫描
    {
      CanGoSleep = false;
      layer = 2;                                                                        // 进入第二层
      layer2_Mode = ClockMode;                                                          // 进入第二层时钟模式
      xTaskCreate(RTOS_TimeShow, "RTOS_TimeShow", 1024 * 3, NULL, 1, &TaskClockHandle); // 用于判断是否联网从而进行对时
    }
    else if (layerPointer == 4) // 点击番茄模式
    {
      layer = 2;
      layer2_Mode = PotatoClockMode;
      // 用于显示番茄钟内页面
      PotatoClockPage();
      // 用于显示番茄钟界面的光标
      SelectPointer_PotatoClockPage();
    }
    else if (layerPointer == 5) // 如果点击的是设置模式
    {
      layer = 2;              // 进入第二层
      layer2_Mode = SetMode;  // 进入第二层时钟模式
      SetModeShow();          // 进入主界面
      SelectPionterSetMenu(); // 显示初始光标位置
    }
  }
  // 第二层级
  else if (layer == 2)
  {
    // Serial.println("进入第二层及扫描");
    //  待写
    //  二层确认暂时定义为返回主界面
    if (layer2_Mode == ClockMode) // 如果上次选择过了时钟模式则关闭时钟模式
    {
      SLEEP_JUMP = true;
      // vTaskDelete(TaskClockHandle);
      // 首先关闭唤醒源
      // esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);  //关闭所有唤醒源
      // 此时应该已退出低功耗模式
      PartialRefresh_Times = 0; // 设置局刷次数为0
      layer = 1;                // 返回第一层
      layer2_Mode = 0;          // 设置第二层模式为0
      MianScreen();             // 显示主界面
    }

    else if (layer2_Mode == SetMode) // 如果上次选择过了设置模式则关闭
    {
      // 判断第二层内选择的是滴几个按钮
      if (layer2PointerSetMenu == 0) // 如果光标在0（<返回）上
      {
        // 调用返回主界面函数
        MianScreen(); // 显示主界面
        // 设置各项状态
        layer = 1;       // 添加第一层标记
        layer2_Mode = 0; // 设置第二层模式为0
      }
      else if (layer2PointerSetMenu == 1) // 如果光标在(联网设置)上
      {
        // 进入配网模式
        WifiScanDisplay();
      }
      else if (layer2PointerSetMenu == 2) // 如果光标在(阅读超时)上
      {
        // 判断超时时间1min/5min/10min
        if (TXT_ReadTimeOut == 1)
        {
          TXT_ReadTimeOut = 5;
        }
        else if (TXT_ReadTimeOut == 5)
        {
          TXT_ReadTimeOut = 10;
        }
        else if (TXT_ReadTimeOut == 10)
        {
          TXT_ReadTimeOut = 1;
        }

        // 写入内存卡进行保存
        WriteSDCardMessage("/root/SystemData", "TXTReadTimeOut.time", (String)TXT_ReadTimeOut);

        u8g2Fonts.setFont(u8g2_font_wqy16_t_gb2312);
        do
        {
          display.fillRect(128, 100, 50, 16, GxEPD_WHITE);
        } while (display.nextPage());
        do
        {
          u8g2Fonts.setCursor(128, 116);
          u8g2Fonts.print((String)TXT_ReadTimeOut + " Min");
        } while (display.nextPage());
        display.hibernate();
      }
      else if (layer2PointerSetMenu == 4)
      { // 如果光标在(温度显示)上
        Serial.println(isOpenTempHumid);
        if (isOpenTempHumid == true)
        {
          // 先反转是否显示温湿度
          isOpenTempHumid = false;
          // 写入内存卡进行保存
          WriteSDCardMessage("/root/SystemData", "ISOpenTempHumid.temphumid", (String)isOpenTempHumid);
          // 修改界面显示内容
          u8g2Fonts.setFont(u8g2_font_wqy16_t_gb2312);
          do
          {
            u8g2Fonts.setCursor(128, 192);
            u8g2Fonts.print("关闭");
          } while (display.nextPage());
        }
        else
        {
          // 先反转是否显示温湿度
          isOpenTempHumid = true;
          // 写入内存卡进行保存
          WriteSDCardMessage("/root/SystemData", "ISOpenTempHumid.temphumid", (String)isOpenTempHumid);
          // 修改界面显示内容
          u8g2Fonts.setFont(u8g2_font_wqy16_t_gb2312);
          do
          {
            u8g2Fonts.setCursor(128, 192);
            u8g2Fonts.print("开启");
          } while (display.nextPage());
        }
        display.hibernate();
      }
      else if (layer2PointerSetMenu == 5) // 如果光标在(刷白保存)上
      {
        // display.epd2.selectSPI(hspi, SPISettings(25000000, MSBFIRST, SPI_MODE0));
        // display.init(115200);
        display.setPartialWindow(0, 0, display.width(), display.height());
        display.firstPage();
        do
        {
        } while (display.nextPage());
        display.hibernate();
      }
    }

    else if (layer2_Mode == ReadMode) // 如果上次选择过了阅读模式则关闭
    {
      // 判断阅读模式下的光标在哪个上
      if (layer2PointerReadFile == 0) // 如果光标在 返回 按键上
      {
        layer = 1;       // 返回第一层
        layer2_Mode = 0; // 设置第二层模式为0
        savedFile = "";  // 清空保存的的文件
        nowpage = 0;     // 当前页面为首页

        MianScreen(); // 显示主界面
      }
      else if (layer2PointerReadFile == 1) // 如果光标显示在 上一页按键上
      {
        // 判断当前在第几页，若在首页：0则不操作，在 后面则向前翻页
        if (nowpage > 0) // 如果不在首页
        {
          nowpage--; // 向上翻页
          // 刷新页面
          OtherDataShowPage(nowpage, true);
        }
      }
      else if (layer2PointerReadFile == 2) // 如果光标显示在 下一页按键上
      {
        // 判断当前文件在第几页（反推文件名称）
        if (nowpage < sumpage)
        {
          nowpage++;                         // 向下翻页
          OtherDataShowPage(nowpage, false); // 刷新页面
        }
      }
      else
      {
        // 判断当前文件在第几页（反推文件名称）
        NowOpenTXTname = GET_OPEN_TXT_NAME();
        Serial.printf("点击了第三个图标：%s", NowOpenTXTname);
        Serial.println("");
        // 打开文件
        TXTDecode(NowOpenTXTname);
      }
    }

    else if (layer2_Mode == MusicMode)
    {
      if (layer2PointerMusic == 0)
      {
        layer = 1;       // 返回第一层
        layer2_Mode = 0; // 设置第二层模式为0
        nowpage = 0;     // 当前页面为首页
        MianScreen();    // 显示主界面
      }
      else
      {
        audioEject();
        audioInit();
        audioSetVolume(25);
        // sd.begin(SdSpiConfig(4, SHARED_SPI, 25000000, &hspi));
        String nowplaymusicADDR = "/root/Music/" + Filename[layer2PointerMusic - 1];
        audioConnecttoSD(nowplaymusicADDR.c_str());
      }
    }

    else if (layer2_Mode == PotatoClockMode)
    {
      if (layer2PointPotatoClock == 0)
      {
        // 首先杀死所有已经启动了的任务
        if (TaskPotatoClockHandle != NULL)
        {
          PotatoMenuShow.PotatoDel = true; // 标记删除该任务
        }
        // 清楚开始的标志
        PotatoMenuShow.PotatoISINClock = false;
        PotatoMenuShow.PotatoISINRest = false;
        PotatoMenuShow.PotatoISSuspended = false;

        // 再进入主界面
        layer = 1;       // 返回第一层
        layer2_Mode = 0; // 设置第二层模式为0
        nowpage = 0;     // 当前页面为首页
        MianScreen();    // 显示主界面
      }
      else if (layer2PointPotatoClock == 1) // 在开始/暂停/休息番茄钟上
      {
        if (PotatoMenuShow.PotatoISINClock == true || PotatoMenuShow.PotatoISINRest == true) // 如果进入两者钟任意一个计时进行的状态
        {
          // 此时点击按钮的作用为暂停当前计时
          // 直接暂停任务即可
          if (TaskPotatoClockHandle != NULL && PotatoMenuShow.PotatoISSuspended == false) // 判断任务是否还存在&&是否已进入暂停模式 如果不存在可能是已经结束了或者其他状态
          {
            Serial.println("进入暂停");
            PotatoMenuShow.PotatoStop = true;        // 标记任务应该暂停
            PotatoMenuShow.PotatoISSuspended = true; // 标记已经进入了暂停模式
          }
          else if (TaskPotatoClockHandle != NULL && PotatoMenuShow.PotatoISSuspended == true) // 如果已经暂停，则苏醒当前任务
          {
            PotatoMenuShow.PotatoStop = false; // 修改状态未未进入暂停模式
            // vTaskResume(&TaskPotatoClockHandle);
            xTaskResumeFromISR(TaskPotatoClockHandle); // 恢复任务
            PotatoMenuShow.PotatoISSuspended = false;
            // PotatoMenuShow.PotatoISSuspended = false;
          }
        }
        else // 如果未在两个状态之一的话
        {
          // 开始计时代码区域
          PotatoClockStartTick_RefreshScreen();
        }
      }
    }
  }
  // 第三层级
  else if (layer == 3)
  {
    Serial.println("进入第三层及扫描");
    if (layer3_Mode == ConnectWifiMode) // 如果当前显示的是连接wifi模式
    {
      Serial.println("我ConnectWifi");
      if (layer3PointerConnectWifi == 0) // 如果连接wifi模式下框选的是返回
      {
        // 应先关闭关于wifi的一切操作
        vTaskDelete(TaskWifiloopHandle);    // 删除wifi handle client的loop循环
        vTaskDelete(TaskWifiMessageupdate); // 删除 wifi meessage 页面动态刷新获取的SSID和keyword任务
        // 再关闭wifi(已生效)
        WifiDisConnect();
        layer = 2;              // 上一级菜单表标识
        SetModeShow();          // 返设置模式页面
        SelectPionterSetMenu(); // 绘制设置模式当前光标
      }
    }
    else if (layer3_Mode == SiteWordMode)
    {
      Serial.println("我Siteword");
      /* code */
    }
    else if (layer3_Mode == ReadBookMode && layer3_Read_Mode_Set == false) // 如果在阅读书模式 单击用于刷新页面
    {
      // 此时已在阅读界面内，点击确认目的是刷新页面
      // Serial.println("我点了layer=3，layer3_Mode=ReadBookMode的确认按钮");
      display.setFullWindow();
      display.firstPage();
      do
      {
        display.clearScreen();
      } while (display.nextPage());
      PageDecode(true);
      // ReadTXT_UP_PAGE();
      // ReadTXT_NEXT_PAGE();
    }
    else if (layer3_Mode == ReadBookMode && layer3_Read_Mode_Set == true) // 如果在阅读书模式 设置菜单 单击用于确认当前操作
    {
      if (layer3PointerReadMode == 0) // 选择了返回阅读
      {
        layer3_Read_Mode_Set = false;
      }
      else if (layer3PointerReadMode == 1) // 选择了返回上一页（书籍选择页面）
      {
        // 第三层页面归位
        layer3_Read_Mode_Set = false;
        layer3PointerReadMode = 0;
        layer3_Mode = 0;
        // 显示上一层页面
        GetSDCardMenu();
      }
      else if (layer3PointerReadMode == 2) // 选择了返回主界面（首页）
      {
        // 第三层页面归位
        layer3_Read_Mode_Set = false;
        layer3PointerReadMode = 0;
        layer3_Mode = 0;
        // 第二层页面归位
        layer2PointerReadFile = 0;
        layer2_Mode = 0;
        // 显示主页面
        layer = 1;
        MianScreen(); // 显示主界面
      }
      else if (layer3PointerReadMode == 3) // 选择了向左移动一位
      {
        PagePercentageSelect = '<';
        do
        {
          display.drawRect(42, 117, 6, 3, GxEPD_BLACK);
          display.drawRect(57, 117, 6, 3, GxEPD_WHITE);
        } while (display.nextPage());
        display.hibernate();
      }
      else if (layer3PointerReadMode == 4) // 选择了百分比减少
      {
        // 先判断当前选择的是哪一位，再对对应位进行加减
        switch (PagePercentageSelect)
        {
        case '<':
          if (CharPagePercentage[0] > 1) // 十位减少，最小到0
          {
            CharPagePercentage[0]--;
            do
            {
              u8g2Fonts.setCursor(42, 114);
              u8g2Fonts.print(CharPagePercentage[0]);
              u8g2Fonts.setCursor(57, 114);
              u8g2Fonts.print(CharPagePercentage[1]);
            } while (display.nextPage());
          }
          break;
        case '>':
          if (CharPagePercentage[1] > 1) // 个位减少，最小到0
          {
            CharPagePercentage[1]--;
            do
            {
              u8g2Fonts.setCursor(42, 114);
              u8g2Fonts.print(CharPagePercentage[0]);
              u8g2Fonts.setCursor(57, 114);
              u8g2Fonts.print(CharPagePercentage[1]);
            } while (display.nextPage());
          }
          break;
        }
      }
      else if (layer3PointerReadMode == 5) // 选择了百分比增加
      {
        switch (PagePercentageSelect)
        {
        case '<':
          if (CharPagePercentage[0] < 9) // 十位减少，最小到0
          {
            CharPagePercentage[0]++;
            do
            {
              u8g2Fonts.setCursor(42, 114);
              u8g2Fonts.print(CharPagePercentage[0]);
              u8g2Fonts.setCursor(57, 114);
              u8g2Fonts.print(CharPagePercentage[1]);
            } while (display.nextPage());
            display.hibernate();
          }
          break;
        case '>':
          if (CharPagePercentage[1] < 9) // 个位减少，最小到0
          {
            CharPagePercentage[1]++;
            do
            {
              u8g2Fonts.setCursor(42, 114);
              u8g2Fonts.print(CharPagePercentage[0]);
              u8g2Fonts.setCursor(57, 114);
              u8g2Fonts.print(CharPagePercentage[1]);
            } while (display.nextPage());
            display.hibernate();
          }
          break;
        }
      }
      else if (layer3PointerReadMode == 6) // 选择了向右移一位
      {
        PagePercentageSelect = '>';
        do
        {
          display.drawRect(42, 117, 6, 3, GxEPD_WHITE);
          display.drawRect(57, 117, 6, 3, GxEPD_BLACK);
          display.drawRect(108, 117, 6, 3, GxEPD_WHITE);
        } while (display.nextPage());
        display.hibernate();
      }
      else if (layer3PointerReadMode == 7) // 选择了确认跳页
      {
        layer3PointerReadMode = 0;
        // 计算跳页页数
        String i = (String)CharPagePercentage[0] + (String)CharPagePercentage[1];
        uint8_t j = atoi(i.c_str());
        // Serial.println(j);
        //  返回阅读页面
        layer3_Read_Mode_Set = false;
        // 计算确认后的阅读位置
        uint32_t k = txt_zys * j * 0.01;
        pageCurrent = k;
        // Serial.println(k);
        //  写入页数位置
        uint32_t yswz_uint32 = k;
        String yswz_str = "";
        if (yswz_uint32 >= 1000000)
          yswz_str += String(yswz_uint32);
        else if (yswz_uint32 >= 100000)
          yswz_str += String("0") + String(yswz_uint32);
        else if (yswz_uint32 >= 10000)
          yswz_str += String("00") + String(yswz_uint32);
        else if (yswz_uint32 >= 1000)
          yswz_str += String("000") + String(yswz_uint32);
        else if (yswz_uint32 >= 100)
          yswz_str += String("0000") + String(yswz_uint32);
        else if (yswz_uint32 >= 10)
          yswz_str += String("00000") + String(yswz_uint32);
        else
          yswz_str += String("000000") + String(yswz_uint32);

        indexesFile = SD.open(FilePath, "r+"); // 打开索引文件，可读可写
        indexesFile.seek(-7, SeekEnd);         // 从末尾开始偏移7位
        indexesFile.print(yswz_str);           // 写入数据
        indexesFile.close();
        // 显示当前页
        PageDecode(false);
      }
      // display.hibernate();
    }
  }
  SLEEP_JUMP = true;
}
// 按键回调函数 向下
void CLICK_DOWN()
{
  Serial.println("Button DOWN click.");
  // 判断屏幕当前显示在那个层上
  if (layer == 1) // 如果在第一层级
  {
    if (layerPointer != 5) // 如果指针不在第一页最后一个
    {
      layerPointer++; // 图标右移一位
      // Serial.println("按下了:向下");
      SelectPointer();
    }
    else
    { // 在最后一个时候进行回滚
      Serial.println("我在最后一个");
      layerPointer = 1;
      SelectPointer();
    }
  }
  else if (layer == 2) // 如果在第二层
  {
    if (layer2_Mode == SetMode) // 如果在第二层的SetMode下
    {
      if (layer2PointerSetMenu != 5) // 如果在第二层SetMode下的光标所处位置不在最后一项下
      {
        layer2PointerSetMenu++;
        // 调用第二层光标显示函数
        SelectPionterSetMenu(); // 显示改变后的光标位置
      }
    }
    else if (layer2_Mode == ReadMode) // 如果第二层在ReadMode模式下
    {
      if (layer2PointerReadFile != 19) // 如果在第二层ReadMode下的光标所处位置不在最后一项下
      {
        // 判断是否处于尾页
        if (nowpage < sumpage) // 如果不在尾页
        {
          // 光标位置增加
          layer2PointerReadFile++;
          Serial.println("更新了光标位置");
          // 更新显示的光标位置
          SelectPointerReadMode_File(layer2PointerReadFile);
        }
        else if (nowpage == sumpage) // 处于尾页
        {
          // 判断本页面框选是否小于剩余选项量
          if (layer2PointerReadFile < sumpagefileremain + 2) // 如果不是本页最后一个文件
          {
            // 光标位置增加
            layer2PointerReadFile++;
            // 更新显示的光标位置
            SelectPointerReadMode_File(layer2PointerReadFile);
          }
          else // 如果是最后一个文件则xxxxxxxxx
          {
            // 什么都不做 或者闲的没事写个弹窗提示也不是不可以
            Serial.println("已是本页面最后一个选项");
          }
        }
      }
    }
    else if (layer2_Mode == ClockMode) // 如果处于时钟模式下
    {
      // 在时钟模式下按下了向下按键--直接开始进入秒表计时（无需等待，无需更新光标）
      //
      layer2PointerClock = 1;            // 标记进入计时器模式
      TimeTimerSec = rx8025.getSecond(); // 获取当前秒钟并保存
      layer = 3;                         // 进入第三层显示界面
      // 调用时间/倒计时显示界面
    }
    else if (layer2_Mode == MusicMode)
    {
      if (layer2PointerMusic < 17)
      {
        layer2PointerMusic++;
        SelectPointer_Musicplayer(layer2PointerMusic);
      }
    }
    else if (layer2_Mode == PotatoClockMode)
    {
      if (layer2PointPotatoClock < 1)
      {
        layer2PointPotatoClock++;
        SelectPointer_PotatoClockPage(layer2PointPotatoClock);
      }
    }
  }
  else if (layer == 3) // 如果在第三层按下向上按键（暂定为已进入小说书籍界面）
  {
    if (layer3_Mode == ReadBookMode && layer3_Read_Mode_Set == false) // 如果在阅读书模式
    {
      // 进行书籍内的向后翻页显示
      Serial.println("已向后翻页");
      PageChange = 1;
      PageDecode(true);
    }
    else if (layer3_Mode == ReadBookMode && layer3_Read_Mode_Set == true) // 如果在阅读书模式阅读设置界面
    {
      TXT_overTime = millis(); // 更新一次暂停操作时间
      // 向下移动光标
      if (layer3PointerReadMode < 7)
      {
        layer3PointerReadMode++;
        ReadModeReadShowMenu_Rectangle();
      }
    }
  }
}
// 按键回调函数 双击确认
void DOUBLE_CLICK_CONFIRM()
{
  Serial.println("Button DOUBLE click.");
  if (layer == 3 && layer3_Mode == ReadBookMode && layer3_Read_Mode_Set == false) // 阅读界面中双击用于返回时钟页面？上一级页面放在长按调用出的菜单里
  {
    CanGoSleep = false;
    layer = 2;               // 进入第二层
    layer2_Mode = ClockMode; // 进入第二层时钟模式
    display.setPartialWindow(0, 0, display.width(), display.height());
    display.firstPage();
    do
    {
      display.clearScreen(GxEPD_WHITE);
    } while (display.nextPage());
    // BTN_CONFIRM.attachClick(CLICK_CONFIRM);
    // SLEEP_JUMP = true;
    xTaskCreate(RTOS_TimeShow, "RTOS_TimeShow", 1024 * 3, NULL, 1, &TaskClockHandle); // 用于判断是否联网从而进行对时
  }
  else if (layer == 1) // 主界面双击进入阅读界面
  {
    Serial.print("当前打开的TXT文件名称:");
    Serial.println(NowOpenTXTname);
    digitalWrite(WAKEIO, HIGH); // 使能
    if (NowOpenTXTname != "")   // 如果检测到上次打开过小说
    {
      layer = 3;                  // 第三层
      layer3_Mode = ReadBookMode; // 设置第二层模式为ReadBookMode
      IS_File_Open = true;
      PageDecode(true);
    }
    else // 否则提示未检测到上次打开小说
    {
      display.setPartialWindow(0, 0, display.width(), display.height());
      do
      {
        u8g2Fonts.setCursor(100, 371);
        u8g2Fonts.print("未发现小说");
      } while (display.nextPage());
      display.hibernate();
    }
  }
  else if (layer == 2 && layer2_Mode == PotatoClockMode && layer2PointPotatoClock == 1) // 再番茄钟界面 指针在非返回上 双击可以直接重置番茄钟的计时
  {
    // 先判断是否存在一个计时，如果不存在的话就不进行任何操作
    if (TaskPotatoClockHandle != NULL) // 如果存在时钟任务
    {
      PotatoMenuShow.PotatoDel = true; // 标记删除该任务
      // 清楚开始的标志
      PotatoMenuShow.PotatoISINClock = false;
      PotatoMenuShow.PotatoISINRest = false;
      PotatoMenuShow.PotatoISSuspended = false;
      // 再调用界面显示
      PotatoClockPage();
      // 用于显示番茄钟界面的光标
      SelectPointer_PotatoClockPage(layer2PointPotatoClock);
    }
  }
}
// 按键回调函数 长按确认
void LONG_CLICK_CONFIRM()
{
  Serial.println("进入长按确认按键扫描");
  if (layer == 3)
  {
    if (layer3_Mode == ReadBookMode && layer3_Read_Mode_Set == false)
    {
      TXT_overTime = millis(); // 更新一次暂停操作时间
      layer3_Read_Mode_Set = true;
      ReadModeReadShowMenu();
    }
  }
}

void setup()
{
  Serial.begin(115200);
  setCpuFrequencyMhz(80);
  delay(100);
  //ChipID = ESP.getEfuseMac();
    
  pinMode(BTNIO_UP, INPUT_PULLUP);
  pinMode(BTNIO_DOWN, INPUT_PULLUP);
  pinMode(BTNIO_CONFIRM, INPUT_PULLUP);
  pinMode(WAKEIO, OUTPUT);
  digitalWrite(WAKEIO, HIGH);

  rx8025.RX8025_init(); // RTC时钟初始化

  display.setRotation(4);

  hspi.begin(18 /* SCK */, 19 /* MISO */, 23 /* MOSI */, 5 /* SS */);
  //sdspi.begin(SD_SCLK, SD_MISO, SD_MOSI, SD_CS);
  //  delay(100);
  pinMode(ADC_BAT, INPUT);
  delay(10);
  // 测试部分，用于查看RTC内存是否正常工作
  // for (int i = 0; i < 8; i++)
  // {
  //   Serial.print("电量次数:");
  //   Serial.print(i);
  //   Serial.print("测到的值:");
  //   Serial.print(BatteryCaculate.BAT_Value_buf[i]);
  // }

  // 单机
  BTN_UP.attachClick(CLICK_UP);
  BTN_CONFIRM.attachClick(CLICK_CONFIRM);
  BTN_DOWM.attachClick(CLICK_DOWN);
  // 双击
  BTN_CONFIRM.attachDoubleClick(DOUBLE_CLICK_CONFIRM);
  // 长按
  BTN_CONFIRM.attachLongPressStart(LONG_CLICK_CONFIRM);
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_39, 1);               // 1 = High, 0 = Low //设置睡眠唤醒引脚
  if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER) // 被定时器唤醒原因：时钟定时器
  {
    display.epd2.selectSPI(hspi, SPISettings(25000000, MSBFIRST, SPI_MODE0));
    display.init(115200, false);
    u8g2Fonts.begin(display);
    u8g2Fonts.setForegroundColor(GxEPD_BLACK); // 设置前景色
    u8g2Fonts.setBackgroundColor(GxEPD_WHITE); // 设置背景色
    SystemSleep();
  }
  else
  {
    if (!SD.begin(SD_CS, hspi))
    {
      Serial.println("SD卡加载失败!");
      // return;
    }
    // 读取系统配置文件(WIFI)
    if (SD.exists("/root/SystemData/WIFI.wifi"))
    {
      Wifistatus = true;
      File file = SD.open("/root/SystemData/WIFI.wifi", "r");
      String WIFISSIDPASSWORD = file.readString();
      int place = WIFISSIDPASSWORD.indexOf("\r\n");
      String SSID = WIFISSIDPASSWORD.substring(0, place);
      String KEY = WIFISSIDPASSWORD.substring(place + 2, WIFISSIDPASSWORD.length());
      file.close();
      SSID.c_str();
      char SSIDC[32] = {};
      char KEYC[64] = {};
      strcpy(SSIDC, SSID.c_str());
      strcpy(KEYC, KEY.c_str());
      GetWIFISSIDandKEY(SSIDC, KEYC);
    }
    // 读取系统配置文件(阅读超时时长)
    if (SD.exists("/root/SystemData/TXTReadTimeOut.time"))
    {
      File file = SD.open("/root/SystemData/TXTReadTimeOut.time", "r");
      String TXTTimeOut = file.readString();
      file.close();
      TXT_ReadTimeOut = TXTTimeOut.toInt();
    }
    // 读取系统配置文件（上次对时时间）
    if (SD.exists("/root/SystemData/LastCheckTimeMonth.month"))
    {
      File file = SD.open("/root/SystemData/LastCheckTimeMonth.month", "r");
      String LastHour = file.readString();
      TimeMenuShow.LastCheckMonth = LastHour.toInt();
    }
    // 读取SHT30
    if (SD.exists("/root/SystemData/ISOpenTempHumid.temphumid"))
    {
      File file = SD.open("/root/SystemData/ISOpenTempHumid.temphumid", "r");
      String LastHour = file.readString();
      isOpenTempHumid = (LastHour == "1") ? true : false;
    }

    display.epd2.selectSPI(hspi, SPISettings(25000000, MSBFIRST, SPI_MODE0));
    display.init(115200);
    u8g2Fonts.begin(display);
    u8g2Fonts.setForegroundColor(GxEPD_BLACK); // 设置前景色
    u8g2Fonts.setBackgroundColor(GxEPD_WHITE); // 设置背景色
    display.setRotation(4);
    MianScreen();
  }
  Serial.print("阅读超时时间:");
  Serial.println(TXT_ReadTimeOut);
  // xTaskCreate(MainLoop, "MainLoop", 1024*30, NULL, 1, NULL);
}

void loop()
{
  // digitalWrite(WAKEIO, HIGH);
  // delay(500);
  // if(sht30.get()==0){
  //     Serial.println("sht30温度:");
  //     Serial.println(sht30.cTemp - 2.8);
  //     Serial.println("sht30湿度:");
  //     Serial.println(sht30.humidity);
  // }
  // else
  // {
  //   Serial.println("sht30读取错误");
  // }
  if (layer == 2 && layer2_Mode == ClockMode && CanGoSleep == true) // 如果需要更新时间，选择了时间模式由于自带暂停所以不需要手动延时
  {
    // Serial.println("进入睡眠模式");
    int timedelay = 60 - TimeShow();
    esp_sleep_enable_timer_wakeup(1000 * 1000 * (timedelay + 0.5));
    esp_deep_sleep_start();
    // esp_light_sleep_start();
  }
  else // 若不在时间模式里面则开启扫描延时
  {
    // vTaskDelay(50);
  }
  if (layer == 3 && layer3_Mode == ReadBookMode)
  {
    TXT_nowTime = millis();
    if ((TXT_nowTime - TXT_overTime) >= 60000 * TXT_ReadTimeOut) // 若超时
    {
      // 显示超时界面
      TXT_nowTime = 0;
      TXT_overTime = 0;
      // 进入时钟模式
      SystemSleep();
    }
  }

  // pinMode(BTNIO_CONFIRM, INPUT_PULLUP);
  BTN_UP.tick();
  BTN_CONFIRM.tick();
  BTN_DOWM.tick();
  ButtonScan(); // 自动唤醒后立刻进行按键状态扫描

  if (PotatoMenuShow.PotatoDel == true)
  {
    vTaskDelete(TaskPotatoClockHandle);
    PotatoMenuShow.PotatoDel = false;
  }
  if (PotatoMenuShow.PotatoStop == true) // 如果未进入暂停状态
  {
    vTaskSuspend(TaskPotatoClockHandle); // 暂停
    PotatoMenuShow.PotatoStop = false;
  }
  // Serial.println(isOpenTempHumid);
  //  vTaskDelete(NULL);
}

// 第一层界面 layer = 1
void MianScreen() // 用于绘制主界面五大图标
{
  pinMode(WAKEIO, OUTPUT);
  digitalWrite(WAKEIO, HIGH);
  uint8_t powerString = BATCaculate();
  if (powerString > 100)
  {
    powerString = 100;
  }
  else if (powerString < 0)
  {
    powerString = 0;
  }
  // Serial.println(powerString);

  display.setRotation(4);
  layer = 1; // 标志回到第一页面
  display.setPartialWindow(0, 0, display.width(), display.height());
  display.firstPage();
  u8g2Fonts.setFont(u8g2_font_wqy16_t_gb2312);
  do
  {
    display.drawInvertedBitmap(28, 28, bitmap_book64, 64, 64, GxEPD_BLACK); // 主页面书图片
    // u8g2Fonts.setCursor(93, 115);
    // u8g2Fonts.print("阅读模式");
    display.drawInvertedBitmap(148, 28, bitmap_music64, 64, 64, GxEPD_BLACK); // 主页面音乐图片

    display.drawInvertedBitmap(88, 148, bitmap_clock64, 64, 64, GxEPD_BLACK); // 主页面时钟图片
    // u8g2Fonts.setCursor(93, 245);
    // u8g2Fonts.print("时钟模式");
    display.drawInvertedBitmap(28, 268, bitmap_album64, 64, 64, GxEPD_BLACK); // 主页面相册图片

    display.drawInvertedBitmap(148, 268, bitmap_setting64, 64, 64, GxEPD_BLACK); // 主页面设置图片
    // u8g2Fonts.setCursor(93, 375);
    // u8g2Fonts.print("设置模式");
    // SelectRectangle();

    // display.drawRoundRect(106, 25, 85, 80, 5, GxEPD_BLACK);
    u8g2Fonts.setCursor(110, 400);
    u8g2Fonts.print(powerString);
    // u8g2Fonts.print(powerString);
    u8g2Fonts.print("%");
  } while (display.nextPage());
  digitalWrite(WAKEIO, LOW);

  // 显示选择框
  SelectPointer();
}

// 用于在主界面图标上绘制光标（确定选择方框）选项
void SelectPointer()
{
  display.setPartialWindow(0, 0, display.width(), display.height());
  // display.firstPage();

  // 如果是第一层并且是第一个选项
  if (layer == 1 && layerPointer == 1)
  {
    do
    {
      display.drawRoundRect(134, 254, 92, 92, 10, GxEPD_WHITE);
      display.drawRoundRect(14, 14, 92, 92, 10, GxEPD_BLACK);
      display.drawRoundRect(134, 14, 92, 92, 10, GxEPD_WHITE);
    } while (display.nextPage());
  }
  // 如果是第一层并且是第二个选项
  else if (layer == 1 && layerPointer == 2)
  {
    do
    {
      display.drawRoundRect(134, 14, 92, 92, 10, GxEPD_BLACK);
      display.drawRoundRect(14, 14, 92, 92, 10, GxEPD_WHITE);
      display.drawRoundRect(14, 134, 212, 92, 10, GxEPD_WHITE);
    } while (display.nextPage());
  }
  // 如果是第一层并且是第三个选项
  else if (layer == 1 && layerPointer == 3)
  {
    do
    {
      display.drawRoundRect(134, 14, 92, 92, 10, GxEPD_WHITE);
      display.drawRoundRect(14, 134, 212, 92, 10, GxEPD_BLACK);
      display.drawRoundRect(14, 254, 92, 92, 10, GxEPD_WHITE);
    } while (display.nextPage());
  }
  // 如果是第一层并且是第四个选项
  else if (layer == 1 && layerPointer == 4)
  {
    do
    {
      display.drawRoundRect(14, 134, 212, 92, 10, GxEPD_WHITE);
      display.drawRoundRect(14, 254, 92, 92, 10, GxEPD_BLACK);
      display.drawRoundRect(134, 254, 92, 92, 10, GxEPD_WHITE);
    } while (display.nextPage());
  }
  // 如果是第一层并且是第五个选项
  else if (layer == 1 && layerPointer == 5)
  {
    do
    {
      display.drawRoundRect(14, 254, 92, 92, 10, GxEPD_WHITE);
      display.drawRoundRect(134, 254, 92, 92, 10, GxEPD_BLACK);
      display.drawRoundRect(14, 14, 92, 92, 10, GxEPD_WHITE);
    } while (display.nextPage());
  }
  display.hibernate();
}

// 用于在layer2设置界面图标上绘制光标（确定选择方框）选项
void SelectPionterSetMenu()
{
  display.setPartialWindow(0, 0, display.width(), display.height());

  // 判断是否在第二层（防止误调用操作）判断是否为Setmode（设置模式）下的选择
  if (layer == 2 && layer2_Mode == SetMode)
  {
    if (layer2PointerSetMenu == 0) // 光标显示在第一个选项上(<返回)
    {
      // 显示光标在第一个按键上
      do
      {
        display.drawRoundRect(5, 63, 105, 19, 3, GxEPD_WHITE);
        display.drawRoundRect(5, 25, 52, 19, 3, GxEPD_BLACK);
      } while (display.nextPage());
      display.hibernate();
    }

    else if (layer2PointerSetMenu == 1) // 光标显示在第二个选项上()
    {
      // 显示光标在第二个按键上
      do
      {
        display.drawRoundRect(5, 25, 52, 19, 3, GxEPD_WHITE);
        display.drawRoundRect(5, 101, 105, 19, 3, GxEPD_WHITE);
        display.drawRoundRect(5, 63, 105, 19, 3, GxEPD_BLACK);
      } while (display.nextPage());
      display.hibernate();
    }

    else if (layer2PointerSetMenu == 2) // 光标显示在第二个选项上()
    {
      // 显示光标在第二个按键上
      do
      {
        display.drawRoundRect(5, 63, 105, 19, 3, GxEPD_WHITE);
        display.drawRoundRect(5, 139, 105, 19, 3, GxEPD_WHITE);
        display.drawRoundRect(5, 101, 105, 19, 3, GxEPD_BLACK);
      } while (display.nextPage());
      display.hibernate();
    }

    else if (layer2PointerSetMenu == 3) // 光标显示在第二个选项上()
    {
      // 显示光标在第二个按键上
      do
      {
        display.drawRoundRect(5, 101, 105, 19, 3, GxEPD_WHITE);
        display.drawRoundRect(5, 177, 105, 19, 3, GxEPD_WHITE);
        display.drawRoundRect(5, 139, 105, 19, 3, GxEPD_BLACK);
      } while (display.nextPage());
      display.hibernate();
    }

    else if (layer2PointerSetMenu == 4) // 光标显示在第二个选项上()
    {
      // 显示光标在第二个按键上
      do
      {
        display.drawRoundRect(5, 139, 105, 19, 3, GxEPD_WHITE);
        display.drawRoundRect(5, 215, 105, 19, 3, GxEPD_WHITE);
        display.drawRoundRect(5, 177, 105, 19, 3, GxEPD_BLACK);
      } while (display.nextPage());
      display.hibernate();
    }

    else if (layer2PointerSetMenu == 5) // 光标显示在第二个选项上()
    {
      // 显示光标在第二个按键上
      do
      {
        display.drawRoundRect(5, 177, 105, 19, 3, GxEPD_WHITE);
        display.drawRoundRect(5, 215, 105, 19, 3, GxEPD_BLACK);
      } while (display.nextPage());
      display.hibernate();
    }
  }
}

// 用于在layer2 阅读模式 界面下 文件浏览 绘制光标方框
void SelectPointerReadMode_File(uint16_t p)
{
  display.setPartialWindow(0, 0, display.width(), display.height()); // 局刷
  if (p == 0)                                                        // 在返回上
  {
    do
    {
      display.drawRoundRect(5, 13, 55, 19, 5, GxEPD_BLACK);
      display.drawRoundRect(92, 13, 60, 19, 5, GxEPD_WHITE); // 清楚1
    } while (display.nextPage());
  }
  else if (p == 1) // 在上一页
  {
    do
    {
      display.drawRoundRect(5, 13, 55, 19, 5, GxEPD_WHITE);   // 清楚0
      display.drawRoundRect(181, 13, 60, 19, 5, GxEPD_WHITE); // 清楚2
      display.drawRoundRect(92, 13, 60, 19, 5, GxEPD_BLACK);  // 绘制自己 1
    } while (display.nextPage());
  }
  else if (p == 2) // 在下一页
  {
    do
    {
      display.drawRoundRect(92, 13, 60, 19, 5, GxEPD_WHITE);     // 清楚上一页
      display.drawRoundRect(181, 13, 60, 19, 5, GxEPD_BLACK);    // 绘制下一页
      display.drawCircle(13, 55 + 19 * (p - 2), 8, GxEPD_WHITE); // 清楚3

    } while (display.nextPage());
  }
  else if (p == 3) // 在第一本书
  {
    do
    {
      display.drawRoundRect(181, 13, 60, 19, 5, GxEPD_WHITE);    // 清楚下一页
      display.drawCircle(13, 55 + 19 * (p - 3), 8, GxEPD_BLACK); // 绘制第一本书
      display.drawCircle(13, 55 + 19 * (p - 2), 8, GxEPD_WHITE); // 清楚第二本书

    } while (display.nextPage());
  }
  else if (p < 20) // 在
  {
    do
    {
      display.drawCircle(13, 55 + 19 * (p - 4), 8, GxEPD_WHITE);
      display.drawCircle(13, 55 + 19 * (p - 3), 8, GxEPD_BLACK);
      display.drawCircle(13, 55 + 19 * (p - 2), 8, GxEPD_WHITE);
    } while (display.nextPage());
  }
  display.hibernate();
}

// 用于退出时间休眠模式的按键扫描
void ButtonScan()
{

  if (layer == 2 && digitalRead(BTNIO_CONFIRM) == 1)
  {
    // Serial.println("进入第二层及扫描");
    // 待写
    // 二层确认暂时定义为返回主界面
    if (layer2_Mode == ClockMode) // 如果上次选择过了时钟模式则关闭时钟模式
    {
      SLEEP_JUMP = false; // 该位置的JUMP为从时钟睡眠模式退出
      // vTaskDelete(TaskClockHandle);
      // 首先关闭唤醒源
      // esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);  //关闭所有唤醒源
      // 此时应该已退出低功耗模式
      PartialRefresh_Times = 0; // 设置局刷次数为0
      layer = 1;                // 返回第一层
      layer2_Mode = 0;          // 设置第二层模式为0
      display.setRotation(3);
      MianScreen(); // 显示主界面

      // digitalWrite(WAKEIO, HIGH); //使能外设
    }
  }
}

// 用于显示时间模式内页面

int TimeShow()
{
  int Cursor = 0;
  // 检查，如果打开了温湿度显示，则整体左移30像素
  if (isOpenTempHumid == true)
  {
    Cursor = 56;
  }

  pinMode(WAKEIO, OUTPUT);
  digitalWrite(WAKEIO, HIGH);
  // 该处调用电量计算显示函数
  uint8_t powerString = BATCaculate();
  if (powerString > 100)
  {
    powerString = 100;
  }
  else if (powerString < 0)
  {
    powerString = 0;
  }

  // Serial.println(powerString);
  // digitalWrite(WAKEIO, LOW);

  String TimesumFront;
  String TimesumLast;
  String Datesum;
  int Hour = (int)(rx8025.getHour() & 0xff);
  int Minute = (int)(rx8025.getMinute() & 0xff);
  int Year = rx8025.getYear();
  int Month = rx8025.getMonth();
  int Date = rx8025.getDate();
  if (Hour < 10)
  {
    TimesumFront = "0" + String(Hour);
  }
  else
  {
    TimesumFront = String(Hour);
  }

  if (Minute < 10)
  {
    TimesumLast = "0" + (String)Minute;
  }
  else
  {
    TimesumLast = (String)Minute;
  }

  Datesum = "20" + String(Year) + "." + String(Month) + "." + String(Date);

  if (PartialRefresh_Times == 120)
  {
    // Serial.println("醒来后用了全刷");
    display.setFullWindow();
    PartialRefresh_Times = 0;
  }
  else
  {
    // Serial.println("醒来后用了局刷");
    display.setPartialWindow(0, 0, display.width(), display.height());
    PartialRefresh_Times++;
  }

  display.firstPage();
  display.setRotation(3);

  do
  {
    u8g2Fonts.setFont(u8g2_font_logisoso92_tn);
    if (Hour == 1 || Hour == 10 || Hour == 11)
    {
      u8g2Fonts.setCursor(78 - Cursor, 162);
      u8g2Fonts.print(TimesumFront);
    }
    else
    {
      u8g2Fonts.setCursor(69 - Cursor, 162);
      u8g2Fonts.print(TimesumFront);
    }

    u8g2Fonts.setCursor(215 - Cursor, 162);
    u8g2Fonts.print(TimesumLast);
    display.fillRoundRect(202 - Cursor, 100, 6, 10, 6, GxEPD_BLACK);
    display.fillRoundRect(202 - Cursor, 130, 6, 10, 6, GxEPD_BLACK);

    u8g2Fonts.setFont(u8g2_font_wqy16_t_gb2312);
    u8g2Fonts.setCursor(145, 223);
    u8g2Fonts.print(Datesum);

    u8g2Fonts.setCursor(235, 223);
    u8g2Fonts.print(powerString);
    u8g2Fonts.print("%");

    //  温湿度显示区域
    String T;
    String H;
    if (isOpenTempHumid == true)
    {
      display.drawRect(291, 53, 2, 135, GxEPD_BLACK); // 画出竖线

      if (sht30.get() == 0)
      {
        T = (String)sht30.cTemp;
        T = T + " C";
        H = (String)(sht30.humidity - HumidOffest);
        H = H + " %";

        display.drawBitmap(305, 70, bitmap_Tepm32, 32, 32, GxEPD_BLACK);
        u8g2Fonts.setCursor(345, 90);
        u8g2Fonts.print(T);
        display.drawBitmap(305, 133, bitmap_Humid32, 32, 32, GxEPD_BLACK);
        u8g2Fonts.setCursor(345, 158);
        u8g2Fonts.print(H);
      }
      else
      {
        display.drawBitmap(305, 70, bitmap_Tepm32, 32, 32, GxEPD_BLACK);
        u8g2Fonts.setCursor(345, 90);
        u8g2Fonts.print("False");
        display.drawBitmap(305, 133, bitmap_Humid32, 32, 32, GxEPD_BLACK);
        u8g2Fonts.setCursor(345, 158);
        u8g2Fonts.print("False");
      }
    }

  } while (display.nextPage());

  display.hibernate();
  digitalWrite(WAKEIO, LOW);
  return rx8025.getSecond();
}

// 用于显示设置模式内页面
void SetModeShow()
{
  u8g2Fonts.setFont(u8g2_font_wqy16_t_gb2312);
  display.setPartialWindow(0, 0, display.width(), display.height());
  // display.setPartialWindow(0, 0, display.width(), display.height());
  display.firstPage();
  do
  {
    display.drawInvertedBitmap(8, 28, bitmap_back, 16, 16, GxEPD_BLACK);
    u8g2Fonts.setCursor(26, 40);
    u8g2Fonts.print("返回");
    // display.drawInvertedBitmap(8, 26, bitmap_deviceinformation, 48, 48, GxEPD_BLACK);
    // display.drawRoundRect(8, 23, 138, 45, 5, GxEPD_BLACK);
    u8g2Fonts.setCursor(8, 78);
    u8g2Fonts.print(SMDM.Setmenu1);
    u8g2Fonts.setCursor(8, 116);
    u8g2Fonts.print(SMDM.Setmenu2);
    u8g2Fonts.setCursor(8, 154);
    u8g2Fonts.print(SMDM.Setmenu3);
    u8g2Fonts.setCursor(8, 192);
    u8g2Fonts.print(SMDM.Setmenu4);
    u8g2Fonts.setCursor(8, 230);
    u8g2Fonts.print(SMDM.Setmenu5);
    // 下面区域用于显示软件版本号和其他东西
    display.fillRect(25, 250, 190, 2, GxEPD_BLACK);
    u8g2Fonts.setCursor(38, 278);
    u8g2Fonts.print("软件版本：\t "__TIME__);
    u8g2Fonts.setCursor(38, 308);
    u8g2Fonts.print("发布日期：\t "__DATE__);
    u8g2Fonts.setCursor(38, 338);
    u8g2Fonts.print("制作人员：\t 叫我武哒哒");
    u8g2Fonts.setCursor(38, 368);
    u8g2Fonts.print("bilibili   ：\t 叫我武哒哒");
    u8g2Fonts.setCursor(38, 398);
    u8g2Fonts.print("ChipID: \t");
    u8g2Fonts.print(ChipID);
    // display.drawFastVLine(0, 250, 3, GxEPD_BLACK);

  } while (display.nextPage());

  // 在按键右侧显示当前设置的状态
  display.setPartialWindow(0, 0, display.width(), display.height());
  do
  {
    u8g2Fonts.setCursor(128, 78);
    if (Wifistatus == true) // 后期修改为读取闪存中是否存在wifi账号和密码的文件来决定
    {
      u8g2Fonts.print("已配网  :");
      u8g2Fonts.setCursor(177, 78);
      u8g2Fonts.print(TransportSSID());
    }
    else
    {
      u8g2Fonts.print("未配网");
    }

    u8g2Fonts.setCursor(128, 116);
    u8g2Fonts.print((String)TXT_ReadTimeOut + " Min");

    u8g2Fonts.setCursor(128, 154);
    if (Colorturnstatus == true)
    {
      u8g2Fonts.print("开启");
    }
    else
    {
      u8g2Fonts.print("关闭");
    }

    u8g2Fonts.setCursor(128, 192);
    if (isOpenTempHumid == true)
      u8g2Fonts.print("开启");
    else
      u8g2Fonts.print("关闭");

    u8g2Fonts.setCursor(128, 230);
    u8g2Fonts.print("关闭");

  } while (display.nextPage());

  display.hibernate();
}

// 用于显示阅读模式用户文件夹内容
void OtherDataShow()
{
  // 获取文件夹文件以及数量
  int Booksum = 0; // 本文件夹拥有的TXT文件总数量

  File dir = SD.open("/root/OthersData");
  File file = dir.openNextFile();

  while (file)
  {
    if (file.isDirectory()) // 如果文件是一个目录，则不读取
    {
    }
    else
    {
      if (Booksum <= 17)
      {
        TXTFilename[Booksum] = file.name();
        // Serial.println(TXTFilename[Booksum]);
      }
      // 否则就是一个标准的文件，先存入
      Booksum++;
    }
    file = dir.openNextFile();
  }
  dir.close();
  file.close();

  GetData = Booksum;
  Serial.print("书的总数量:");
  Serial.println(GetData);
  // 如果小于17则只有单页
  if (GetData <= 17)
  {
    sumpagefileremain = GetData;
  }
  sumpage = GetData / 17; // 更新总页数
  Serial.print("总页面:\n");
  Serial.println(sumpage);
  layer2PointerReadFile = 0; // 设置初始化光标位置

  display.setPartialWindow(0, 0, display.width(), display.height());
  u8g2Fonts.setFont(u8g2_font_wqy16_t_gb2312);
  display.firstPage();
  do
  {
    // 如果不存在文件
    if (GetData == 0)
    {
      display.drawInvertedBitmap(8, 17, bitmap_back, 16, 16, GxEPD_BLACK);
      u8g2Fonts.setCursor(26, 28);
      u8g2Fonts.print("返回");

      display.drawInvertedBitmap(224, 14, bitmap_NEXTPAGE, 16, 16, GxEPD_BLACK);
      u8g2Fonts.setCursor(184, 28);
      u8g2Fonts.print("下一页");

      display.drawInvertedBitmap(90, 14, bitmap_FRONTPAGE, 16, 16, GxEPD_BLACK);
      u8g2Fonts.setCursor(104, 28);
      u8g2Fonts.print("上一页");

      u8g2Fonts.setCursor(95, 55);
      u8g2Fonts.print("未找到文件");
    }
    else // 存在文件
    {
      display.drawInvertedBitmap(8, 17, bitmap_back, 16, 16, GxEPD_BLACK);
      u8g2Fonts.setCursor(26, 28);
      u8g2Fonts.print("返回");

      display.drawInvertedBitmap(224, 14, bitmap_NEXTPAGE, 16, 16, GxEPD_BLACK);
      u8g2Fonts.setCursor(184, 28);
      u8g2Fonts.print("下一页");

      display.drawInvertedBitmap(90, 14, bitmap_FRONTPAGE, 16, 16, GxEPD_BLACK);
      u8g2Fonts.setCursor(104, 28);
      u8g2Fonts.print("上一页");
      // 在规定区域内显示文件（一页暂定显示8个文件）

      int cursory = 61;
      if (sumpage == 0) // 如果只有一页
      {
        for (int i = 0; i < GetData; i++)
        {
          u8g2Fonts.setCursor(26, cursory);
          u8g2Fonts.print(TXTFilename[i]);
          cursory = cursory + 19;
        }
      }
      else // 否则该页为满页（因为这个函数只显示第一页的内容）
      {
        for (int i = 0; i < 17; i++)
        {
          u8g2Fonts.setCursor(26, cursory);
          u8g2Fonts.print(TXTFilename[i]);
          cursory = cursory + 19;
        }
      }
    }
    display.drawRoundRect(5, 13, 55, 19, 5, GxEPD_BLACK);
    // 调用文件浏览的光标显示
  } while (display.nextPage());
  // SelectPointerReadMode_File(); //更新光标位置
  display.hibernate();
}

// 用于显示阅读模式用户文件夹不同页数
void OtherDataShowPage(int page, bool nownext)
{
  // 获取文件夹文件以及数量
  char fileNamebuffer[300]; // 3个可以存一个utf8编码的汉字，现在最长能够打开60个汉字（只用于记录数量所以不需要）
  int Booksum = 0;

  // sd.begin(SdSpiConfig(4, SHARED_SPI, 25000000, &hspi));

  if (!SD.open("/root/OthersData"))
  {
    Serial.println("dir.open failed");
  }
  File dir = SD.open("/root/OthersData");
  File file = dir.openNextFile("r");

  // 先把文件指针空循环到需要的页面
  for (size_t i = 0; i < page * 17; i++)
  {
    file = dir.openNextFile("r");
  }

  uint8_t j = 0;
  // 再从这个位置开始解析文件名
  if (sumpage > page) // 如果总文件数量/17大于当前显示的页面，则表明本页面为满页面，否则表明本页面为非满页面
  {
    Serial.println("满页面");
    for (int i = 0; i < 17; i++) // 获取不同页面下文件的名称
    {

      TXTFilename[j] = file.name();
      j++;
      file = dir.openNextFile("r");
    }
  }
  else
  {
    Serial.println("FEI满页面");
    sumpagefileremain = GetData - 17 * page;    // 最后一页剩余文件数量
    for (int i = 0; i < sumpagefileremain; i++) // 获取不同页面下文件的名称
    {
      TXTFilename[j] = file.name(); // 分割文件
      j++;
      file = dir.openNextFile("r");
    }
    for (int i = sumpagefileremain; i < 17; i++) // 清楚非本页面的显示内容
    {
      TXTFilename[i] = ""; // 将非本页面内容写为空
    }
  }

  dir.close();
  file.close();

  display.setPartialWindow(0, 0, display.width(), display.height());
  u8g2Fonts.setFont(u8g2_font_wqy16_t_gb2312);
  display.firstPage();
  do
  {
    // 如果不存在文件
    if (GetData == 0)
    {
      display.drawInvertedBitmap(8, 17, bitmap_back, 16, 16, GxEPD_BLACK);
      u8g2Fonts.setCursor(26, 28);
      u8g2Fonts.print("返回");

      display.drawInvertedBitmap(224, 14, bitmap_NEXTPAGE, 16, 16, GxEPD_BLACK);
      u8g2Fonts.setCursor(184, 28);
      u8g2Fonts.print("下一页");

      display.drawInvertedBitmap(90, 14, bitmap_FRONTPAGE, 16, 16, GxEPD_BLACK);
      u8g2Fonts.setCursor(104, 28);
      u8g2Fonts.print("上一页");

      u8g2Fonts.setCursor(95, 55);
      u8g2Fonts.print("未找到文件");
    }
    else // 存在文件
    {
      display.drawInvertedBitmap(8, 17, bitmap_back, 16, 16, GxEPD_BLACK);
      u8g2Fonts.setCursor(26, 28);
      u8g2Fonts.print("返回");

      display.drawInvertedBitmap(224, 14, bitmap_NEXTPAGE, 16, 16, GxEPD_BLACK);
      u8g2Fonts.setCursor(184, 28);
      u8g2Fonts.print("下一页");

      display.drawInvertedBitmap(90, 14, bitmap_FRONTPAGE, 16, 16, GxEPD_BLACK);
      u8g2Fonts.setCursor(104, 28);
      u8g2Fonts.print("上一页");

      // 判断pageup还是pagedown用于直接绘制现实的光标
      if (nownext == true)
      {
        display.drawRoundRect(92, 13, 60, 19, 5, GxEPD_BLACK); // 清楚上一页
      }
      else
      {
        display.drawRoundRect(181, 13, 60, 19, 5, GxEPD_BLACK); // 绘制下一页
      }

      // 在规定区域内显示文件（一页暂定显示17个文件）
      int cursory = 61;
      for (int i = 0; i < 17; i++)
      {
        u8g2Fonts.setCursor(26, cursory);
        u8g2Fonts.print(TXTFilename[i]);
        cursory = cursory + 19;
      }
    }

    // 调用文件浏览的光标显示
  } while (display.nextPage());
  // SelectPointerReadMode_File(); //更新光标位置
  display.hibernate();
}

// 用于显示扫描Wifi并在下一个页面中
void WifiScanDisplay()
{
  // 新页面左侧显示扫描到的wifi(5个)，右侧显示二维码

  // 显示新页面
  layer = 3;
  layer3_Mode = ConnectWifiMode;
  display.setPartialWindow(0, 0, display.width(), display.height());
  display.firstPage();
  // 在二维码位置显示正在进入配网模式提示，待生成完毕后再显示二维码
  do
  {
    display.drawInvertedBitmap(100, 12, bitmap_QRBULB, 32, 32, GxEPD_BLACK);
    u8g2Fonts.setFont(chinese_city_gb2312);
    u8g2Fonts.setCursor(93, 60);
    u8g2Fonts.print("Attention:");
    u8g2Fonts.setCursor(68, 78);
    u8g2Fonts.print("生成二维码中");
    u8g2Fonts.setCursor(18, 96);
    u8g2Fonts.print("稍后请扫描二维码填写wifi信息");
  } while (display.nextPage());

  // 调用函数
  initBasic();      // 设置设备名称
  connectNewWifi(); // 尝试连接wifi
  // 建立handle处理函数
  xTaskCreate(RTOS_SetInternetLoop, "RTOS_SetInternetLoop", 1024 * 5, NULL, 1, &TaskWifiloopHandle);
  display.setPartialWindow(0, 0, display.width(), display.height());
  display.firstPage();
  do
  {
    display.clearScreen();
  } while (display.nextPage());

  display.setPartialWindow(0, 0, display.width(), display.height());
  // 显示左上角返回按钮并自动框选
  do
  {
    u8g2Fonts.setCursor(8, 18);
    display.drawInvertedBitmap(8, 8, bitmap_back, 16, 16, GxEPD_BLACK);
    u8g2Fonts.setCursor(26, 20);
    u8g2Fonts.print("返回");
  } while (display.nextPage());
  do
  {
    display.drawRoundRect(5, 23, 82, 19, 3, GxEPD_WHITE);
    display.drawRoundRect(5, 5, 52, 19, 3, GxEPD_BLACK);
  } while (display.nextPage());
  layer3PointerConnectWifi = 0; // 0为框选返回
  // 显示下方提示

  do
  {
    u8g2Fonts.setCursor(8, 46);
    u8g2Fonts.print("先连接到热点:PocketCard");
  } while (display.nextPage());
  do
  {
    u8g2Fonts.setCursor(8, 64);
    u8g2Fonts.print("后扫描右侧二维码填写信息");
  } while (display.nextPage());
  // 显示二维码
  do
  {
    display.drawInvertedBitmap(60, 140, bitmap_QRCODE, 120, 120, GxEPD_BLACK);
  } while (display.nextPage());

  // 左侧显示网页端填写的数据框
  display.hibernate();

  // 创建任务用于实时更新连接状态
  xTaskCreate(WifiMessageupdate, "WifiMessageupdate", 1024 * 6, NULL, 1, &TaskWifiMessageupdate);
}

// 用于显示选择时间模式后时间的函数
void RTOS_TimeShow(void *RTOS_TimeShow)
{
  // 判断是否已进行配网，若已配网并且上次对时已超过x天就再次对时一次
  TimeShow();
  int Month = rx8025.getMonth();
  if (Wifistatus == true)
  {
    // 先判断上次对时的时间(默认一到两个月更新一次)
    if (TimeMenuShow.LastCheckMonth != Month) // 上次对时和现在不是一个月内则对时
    {
      CanGoSleep = false;
      // 在后台进行一次网络对时
      connectNewWifi();
      timeClient.begin(); // 开启timeclient
      xTaskCreate(UpdateTime, "UpdateTime", 1024 * 8, NULL, 1, &TaskUpdateTimeHandle);
    }
    else
    {
      CanGoSleep = true;
    }
  }
  else
  {
    CanGoSleep = true;
  }
  vTaskDelete(NULL);
}

// 用于显示进入阅读界面后长按确认键打开的菜单
void ReadModeReadShowMenu()
{
  PagePercentage = (pageCurrent * 100) / txt_zys;
  Serial.println(PagePercentage);
  // uint8_t CharPagePercentage[2] = {};
  CharPagePercentage[0] = PagePercentage / 10;
  CharPagePercentage[1] = PagePercentage % 10;
  Serial.println(CharPagePercentage[0]);
  Serial.println(CharPagePercentage[1]);

  u8g2Fonts.setFont(chinese_city_gb2312);
  display.setPartialWindow(0, 0, display.width(), display.height());
  display.firstPage();
  do
  {
    display.fillRect(0, 0, 128, 130, GxEPD_WHITE);          // 清除区域
    display.drawRoundRect(-4, 0, 130, 128, 4, GxEPD_BLACK); // 绘制圆角框

    // 显示菜单主区域
    u8g2Fonts.setCursor(8, 20);
    u8g2Fonts.print(RMDM.Readmenu1);
    u8g2Fonts.setCursor(8, 38);
    u8g2Fonts.print(RMDM.Readmenu2);
    u8g2Fonts.setCursor(8, 56);
    u8g2Fonts.print(RMDM.Readmenu3);
    display.drawRoundRect(5, 5, 117, 19, 3, GxEPD_BLACK); // 绘制初始圆角

    // 显示菜单下方区域
    u8g2Fonts.setCursor(8, 79);
    u8g2Fonts.print("当前页:");
    u8g2Fonts.setCursor(57, 78);
    u8g2Fonts.print(pageCurrent);
    u8g2Fonts.setCursor(8, 97);
    u8g2Fonts.print("总页数:");
    u8g2Fonts.setCursor(57, 96);
    u8g2Fonts.print(txt_zys);

    // 显示跳转进度 以百分比显示，仅精确到个位，不精确到小数位
    u8g2Fonts.setCursor(10, 114);
    u8g2Fonts.print("<");
    u8g2Fonts.setCursor(25, 114);
    u8g2Fonts.print("-");

    u8g2Fonts.setCursor(42, 114);
    u8g2Fonts.print(CharPagePercentage[0]); // 显示当前页数位置百分比第一位
    u8g2Fonts.setCursor(57, 114);
    u8g2Fonts.print(CharPagePercentage[1]); // 显示当前页数位置百分比第二位

    u8g2Fonts.setCursor(72, 114);
    u8g2Fonts.print("%");
    u8g2Fonts.setCursor(85, 114);
    u8g2Fonts.print("+");
    u8g2Fonts.setCursor(99, 114);
    u8g2Fonts.print(">");

    u8g2Fonts.setCursor(112, 114);
    u8g2Fonts.print("E"); // 显示Enter

    display.drawRect(42, 117, 6, 3, GxEPD_BLACK); // 自动框选十位
    // 显示电量

  } while (display.nextPage());
  display.hibernate();
}

// 用于显示进入阅读界面长按后的选框
void ReadModeReadShowMenu_Rectangle()
{
  // 判断是否满足进入阅读设置界面
  if (layer == 3 && layer3_Mode == ReadBookMode && layer3_Read_Mode_Set == true)
  {
    display.setPartialWindow(0, 0, display.width(), display.height());
    // 判断选择的哪个选项
    if (layer3PointerReadMode == 0) // 在返回阅读选项上
    {
      do
      {
        display.drawRoundRect(5, 23, 117, 19, 3, GxEPD_WHITE); // 清除圆角
        display.drawRoundRect(5, 5, 117, 19, 3, GxEPD_BLACK);  // 绘制圆角
      } while (display.nextPage());
    }
    else if (layer3PointerReadMode == 1) // 在返回阅读选项上
    {
      do
      {
        display.drawRoundRect(5, 41, 117, 19, 3, GxEPD_WHITE); // 清楚圆角
        display.drawRoundRect(5, 5, 117, 19, 3, GxEPD_WHITE);  // 清楚圆角
        display.drawRoundRect(5, 23, 117, 19, 3, GxEPD_BLACK); // 绘制圆角
      } while (display.nextPage());
    }
    else if (layer3PointerReadMode == 2) // 在返回阅读选项上
    {
      do
      {
        display.drawRoundRect(5, 23, 117, 19, 3, GxEPD_WHITE); // 清楚圆角
        display.drawRoundRect(5, 41, 117, 19, 3, GxEPD_BLACK); // 绘制圆角
      } while (display.nextPage());
    }
    else if (layer3PointerReadMode == 3) // 在左移一位
    {
      do
      {
        display.drawRoundRect(5, 41, 117, 19, 3, GxEPD_WHITE); // 绘制圆角
        display.drawLine(10, 118, 17, 118, GxEPD_BLACK);
        display.drawLine(24, 118, 31, 118, GxEPD_WHITE);
      } while (display.nextPage());
    }
    else if (layer3PointerReadMode == 4) // 在页数减少
    {
      do
      {
        display.drawLine(10, 118, 17, 118, GxEPD_WHITE);
        display.drawLine(24, 118, 31, 118, GxEPD_BLACK);
        display.drawLine(85, 118, 91, 118, GxEPD_WHITE);
      } while (display.nextPage());
    }
    else if (layer3PointerReadMode == 5) // 在页数增加
    {
      do
      {
        display.drawLine(24, 118, 31, 118, GxEPD_WHITE);
        display.drawLine(85, 118, 91, 118, GxEPD_BLACK);
        display.drawLine(97, 118, 104, 118, GxEPD_WHITE);
      } while (display.nextPage());
    }
    else if (layer3PointerReadMode == 6) // 在右移一位
    {
      do
      {
        display.drawLine(85, 118, 91, 118, GxEPD_WHITE);
        display.drawLine(97, 118, 104, 118, GxEPD_BLACK);
        display.drawLine(112, 118, 118, 118, GxEPD_WHITE);
      } while (display.nextPage());
    }
    else if (layer3PointerReadMode == 7) // 在确认跳页按键上
    {
      do
      {
        display.drawLine(97, 118, 104, 118, GxEPD_WHITE);
        display.drawLine(112, 118, 118, 118, GxEPD_BLACK);
      } while (display.nextPage());
      display.hibernate();
    }
    display.hibernate();
  }
}

// 用于分割字符串函数
String fenge(String str, String fen, int index)
{
  int weizhi;
  String temps[str.length()];
  int i = 0;
  do
  {
    weizhi = str.indexOf(fen);
    if (weizhi != -1)
    {
      temps[i] = str.substring(0, weizhi);
      str = str.substring(weizhi + fen.length(), str.length());
      i++;
    }
    else
    {
      if (str.length() > 0)
        temps[i] = str;
    }
  } while (weizhi >= 0);

  if (index > i)
    return "-1";
  return temps[index];
}

// 用于获取当前光标选择的txt文件
String GET_OPEN_TXT_NAME()
{
  txtFile.close();
  // String getTXT;
  String getTXT = TXTFilename[layer2PointerReadFile - 3];
  // 打开为全局
  txtFile = SD.open("/root/OthersData/" + getTXT, "r"); // 选择txt文件，只限中文路径和名称
  return getTXT;
}

// 获取SD卡目录函数
void GetSDCardMenu()
{
  if (!SD.begin(SD_CS, hspi))
  {
    Serial.println("SD卡加载失败!");
    return;
  }

  // 先判断是否存在系统文件夹
  if (!SD.exists("/root"))
  {
    SD.mkdir("/root/Music");      // 音乐文件
    SD.mkdir("/root/UserData");   // 用户数据
    SD.mkdir("/root/OthersData"); // 小说文件夹
    SD.mkdir("/root/SystemData"); // 系统数据如wifi
    SD.mkdir("/root/BookDecode"); // 小说解析
  }
  else // 否则直接打开阅读模式界面
  {
    layer = 2;
    layer2_Mode = ReadMode;
    OtherDataShow(); // 显示阅读模式界面
  }
}

// 进入配网模式需要循环运行的函数
void RTOS_SetInternetLoop(void *RTOS_SetInternetLoop)
{
  while (1)
  {
    WifiConnect();
    vTaskDelay(30);
  }
}

// 用于在配网模式下动态显示当前配网信息（结束配网应杀掉该任务）
void WifiMessageupdate(void *WifiMessageupdate)
{
  String SSIDOLD = TransportSSID();
  String KeywordOLD = TransportKeyWord();
  while (1)
  {
    // 判断新旧账号密码是否相同
    String SSIDNOW = TransportSSID();
    String KeywordNOW = TransportKeyWord();
    if (1) // 如果不同 SSIDNOW != SSIDOLD || KeywordNOW != KeywordOLD
    {
      SSIDOLD = SSIDNOW;
      KeywordOLD = KeywordNOW;
      display.setPartialWindow(0, 0, display.width(), display.height());
      do
      {
        u8g2Fonts.setCursor(57, 82);
        u8g2Fonts.print("Connect to:");
        u8g2Fonts.setCursor(8, 100);
        u8g2Fonts.print("SSID");
        u8g2Fonts.setCursor(8, 118);
        u8g2Fonts.print("KEY");
        u8g2Fonts.setCursor(40, 100);
        u8g2Fonts.print(SSIDNOW);
        u8g2Fonts.setCursor(40, 118);
        u8g2Fonts.print(KeywordNOW);
      } while (display.nextPage());

      display.hibernate();
      // 系统标记配网成功
      vTaskDelay(10000 / portTICK_PERIOD_MS); // 等待5s获取wifi连接的状态
      if (IsConnectOK() == true)
      {
        Wifistatus = true;
        CanGoSleep = false; // 如果配网成功标记打开时钟模式不自动进入低功耗模式
        // 将配网成功信息写入SD卡指定目录（文件位置， 文件名称WIFI名称， 文件内容WIFI密码）
        WriteSDCardMessage("/root/SystemData", "WIFI.wifi", SSIDNOW + "\r\n" + KeywordNOW);
        // 显示wifi已连接提示
        u8g2Fonts.setFont(chinese_city_gb2312);
        do
        {
          display.fillRect(100, 82, 80, 18, GxEPD_WHITE); // 填充白色
          u8g2Fonts.setCursor(100, 100);
          u8g2Fonts.print("已连接");
        } while (display.nextPage());
        display.hibernate();
      }
      else
      {
        u8g2Fonts.setFont(chinese_city_gb2312);
        do
        {
          // display.drawRect(100, 100, 72, 18, GxEPD_WHITE);  //填充白色
          display.fillRect(100, 82, 80, 36, GxEPD_WHITE); // 填充白色
          u8g2Fonts.setCursor(100, 100);
          u8g2Fonts.print("连接失败");
        } while (display.nextPage());
        display.hibernate();
      }

      // 不点击返回可进入网页操作
    }
    vTaskDelay(200 / portTICK_PERIOD_MS);
  }
}

// 用于进行一次配网对时
void UpdateTime(void *UpdateTime)
{
  bool a = WifiEasyConnect();
  Serial.print("是否连接上wifi:");
  Serial.println(a);
  bool b;
  if (a == true)
  {
    CanGoSleep = false; // 已连接网络无需进入低功耗模式
    // 判断当前连接的网络是否能获取网络时间
    while (!timeClient.update())
    {
      delay(500);
      if (timeClient.update() == true)
      {
        break;
      }
    }
    b = true; //
    // bool b = timeClient.update();
    Serial.print("是否能获取网络时间:");
    Serial.println(b);
    if (b == true)
    {
      int Hour = timeClient.getHours();          // 时
      int Minute = timeClient.getMinutes();      // 分
      int Sec = timeClient.getSeconds();         // 秒
      int epochTime = timeClient.getEpochTime(); // 获取epochtime
      struct tm *ptm = gmtime((time_t *)&epochTime);
      int Year = ptm->tm_year + 1900 - 2000; // 年
      int Month = ptm->tm_mon + 1;           // 月
      int Day = ptm->tm_mday;                // 日期
      int Wday = ptm->tm_wday;               // 星期

      // 同步RTC时钟芯片
      rx8025.setRtcTime(Sec, Minute, Hour, Wday, Day, Month, Year);
      Serial.print("已同步RTC时钟");

      // 写入内存介质
      WriteSDCardMessage("/root/SystemData", "LastCheckTimeMonth.month", (String)Month);

      delay(100);
    }

    // 断开网络连接
    if (WifiDisConnect() == true)
    {
      Serial.println("已断开网络连接");
    }
    CanGoSleep = true; // 断开网络后可以进入低功耗模式
  }
  else
  {
    CanGoSleep = true;
  }

  vTaskDelete(NULL); // 自杀
}

// 用于进入时钟界面，并在时钟上显示进入时钟原因
void SystemSleep()
{
  CanGoSleep = false;
  layer = 2;                                                                        // 进入第二层
  layer2_Mode = ClockMode;                                                          // 进入第二层时钟模式
  xTaskCreate(RTOS_TimeShow, "RTOS_TimeShow", 1024 * 3, NULL, 1, &TaskClockHandle); // 用于判断是否联网从而进行对时
}

// 解析txt文件
void TXTDecode(String bookname)
{
  //  判断文件是否存在过解析文件（后缀 文件名称.mytxt）
  String booknamemytxt = bookname + ".i";                 //.i为索引主文件booknamemytxt的名称               booknamemytxt
  FilePath = "/root/BookDecode/" + booknamemytxt;         // 索引主文件 booknamemytxt存储的位置  FilePath
  String FilePathSource = "/root/OthersData/" + bookname; // 打开的小说 文件存放的路径                 FilePathSource
  bool ISFile_find = true;                                // 是否发现该书的索引文件

  // 判断是否存在该书籍的解析文件
  // SD.open("/root/BookDecode");
  if (!SD.exists(FilePath))
  {
    Serial.println("不存在解析文件");
    ISFile_find = false;
  }
  else
  {
    Serial.println("存在解析文件");
    // file.close();
    ISFile_find = true;
  }
  // dir.close();

  // 如果存在解析文件则直接打开书籍开始阅读
  if (ISFile_find == true)
  {
    layer = 3;                  // 第三层
    layer3_Mode = ReadBookMode; // 设置第二层模式为ReadBookMode
    IS_File_Open = true;
    TXT_nowTime = millis();
    TXT_overTime = millis(); // 获取当前时间用于阅读模式超时使用
    PageDecode(true);        // 直接打开文件
    setCpuFrequencyMhz(80);
  }
  else
  {
    Serial.println("未发现该书名下的解析文件");
    // 先创建解析文件

    File file = SD.open(FilePath.c_str(), "w", true);
    file.close();

    Serial.println("检测到文件第一次打开，创建文件"); // 串口提示

    display.setPartialWindow(0, 0, display.width(), display.height());
    u8g2Fonts.setFont(u8g2_font_wqy15_t_gb2312);
    // 刷白一个小区域
    do
    {
      display.drawRect(5, 180, 230, 60, GxEPD_WHITE);
    } while (display.nextPage());
    do
    {
      u8g2Fonts.setCursor(16, 200);
      u8g2Fonts.print("正在生成索引,预计1-5min结束");
    } while (display.nextPage());

    setCpuFrequencyMhz(240);
    RECodeTXT(SD, FilePathSource.c_str(), FilePath.c_str());
    setCpuFrequencyMhz(80);

    layer = 3;                  // 第三层
    layer3_Mode = ReadBookMode; // 设置第二层模式为ReadBookMode
    IS_File_Open = true;
    TXT_nowTime = millis();
    TXT_overTime = millis(); // 获取当前时间用于阅读模式超时使用
    PageDecode(true);        // 创建完阅读页数文件后打开小说
  }
}

void FixedRefresh() // 定次刷新,每5次刷黑白一次，每15次正式全局刷黑白
{
  // 每6次快速全屏刷新一次，第30次完全全屏刷新
  if (pageUpdataCount == 30) // 第30次完全刷新
  {
    // do
    // {
    //   display.clearScreen();
    // } while (display.nextPage());
    // display.hibernate();
    // display.init(115200);

    pageUpdataCount = 0;
  }
  else if (pageUpdataCount % 20 == 0) // 每6次黑白刷新一次
  {
    do
    {
      display.fillScreen(GxEPD_BLACK);
    } while (display.nextPage());
    do
    {
      display.fillScreen(GxEPD_WHITE);
    } while (display.nextPage());
    display.hibernate();
  }
  pageUpdataCount++;
}

void DisplayTXT(String *txt) // 仅仅用于显示阅读书界面
{
  u8g2Fonts.setFont(u8g2_font_wqy16_t_gb2312);
  display.setPartialWindow(0, 0, display.width(), display.height());
  display.firstPage();
  do
  {
    for (uint8_t i = 0; i < 23; i++)
    {
      uint8_t offset = 0;    // 缩减偏移量
      if (txt[i][0] == 0x20) // 检查首行是否为半角空格 0x20
      {
        // 继续检测后3位是否为半角空格，检测到连续的4个半角空格，偏移12个像素
        if (txt[i][1] == 0x20 && txt[i][2] == 0x20 && txt[i][3] == 0x20)
          offset = 0;
      }
      else if (txt[i][0] == 0xE3 && txt[i][1] == 0x80 && txt[i][2] == 0x80) // 检查首行是否为全角空格 0x3000 = E3 80 80
      {
        // 继续检测后2位是否为全角空格，检测到连续的2个全角空格，偏移2个像素
        if (txt[i][3] == 0xE3 && txt[i][4] == 0x80 && txt[i][5] == 0x80)
          offset = 2;
      }

      u8g2Fonts.setCursor(2 + offset, i * 18 + 25);
      u8g2Fonts.print(txt[i]);
      // Serial.println(txt[i]);
      // u8g2Fonts.getUTF8Width();
    }
  } while (display.nextPage());
  // delay(1);
  display.hibernate();
}

// 页数解析，懒得重写了
void PageDecode(bool PAGE)
{
  TXT_overTime = millis(); // 更新一次暂停操作时间
  // String indexesName = "/root/BookDecode/" + NowOpenTXTname + ".i"; // 定义索引文件名字格式 xxx.txt.i
  // indexesFile = SD.open(FilePath, "r");                          // 打开索引文件
  //  txtFile = SD.open("/root/OthersData/" + NowOpenTXTname, "r");   //打开txt文件，只限中文路径和名称
  //   检测当前页面
  //   查看索引末14-1的位数据
  String txt_syys_str = ""; // 索引记录的页数
  String txt_sydx_str = ""; // 索引记录的文件大小

  indexesFile = SD.open(FilePath, "r"); // 打开索引文件

  txt_zys = (indexesFile.size() / 7) - 1; // 获取总页数

  indexesFile.seek(-14, SeekEnd); // 从末尾开始偏移14位

  for (uint8_t i = 0; i < 7; i++) // 获取索引末14-8位，记录txt文件大小用
  {
    char c = indexesFile.read();
    txt_sydx_str += c;
  }
  uint32_t txt_sydx_uint32 = atol(txt_sydx_str.c_str()); // 转换成int格式
  // Serial.println(txt_sydx_uint32);

  for (uint8_t i = 0; i < 7; i++) // 获取索引末7-1位，记录上一次打开的页数
  {
    char c = indexesFile.read();
    txt_syys_str += c;
  }
  uint32_t txt_syys_uint32 = atol(txt_syys_str.c_str()); // 转换成int格式

  pageCurrent = txt_syys_uint32; // 索引末7-1位转换后的值就是当前页

  indexesFile.close();

  if (IS_File_Open == true) // 如果刚打开TXT显示，无法显示存储到的页数，所以需要该选择来直接偏移
  {
    if (pageCurrent == 0)
    {
      PageChange = 1;
    }
    else
    {
      pageCurrent += 1;
      ReadTXT_UP_PAGE(); // 第2页以上则利用上一页命令来偏移到指定位置，但要加多一页，发送上一页指令
    }
    IS_File_Open = false;
  }

  Serial.print("当前页数:");
  Serial.println(pageCurrent);

  FixedRefresh();
  String txt[22 + 1] = {}; // 0-19行为一页 共20行
  int8_t line = 0;         // 当前行
  char c;                  // 中间数据
  uint16_t en_count = 0;   // 统计ascii和ascii扩展字符 1-2个字节
  uint16_t ch_count = 0;   // 统计中文等 3个字节的字符
  uint8_t line_old = 0;    // 记录旧行位置
  boolean hskgState = 1;   // 行首4个空格检测 0-检测过 1-未检测
  // int byteIndex;           // 文件开始读取的位置
  // int LastbyteIndex = 0;   // 上一次文件开始读取的位置

  // indexesFile = SD.open(FilePath.c_str(), "r"); // 打开索引文件，可读可写
  // String content = indexesFile.readString();
  // indexesFile.close();
  // byteIndex = content.toInt();

  if (PAGE == false) // PAGE:fasle-上一页 true:下一页
  {
    // txtFile.seek(LastbyteIndex);
    ReadTXT_UP_PAGE();
  }
  else
  {
    // txtFile.seek(byteIndex);
  }
  // txtFile = SD.open("/root/OthersData/" + NowOpenTXTname);
  while (line < 22)
  {
    if (line_old != line) // 行首4个空格检测状态重置
    {
      line_old = line;
      hskgState = 1;
    }

    c = txtFile.read(); // 读取一个字节

    while (c == '\n' && line <= 21) // 检查换行符,并将多个连续空白的换行合并成一个
    {

      // 检测到首行并且为空白则不需要插入换行
      if (line == 0) // 等于首行，并且首行不为空，才插入换行
      {
        if (txt[line].length() > 0)
          line++; // 换行
        else
          txt[line].clear();
      }
      else // 非首行的换行检测
      {
        // 连续空白的换行合并成一个
        if (txt[line].length() > 0)
          line++;
        else if (txt[line].length() == 0 && txt[line - 1].length() > 0)
          line++;
        /*else if (txt[line].length() == 1 && txt[line - 1].length() == 1) hh = 0;*/
      }

      if (line <= 21)
        c = txtFile.read();
      // byteIndex++;
      en_count = 0;
      ch_count = 0;
    }

    if (c == '\t') // 检查水平制表符 tab
    {
      if (txt[line].length() == 0)
        txt[line] += "    "; // 行首的一个水平制表符 替换成4个空格
      else
        txt[line] += "       "; // 非行首的一个水平制表符 替换成7个空格
    }
    else if ((c >= 0 && c <= 31) || c == 127) // 检查没有实际显示功能的字符
    {
    }
    else
      txt[line] += c;
    // 检查字符的格式 + 数据处理 + 长度计算
    boolean asciiState = 0;
    byte a = B11100000;
    byte b = c & a;

    if (b == B11100000) // 中文等 3个字节
    {
      ch_count++;
      c = txtFile.read();
      // byteIndex += 3;
      txt[line] += c;
      c = txtFile.read();
      // byteIndex += 3;
      txt[line] += c;
    }
    else if (b == B11000000) // ascii扩展 2个字节
    {
      en_count += 14;
      c = txtFile.read();
      // byteIndex + 2;
      txt[line] += c;
    }
    else if (c == '\t') // 水平制表符，代替两个中文位置，14*2
    {
      if (txt[line] == "    ")
        en_count += 20; // 行首，因为后面会检测4个空格再加8所以这里是20
      else
        en_count += 28; // 非行首
    }
    else if (c >= 0 && c <= 199)
    {
      en_count += getCharLength(c) + 1;
      asciiState = 1;
    }

    uint16_t StringLength = en_count + (ch_count * 16); // 14为字体大小(ch_count * 14)

    if (StringLength >= 204 && hskgState) // 检测到行首的4个空格预计的长度再加长一点
    {
      if (txt[line][0] == ' ' && txt[line][1] == ' ' &&
          txt[line][2] == ' ' && txt[line][3] == ' ')
      {
        en_count += 8;
      }
      hskgState = 0;
    }

    if (StringLength >= 227) // 检查是否已填满屏幕 283
    {
      if (asciiState == 0) // 最后一个字符是中文，直接换行
      {
        line++;
        en_count = 0;
        ch_count = 0;
      }
      else if (StringLength >= 230) // 286 最后一个字符不是中文，在继续检测
      {

        char t = txtFile.read();
        // byteIndex++;
        txtFile.seek(-1, SeekCur); // 往回移
        int8_t cz = 238 - StringLength;
        int8_t t_length = getCharLength(t);

        byte a = B11100000;
        byte b = t & a;
        if (b == B11100000 || b == B11000000) // 中文 ascii扩展
        {
          line++;
          en_count = 0;
          ch_count = 0;
          // Serial.println("测试2");
        }
        else if (t_length > cz)
        {
          line++;
          en_count = 0;
          ch_count = 0;
          // Serial.println("测试3");
        }
      }
    }
  }
  // indexesFile = SD.open(FilePath.c_str(), "r+"); // 打开索引文件，可读可写
  // byteIndex = txtFile.position();
  // Serial.print("读到的位置:");
  // Serial.println(txtFile.position());
  // indexesFile.print(byteIndex); // 写入数据
  // indexesFile.close();
  DisplayTXT(txt);

  pageCurrent++; // 当前页加1
  // 将当前页存入索引文件的末尾七位
  uint32_t yswz_uint32 = pageCurrent;
  String yswz_str = "";
  if (yswz_uint32 >= 1000000)
    yswz_str += String(yswz_uint32);
  else if (yswz_uint32 >= 100000)
    yswz_str += String("0") + String(yswz_uint32);
  else if (yswz_uint32 >= 10000)
    yswz_str += String("00") + String(yswz_uint32);
  else if (yswz_uint32 >= 1000)
    yswz_str += String("000") + String(yswz_uint32);
  else if (yswz_uint32 >= 100)
    yswz_str += String("0000") + String(yswz_uint32);
  else if (yswz_uint32 >= 10)
    yswz_str += String("00000") + String(yswz_uint32);
  else
    yswz_str += String("000000") + String(yswz_uint32);

  indexesFile = SD.open(FilePath, "r+"); // 打开索引文件，可读可写
  indexesFile.seek(-7, SeekEnd);         // 从末尾开始偏移7位
  indexesFile.print(yswz_str);           // 写入数据
  indexesFile.close();
}

void ReadTXT_UP_PAGE() // 翻页之上一页
{
  uint32_t gbwz = 0;                        // 计算上一页的页首位置
  String gbwz_str = "";                     // 光标位置String
  if (pageCurrent == 1 || pageCurrent == 0) // 第0,1页不允许上一页
  {
    PageChange = 0;
    return;
  }
  else if (pageCurrent == 2)
  {
    txtFile.seek(0, SeekSet); // 偏移 第二页的上一页的页首位置就是0
    PageChange = 1;           // 发送下一页指令
  }
  else
  {
    // Serial.print("当前页1："); Serial.println(pageCurrent);
    // 计算上一页的页首位置
    // 因为第一页不需要记录所以要减1，因为我要的是上一页所以再减1
    gbwz = (pageCurrent - 2) * 7 - 7;
    // Serial.print("gbwz："); Serial.println(gbwz);
    // 打开索引，寻找上一页的页首位置
    indexesFile = SD.open("/root/BookDecode/" + NowOpenTXTname + ".i", "r");
    indexesFile.seek(gbwz, SeekSet);
    // 获取索引的数据
    for (uint8_t i = 0; i < 7; i++)
    {
      char c = indexesFile.read();
      gbwz_str += c;
    }
    uint32_t gbwz_uint32 = atol(gbwz_str.c_str()); // 装换成int格式
    indexesFile.close();

    txtFile.seek(gbwz_uint32, SeekSet); // 索引的数据就是TXT文件的偏移量
    // Serial.print("偏移量：");
    // Serial.println(gbwz_uint32);
    PageChange = 1; // 发送下一页指令
  }
  pageCurrent -= 2; // 屏幕读取后会加1页，所以减2
                    // 将当前页存入索引文件的末尾七位
  uint32_t yswz_uint32 = pageCurrent;
  String yswz_str = "";
  if (yswz_uint32 >= 1000000)
    yswz_str += String(yswz_uint32);
  else if (yswz_uint32 >= 100000)
    yswz_str += String("0") + String(yswz_uint32);
  else if (yswz_uint32 >= 10000)
    yswz_str += String("00") + String(yswz_uint32);
  else if (yswz_uint32 >= 1000)
    yswz_str += String("000") + String(yswz_uint32);
  else if (yswz_uint32 >= 100)
    yswz_str += String("0000") + String(yswz_uint32);
  else if (yswz_uint32 >= 10)
    yswz_str += String("00000") + String(yswz_uint32);
  else
    yswz_str += String("000000") + String(yswz_uint32);

  indexesFile = SD.open("/root/BookDecode/" + NowOpenTXTname + ".i", "r+"); // 打开索引文件，可读可写
  indexesFile.seek(-7, SeekEnd);                                            // 从末尾开始偏移7位
  indexesFile.print(yswz_str);                                              // 写入数据
  Serial.print("写入的数据是:");
  Serial.println(yswz_str);
  indexesFile.close();
}

void audioEject()
{
  // 切断音频芯片供电
  // pinMode(12, OUTPUT);
  digitalWrite(12, LOW);
  // pinMode(13, OUTPUT);
  digitalWrite(13, LOW);
  // 结束RTOS任务
  if (TaskAudioloopHandle != NULL)
  {
    vTaskDelete(TaskAudioloopHandle);
  }
}

void audioTask(void *parameter)
{
  CreateQueues();
  if (!audioSetQueue || !audioGetQueue)
  {
    log_e("queues are not initialized");
    while (true)
    {
      ;
    } // endless loop
  }

  struct audioMessage audioRxTaskMessage;
  struct audioMessage audioTxTaskMessage;

  audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
  audio.setVolume(25); // 0...21

  while (true)
  {
    if (xQueueReceive(audioSetQueue, &audioRxTaskMessage, 1) == pdPASS)
    {
      if (audioRxTaskMessage.cmd == SET_VOLUME)
      {
        audioTxTaskMessage.cmd = SET_VOLUME;
        audio.setVolume(audioRxTaskMessage.value);
        audioTxTaskMessage.ret = 1;
        xQueueSend(audioGetQueue, &audioTxTaskMessage, portMAX_DELAY);
      }
      else if (audioRxTaskMessage.cmd == CONNECTTOHOST)
      {
        audioTxTaskMessage.cmd = CONNECTTOHOST;
        audioTxTaskMessage.ret = audio.connecttohost(audioRxTaskMessage.txt);
        xQueueSend(audioGetQueue, &audioTxTaskMessage, portMAX_DELAY);
      }
      else if (audioRxTaskMessage.cmd == CONNECTTOSD)
      {
        audioTxTaskMessage.cmd = CONNECTTOSD;
        audioTxTaskMessage.ret = audio.connecttoSD(audioRxTaskMessage.txt);
        xQueueSend(audioGetQueue, &audioTxTaskMessage, portMAX_DELAY);
      }
      else if (audioRxTaskMessage.cmd == GET_VOLUME)
      {
        audioTxTaskMessage.cmd = GET_VOLUME;
        audioTxTaskMessage.ret = audio.getVolume();
        xQueueSend(audioGetQueue, &audioTxTaskMessage, portMAX_DELAY);
      }
      else
      {
        log_i("error");
      }
    }
    vTaskDelay(1 / portTICK_PERIOD_MS);
    audio.loop();
  }
}

void audioInit()
{
  // 提高cpu频率
  setCpuFrequencyMhz(240);
  // 初始化IO
  pinMode(12, OUTPUT);
  digitalWrite(12, HIGH);
  pinMode(13, OUTPUT);
  digitalWrite(13, 1);
  // // 设置audio芯片引脚
  // audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
  // // 设置audio初始音量
  // audio.setVolume(13); // 0...21

  xTaskCreatePinnedToCore(
      audioTask,             /* Function to implement the task */
      "audioplay",           /* Name of the task */
      5000,                  /* Stack size in words */
      NULL,                  /* Task input parameter */
      2 | portPRIVILEGE_BIT, /* Priority of the task */
      &TaskAudioloopHandle,  /* Task handle. */
      1                      /* Core where the task should run */
  );
}

audioMessage transmitReceive(audioMessage msg)
{
  xQueueSend(audioSetQueue, &msg, portMAX_DELAY);
  if (xQueueReceive(audioGetQueue, &audioRxMessage, portMAX_DELAY) == pdPASS)
  {
    if (msg.cmd != audioRxMessage.cmd)
    {
      log_e("wrong reply from message queue");
    }
  }
  return audioRxMessage;
}

void audioSetVolume(uint8_t vol)
{
  audioTxMessage.cmd = SET_VOLUME;
  audioTxMessage.value = vol;
  audioMessage RX = transmitReceive(audioTxMessage);
}

uint8_t audioGetVolume()
{
  audioTxMessage.cmd = GET_VOLUME;
  audioMessage RX = transmitReceive(audioTxMessage);
  return RX.ret;
}

bool audioConnecttohost(const char *host)
{
  audioTxMessage.cmd = CONNECTTOHOST;
  audioTxMessage.txt = host;
  audioMessage RX = transmitReceive(audioTxMessage);
  return RX.ret;
}

bool audioConnecttoSD(const char *filename)
{
  audioTxMessage.cmd = CONNECTTOSD;
  audioTxMessage.txt = filename;
  audioMessage RX = transmitReceive(audioTxMessage);
  return RX.ret;
}

void audio_info(const char *info)
{
  Serial.print("info        ");
  Serial.println(info);
}

// 用于显示音乐模式内页面
void MucicPlayerShow(uint32_t page, uint32_t nowORnext)
{

  if (!SD.begin(SD_CS, hspi))
  {
    Serial.println("SD卡加载失败!");
    return;
  }
  if (!SD.exists("/root/Music"))
  {
    SD.mkdir("/root/Music");
  }
  if (!SD.open("/root/Music"))
  {
    Serial.println("dir.open failed");
  }

  File dir = SD.open("/root/Music");
  File file = dir.openNextFile("r");
  while (file)
  {
    if (file.isDirectory()) // 如果文件是一个目录，则不读取
    {
    }
    else
    {
      if (MusicSUM < 17)
      {
        Filename[MusicSUM] = file.name();
        if (Filename[MusicSUM].endsWith(".mp3") || Filename[MusicSUM].endsWith(".aac"))
        {
          MusicSUM++; // 可以过滤掉非MP3文件
        }
      }
      // 否则就是一个标准的文件，先存入
      // MusicSUM++;
    }
    file = dir.openNextFile();
  }
  file.close();

  Serial.print("音乐的总数量:");
  Serial.println(MusicSUM);
  // 如果小于17则只有单页
  if (MusicSUM <= 17)
  {
    // sumpagefileremain = GetData;
  }
  Musicsumpage = MusicSUM / 18; // 更新总页数
  Serial.print("总页面:\n");
  Serial.println(Musicsumpage);
  // layer2PointerReadFile = 0; // 设置初始化光标位置

  display.setPartialWindow(0, 0, display.width(), display.height());
  u8g2Fonts.setFont(u8g2_font_wqy16_t_gb2312);
  display.firstPage();
  do
  {
    // 如果不存在文件
    if (MusicSUM == 0)
    {
      display.drawInvertedBitmap(8, 17, bitmap_back, 16, 16, GxEPD_BLACK);
      u8g2Fonts.setCursor(26, 28);
      u8g2Fonts.print("返回");

      display.drawInvertedBitmap(224, 14, bitmap_NEXTPAGE, 16, 16, GxEPD_BLACK);
      u8g2Fonts.setCursor(184, 28);
      u8g2Fonts.print("下一页");

      display.drawInvertedBitmap(90, 14, bitmap_FRONTPAGE, 16, 16, GxEPD_BLACK);
      u8g2Fonts.setCursor(104, 28);
      u8g2Fonts.print("上一页");

      u8g2Fonts.setCursor(95, 55);
      u8g2Fonts.print("未找到文件");
    }
    else // 存在文件
    {
      display.drawInvertedBitmap(8, 17, bitmap_back, 16, 16, GxEPD_BLACK);
      u8g2Fonts.setCursor(26, 28);
      u8g2Fonts.print("返回");

      display.drawInvertedBitmap(224, 14, bitmap_NEXTPAGE, 16, 16, GxEPD_BLACK);
      u8g2Fonts.setCursor(184, 28);
      u8g2Fonts.print("下一页");

      display.drawInvertedBitmap(90, 14, bitmap_FRONTPAGE, 16, 16, GxEPD_BLACK);
      u8g2Fonts.setCursor(104, 28);
      u8g2Fonts.print("上一页");
      // 在规定区域内显示文件（一页暂定显示8个文件）
      int cursory = 61;
      for (int i = 0; i < MusicSUM; i++)
      {
        u8g2Fonts.setCursor(26, cursory);
        u8g2Fonts.print(Filename[i]);
        cursory = cursory + 19;
      }
      // display.drawRect(224, 42, 50, 300, GxEPD_WHITE);
      display.drawInvertedBitmap(74, 384, bitmap_upMusic16, 16, 16, GxEPD_BLACK);
      display.drawInvertedBitmap(150, 384, bitmap_nextMusic16, 16, 16, GxEPD_BLACK);
      display.drawInvertedBitmap(28, 384, bitmap_uppageMusic16, 16, 16, GxEPD_BLACK);
      display.drawInvertedBitmap(196, 384, bitmap_nextpageMusic16, 16, 16, GxEPD_BLACK);
      display.drawInvertedBitmap(112, 384, bitmap_musicplay, 16, 16, GxEPD_BLACK);
    }
    display.drawRoundRect(5, 12, 55, 19, 5, GxEPD_BLACK);
    // 调用文件浏览的光标显示
  } while (display.nextPage());

  display.hibernate();
  SelectPointer_Musicplayer();
}

// 电量测量函数
int BATCaculate()
{
  int sum;
  int powerPecentage = analogRead(ADC_BAT);

  if (BatteryCaculate.PowerOn == true) // 如果刚开机，则BatteryCaculate.BAT_Value_buf[]中所有值都一致
  {
    BatteryCaculate.BAT_Value_buf[7] = map(powerPecentage, 1680, 2400, 0, 100); // 如果初次开机无数据，则赋值当前采集次数,最后一个数权最高
    for (size_t i = 1; i < 8; i++)
    {
      BatteryCaculate.BAT_Value_buf[i] = BatteryCaculate.BAT_Value_buf[7];
    }
    BatteryCaculate.PowerOn = false;
  }
  else  //否则前面已经存在数据了
  {
    // 使所有数据全向前挪动一位
    for (int i = 0; i < 7; i++)
    {
      BatteryCaculate.BAT_Value_buf[i] = BatteryCaculate.BAT_Value_buf[i + 1];
    }
    BatteryCaculate.BAT_Value_buf[7] = map(powerPecentage, 1680, 2400, 0, 100); //赋值最高位
  }

  for (uint8_t i = 0; i < 8; i++) // 计算加权值
  {
    sum += BatteryCaculate.BAT_Value_buf[i] * BatteryCaculate.weight[i];
  }

  return sum / BatteryCaculate.weight_sum;
}

// 用于音乐模式内页面 绘制光标
void SelectPointer_Musicplayer(uint16_t p)
{
  display.setPartialWindow(0, 0, display.width(), display.height());
  if (p == 0) // 在返回上
  {
    do
    {
      display.drawRoundRect(5, 12, 55, 19, 5, GxEPD_BLACK);
      // display.drawCircle(13, 69, 10, GxEPD_BLACK);
      display.drawCircle(13, 55, 8, GxEPD_WHITE);
    } while (display.nextPage());
  }
  else if (p == 1) // 在
  {
    do
    {
      display.drawRoundRect(5, 12, 55, 19, 5, GxEPD_WHITE);
      display.drawCircle(13, 55, 8, GxEPD_BLACK);
      display.drawCircle(13, 74, 8, GxEPD_WHITE);
    } while (display.nextPage());
  }
  else if (p <= 17) // 在
  {
    do
    {
      display.drawCircle(13, 55 + 19 * (p - 2), 8, GxEPD_WHITE);
      display.drawCircle(13, 55 + 19 * (p - 1), 8, GxEPD_BLACK);
      display.drawCircle(13, 55 + 19 * p, 8, GxEPD_WHITE);
    } while (display.nextPage());
  }
  display.hibernate();
}

// 用于显示番茄钟内页面
void PotatoClockPage()
{
  u8g2Fonts.setFont(u8g2_font_wqy16_t_gb2312);
  display.setPartialWindow(0, 0, display.width(), display.height());
  display.firstPage();
  do
  {
    display.drawInvertedBitmap(8, 17, bitmap_back, 16, 16, GxEPD_BLACK);
    u8g2Fonts.setCursor(26, 28);
    u8g2Fonts.print("返回");
    display.drawBitmap(56, 130, bitmap_potato45, 128, 128, GxEPD_BLACK);
    // display.drawRoundRect(20, 360, 200, 30, 7, GxEPD_BLACK);
    u8g2Fonts.setCursor(67, 352);
    u8g2Fonts.print("开 始 番 茄 钟");
  } while (display.nextPage());
  display.hibernate();
}
// 用于选择番茄钟内页面的 绘制光标
void SelectPointer_PotatoClockPage(uint16_t p)
{
  display.setPartialWindow(0, 0, display.width(), display.height());
  if (p == 0) // 在返回上
  {
    do
    {
      display.drawRoundRect(5, 12, 55, 19, 5, GxEPD_BLACK);
      display.drawRoundRect(20, 330, 200, 30, 7, GxEPD_WHITE);
    } while (display.nextPage());
  }
  if (p == 1) // 在返回上
  {

    do
    {
      display.drawRoundRect(5, 12, 55, 19, 5, GxEPD_WHITE);
      display.drawRoundRect(20, 330, 200, 30, 7, GxEPD_BLACK);
    } while (display.nextPage());
  }
  display.hibernate();
}
// 用于番茄钟 点击开始番茄钟计时后界面刷新部分
void PotatoClockStartTick_RefreshScreen()
{
  // 计算居中位置
  int cursor = (240 - (45 * 2 + 5)) / 2;
  // 番茄上移
  do
  {
    display.fillRect(56, 130, 128, 128, GxEPD_WHITE); // 刷白老番茄位置
  } while (display.nextPage());
  do
  {
    display.drawBitmap(56, 70, bitmap_potatobuttom128, 128, 128, GxEPD_BLACK); // 绘制新番茄位置和倒计时位置
    u8g2Fonts.setFont(u8g2_font_inb63_mn);
    u8g2Fonts.setCursor(cursor, 280);
    u8g2Fonts.print(PotatoMenuShow.SetPotatoPeriod - 1); // 模拟倒计时

    u8g2Fonts.setFont(u8g2_font_wqy16_t_gb2312);
    u8g2Fonts.setCursor(180, 280);
    u8g2Fonts.print("Potato"); // 右下角小单位

    u8g2Fonts.setCursor(67, 352);
    u8g2Fonts.print("正 在 番 茄 钟");
  } while (display.nextPage());
  display.hibernate();

  // 创建一个RTOS任务用于切换番茄钟
  xTaskCreate(RTOS_PotatoClockClock, "RTOS_PotatoClockClock", 1024 * 6, NULL, 1, &TaskPotatoClockHandle);
}

void RTOS_PotatoClockClock(void *RTOS_PotatoClockClock)
{
  int time_period; // 获取一次设定的番茄钟
  int cursor = (240 - (45 * 2 + 5)) / 2;
  bool InPotato = true;
  String time_s;
  while (1)
  {
    time_period = PotatoMenuShow.SetPotatoPeriod;
    while (InPotato)
    {
      PotatoMenuShow.PotatoISINClock = true;
      PotatoMenuShow.PotatoISINRest = false;
      vTaskDelay(1000 * 60);
      time_period--;
      // 开启一个倒计时
      if (time_period < 10 && time_period > 0) // 如果小于10分钟，则额外在显示的前面补0
      {
        time_s = "0" + (String)time_period;
      }
      else
      {
        time_s = time_period;
      }
      // 刷新屏幕时钟
      Serial.println(time_s);
      do
      {
        u8g2Fonts.setFont(u8g2_font_inb63_mn);
        u8g2Fonts.setCursor(cursor, 280);
        u8g2Fonts.print(time_s); // 模拟倒计时
      } while (display.nextPage());
      // display.hibernate();

      if (time_period == 0)
      {
        time_s = "00";
        do
        {
          u8g2Fonts.setFont(u8g2_font_inb63_mn);
          u8g2Fonts.setCursor(cursor, 280);
          u8g2Fonts.print(time_s); // 模拟倒计时
          // display.fillRect(56, 70, 128, 128, GxEPD_WHITE); // 刷白老番茄瓶子位置
        } while (display.nextPage());
        do
        {
          // display.drawBitmap(56, 70, bitmap_potato45, 128, 128, GxEPD_BLACK); // 刷回休息番茄
          u8g2Fonts.setFont(u8g2_font_wqy16_t_gb2312);
          u8g2Fonts.setCursor(67, 352);
          u8g2Fonts.print("休 息 时 间 到");
        } while (display.nextPage());
        // display.hibernate();
        InPotato = false;
      }
      display.hibernate();
    }
    InPotato = true;
    // 开启 声音/闪屏 提示

    // 休息时间专属while
    int time_period = PotatoMenuShow.SetPotatoPeriodWeekShort; // 获取一次设定的番茄钟
    while (InPotato)
    {
      PotatoMenuShow.PotatoISINClock = false;
      PotatoMenuShow.PotatoISINRest = true;
      vTaskDelay(1000 * 60);
      time_period--;
      // 开启一个倒计时
      if (time_period < 10 && time_period > 0) // 如果小于10分钟，则额外在显示的前面补0
      {
        time_s = "0" + (String)time_period;
      }
      else
      {
        time_s = time_period;
      }
      // 刷新屏幕时钟
      Serial.println(time_s);
      do
      {
        u8g2Fonts.setFont(u8g2_font_inb63_mn);
        u8g2Fonts.setCursor(cursor, 280);
        u8g2Fonts.print(time_s); // 模拟倒计时
      } while (display.nextPage());
      // display.hibernate();

      if (time_period == 0)
      {
        time_s = "00";
        do
        {
          u8g2Fonts.setFont(u8g2_font_inb63_mn);
          u8g2Fonts.setCursor(cursor, 280);
          u8g2Fonts.print(time_s); // 模拟倒计时
          // display.fillRect(56, 70, 128, 128, GxEPD_WHITE); // 刷白老番茄瓶子位置
        } while (display.nextPage());
        do
        {
          u8g2Fonts.setFont(u8g2_font_wqy16_t_gb2312);
          u8g2Fonts.setCursor(67, 352);
          u8g2Fonts.print("正 在 番 茄 钟");
        } while (display.nextPage());
        // display.hibernate();
        InPotato = false;
      }
      display.hibernate();
    }
    InPotato = true;
  }
  vTaskDelete(NULL);
}

// 用于向SD卡某文件位置，写入某些信息 FilePlace:文件位置 FileName:文件名称 FileContent：文件内容
void WriteSDCardMessage(String FilePlace, String Filename, String FileContent)
{
  if (!SD.begin(SD_CS, hspi))
  {
    /* code */
  }

  digitalWrite(WAKEIO, HIGH); // 使能
  if (!SD.exists(FilePlace))
  {
    SD.mkdir(FilePlace);
  }
  File file = SD.open(FilePlace + "/" + Filename, FILE_WRITE);
  file.print(FileContent);
  file.flush();
  file.close();
  digitalWrite(WAKEIO, LOW); // 关闭供电
}