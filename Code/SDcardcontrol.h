// #include "FS.h"
// #include <LittleFS.h>
#include "SD.h"
#include "SPI.h"


void LittleFSinit();
int listDir(fs::FS &fs, const char * dirname, uint8_t levels, uint8_t gettarget);
void createDir(fs::FS &fs, const char * path);
void removeDir(fs::FS &fs, const char * path);
void readFile(fs::FS &fs, const char * path);
void writeFile(fs::FS &fs, const char * path, const char * message);
void appendFile(fs::FS &fs, const char * path, const char * message);
void renameFile(fs::FS &fs, const char * path1, const char * path2);
void deleteFile(fs::FS &fs, const char * path);
void testFileIO(fs::FS &fs, const char * path);
//书源文件位置，索引主文件位置，索引书页文件位置,索引书大小位置
void RECodeTXT(fs::FS &fs, const char *path, const char *bookSUOYIN); 

String GetSavedDirFile(uint8_t i);  //0:File 其他Dir
// String GetSavedPage(uint8_t i);     //0:获取大小，其他：页数
void PackFileName();    //用于手机文件夹内文件的名称

int8_t getCharLength(char zf);// 获取ascii字符的长度

void ClearSavedData();  //清空变量
// void sdcardBegin(sdspi);

