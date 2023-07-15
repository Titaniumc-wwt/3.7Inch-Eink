#ifndef RX8025_h
#define RX8025_h

#define RX8025_SEC 0x0
#define RX8025_MIN 0x1
#define RX8025_HR 0x2
#define RX8025_WEEK 0x3
#define RX8025_DATE 0x4
#define RX8025_MTH 0x5
#define RX8025_YR 0x6
#define RX8025_Doffset 0x7
#define RX8025_AW_MIN 0x8
#define RX8025_AW_HR 0x9
#define RX8025_AW_WEEK 0xa
#define RX8025_AD_MIN 0xb
#define RX8025_AD_HR 0xc
#define RX8025_CTL1 0xd
#define RX8025_CTL2 0xE

#include <Arduino.h>
#include <Wire.h>
#include <time.h>
// DateTime (get everything at once) from JeeLabs / Adafruit
// Simple general-purpose date/time class (no TZ / DST / leap second handling!)
class DateTime
{
public:
    DateTime(uint32_t t = 0);
    DateTime(uint16_t year, uint8_t month, uint8_t day,
             uint8_t hour = 0, uint8_t min = 0, uint8_t sec = 0);
    DateTime(const char *date, const char *time);
    uint16_t year() const
    {
        return 2000 + yOff;
    }
    uint8_t month() const
    {
        return m;
    }
    uint8_t day() const
    {
        return d;
    }
    uint8_t hour() const
    {
        return hh;
    }
    uint8_t minute() const
    {
        return mm;
    }
    uint8_t second() const
    {
        return ss;
    }
    uint8_t dayOfTheWeek() const;

    // 32-bit times as seconds since 1/1/2000
    long secondstime() const;
    // 32-bit times as seconds since 1/1/1970
    // THE ABOVE COMMENT IS CORRECT FOR LOCAL TIME; TO USE THIS COMMAND TO
    // OBTAIN TRUE UNIX TIME SINCE EPOCH, YOU MUST CALL THIS COMMAND AFTER
    // SETTING YOUR CLOCK TO UTC
    uint32_t unixtime(void) const;

protected:
    uint8_t yOff, m, d, hh, mm, ss;
};

//判断年份是否是闰年
//Determine whether the year is a leap year
bool isleapYear(const uint8_t);

class RX8025
{
private:
    unsigned char RX8025_Control[2];
    /**
     * 获取寄存器数据
     * @return byte
     */
    byte getData(byte regaddr);

    /**
     * @brief 将十进制编码的二进制数转换为普通十进制数
     *
     * @param val
     * @return byte
     */
    byte decToBcd(byte val);

    /**
     * 将二进制编码的十进制数转换为普通十进制数
     * @param val
     * @return byte
     */
    byte bcdToDec(byte val);

    byte WeekToBdc(byte val);

    byte WeekToNum(byte val);

    
    /*子函数,用于读取数据表中农历月的大月或小月,如果该月为大返回1,为小返回0*/
    char get_moon_day(uint8_t month_p,uint32_t table_addr);

    /*
    公历年对应的农历数据,每年三字节,
    格式第一字节BIT7-4 位表示闰月月份,值为0 为无闰月,BIT3-0 对应农历第1-4 月的大小
    第二字节BIT7-0 对应农历第5-12 月大小,第三字节BIT7 表示农历第13 个月大小
    月份对应的位为1 表示本农历月大(30 天),为0 表示小(29 天)
    第三字节BIT6-5 表示春节的公历月份,BIT4-0 表示春节的公历日期
    */
    uint8_t year_code[597]={//597
                        0x04,0xAe,0x53,    //1901 0
                        0x0A,0x57,0x48,    //1902 3
                        0x55,0x26,0xBd,    //1903 6
                        0x0d,0x26,0x50,    //1904 9
                        0x0d,0x95,0x44,    //1905 12
                        0x46,0xAA,0xB9,    //1906 15
                        0x05,0x6A,0x4d,    //1907 18
                        0x09,0xAd,0x42,    //1908 21
                        0x24,0xAe,0xB6,    //1909
                        0x04,0xAe,0x4A,    //1910
                        0x6A,0x4d,0xBe,    //1911
                        0x0A,0x4d,0x52,    //1912
                        0x0d,0x25,0x46,    //1913
                        0x5d,0x52,0xBA,    //1914
                        0x0B,0x54,0x4e,    //1915
                        0x0d,0x6A,0x43,    //1916
                        0x29,0x6d,0x37,    //1917
                        0x09,0x5B,0x4B,    //1918
                        0x74,0x9B,0xC1,    //1919
                        0x04,0x97,0x54,    //1920
                        0x0A,0x4B,0x48,    //1921
                        0x5B,0x25,0xBC,    //1922
                        0x06,0xA5,0x50,    //1923
                        0x06,0xd4,0x45,    //1924
                        0x4A,0xdA,0xB8,    //1925
                        0x02,0xB6,0x4d,    //1926
                        0x09,0x57,0x42,    //1927
                        0x24,0x97,0xB7,    //1928
                        0x04,0x97,0x4A,    //1929
                        0x66,0x4B,0x3e,    //1930
                        0x0d,0x4A,0x51,    //1931
                        0x0e,0xA5,0x46,    //1932
                        0x56,0xd4,0xBA,    //1933
                        0x05,0xAd,0x4e,    //1934
                        0x02,0xB6,0x44,    //1935
                        0x39,0x37,0x38,    //1936
                        0x09,0x2e,0x4B,    //1937
                        0x7C,0x96,0xBf,    //1938
                        0x0C,0x95,0x53,    //1939
                        0x0d,0x4A,0x48,    //1940
                        0x6d,0xA5,0x3B,    //1941
                        0x0B,0x55,0x4f,    //1942
                        0x05,0x6A,0x45,    //1943
                        0x4A,0xAd,0xB9,    //1944
                        0x02,0x5d,0x4d,    //1945
                        0x09,0x2d,0x42,    //1946
                        0x2C,0x95,0xB6,    //1947
                        0x0A,0x95,0x4A,    //1948
                        0x7B,0x4A,0xBd,    //1949
                        0x06,0xCA,0x51,    //1950
                        0x0B,0x55,0x46,    //1951
                        0x55,0x5A,0xBB,    //1952
                        0x04,0xdA,0x4e,    //1953
                        0x0A,0x5B,0x43,    //1954
                        0x35,0x2B,0xB8,    //1955
                        0x05,0x2B,0x4C,    //1956
                        0x8A,0x95,0x3f,    //1957
                        0x0e,0x95,0x52,    //1958
                        0x06,0xAA,0x48,    //1959
                        0x7A,0xd5,0x3C,    //1960
                        0x0A,0xB5,0x4f,    //1961
                        0x04,0xB6,0x45,    //1962
                        0x4A,0x57,0x39,    //1963
                        0x0A,0x57,0x4d,    //1964
                        0x05,0x26,0x42,    //1965
                        0x3e,0x93,0x35,    //1966
                        0x0d,0x95,0x49,    //1967
                        0x75,0xAA,0xBe,    //1968
                        0x05,0x6A,0x51,    //1969
                        0x09,0x6d,0x46,    //1970
                        0x54,0xAe,0xBB,    //1971
                        0x04,0xAd,0x4f,    //1972
                        0x0A,0x4d,0x43,    //1973
                        0x4d,0x26,0xB7,    //1974
                        0x0d,0x25,0x4B,    //1975
                        0x8d,0x52,0xBf,    //1976
                        0x0B,0x54,0x52,    //1977
                        0x0B,0x6A,0x47,    //1978
                        0x69,0x6d,0x3C,    //1979
                        0x09,0x5B,0x50,    //1980
                        0x04,0x9B,0x45,    //1981
                        0x4A,0x4B,0xB9,    //1982
                        0x0A,0x4B,0x4d,    //1983
                        0xAB,0x25,0xC2,    //1984
                        0x06,0xA5,0x54,    //1985
                        0x06,0xd4,0x49,    //1986
                        0x6A,0xdA,0x3d,    //1987
                        0x0A,0xB6,0x51,    //1988
                        0x09,0x37,0x46,    //1989
                        0x54,0x97,0xBB,    //1990
                        0x04,0x97,0x4f,    //1991
                        0x06,0x4B,0x44,    //1992
                        0x36,0xA5,0x37,    //1993
                        0x0e,0xA5,0x4A,    //1994
                        0x86,0xB2,0xBf,    //1995
                        0x05,0xAC,0x53,    //1996
                        0x0A,0xB6,0x47,    //1997
                        0x59,0x36,0xBC,    //1998
                        0x09,0x2e,0x50,    //1999 294
                        0x0C,0x96,0x45,    //2000 297
                        0x4d,0x4A,0xB8,    //2001
                        0x0d,0x4A,0x4C,    //2002
                        0x0d,0xA5,0x41,    //2003
                        0x25,0xAA,0xB6,    //2004
                        0x05,0x6A,0x49,    //2005
                        0x7A,0xAd,0xBd,    //2006
                        0x02,0x5d,0x52,    //2007
                        0x09,0x2d,0x47,    //2008
                        0x5C,0x95,0xBA,    //2009
                        0x0A,0x95,0x4e,    //2010
                        0x0B,0x4A,0x43,    //2011
                        0x4B,0x55,0x37,    //2012
                        0x0A,0xd5,0x4A,    //2013
                        0x95,0x5A,0xBf,    //2014
                        0x04,0xBA,0x53,    //2015
                        0x0A,0x5B,0x48,    //2016
                        0x65,0x2B,0xBC,    //2017
                        0x05,0x2B,0x50,    //2018
                        0x0A,0x93,0x45,    //2019
                        0x47,0x4A,0xB9,    //2020
                        0x06,0xAA,0x4C,    //2021
                        0x0A,0xd5,0x41,    //2022
                        0x24,0xdA,0xB6,    //2023
                        0x04,0xB6,0x4A,    //2024
                        0x69,0x57,0x3d,    //2025
                        0x0A,0x4e,0x51,    //2026
                        0x0d,0x26,0x46,    //2027
                        0x5e,0x93,0x3A,    //2028
                        0x0d,0x53,0x4d,    //2029
                        0x05,0xAA,0x43,    //2030
                        0x36,0xB5,0x37,    //2031
                        0x09,0x6d,0x4B,    //2032
                        0xB4,0xAe,0xBf,    //2033
                        0x04,0xAd,0x53,    //2034
                        0x0A,0x4d,0x48,    //2035
                        0x6d,0x25,0xBC,    //2036
                        0x0d,0x25,0x4f,    //2037
                        0x0d,0x52,0x44,    //2038
                        0x5d,0xAA,0x38,    //2039
                        0x0B,0x5A,0x4C,    //2040
                        0x05,0x6d,0x41,    //2041
                        0x24,0xAd,0xB6,    //2042
                        0x04,0x9B,0x4A,    //2043
                        0x7A,0x4B,0xBe,    //2044
                        0x0A,0x4B,0x51,    //2045
                        0x0A,0xA5,0x46,    //2046
                        0x5B,0x52,0xBA,    //2047
                        0x06,0xd2,0x4e,    //2048
                        0x0A,0xdA,0x42,    //2049
                        0x35,0x5B,0x37,    //2050
                        0x09,0x37,0x4B,    //2051
                        0x84,0x97,0xC1,    //2052
                        0x04,0x97,0x53,    //2053
                        0x06,0x4B,0x48,    //2054
                        0x66,0xA5,0x3C,    //2055
                        0x0e,0xA5,0x4f,    //2056
                        0x06,0xB2,0x44,    //2057
                        0x4A,0xB6,0x38,    //2058
                        0x0A,0xAe,0x4C,    //2059
                        0x09,0x2e,0x42,    //2060
                        0x3C,0x97,0x35,    //2061
                        0x0C,0x96,0x49,    //2062
                        0x7d,0x4A,0xBd,    //2063
                        0x0d,0x4A,0x51,    //2064
                        0x0d,0xA5,0x45,    //2065
                        0x55,0xAA,0xBA,    //2066
                        0x05,0x6A,0x4e,    //2067
                        0x0A,0x6d,0x43,    //2068
                        0x45,0x2e,0xB7,    //2069
                        0x05,0x2d,0x4B,    //2070
                        0x8A,0x95,0xBf,    //2071
                        0x0A,0x95,0x53,    //2072
                        0x0B,0x4A,0x47,    //2073
                        0x6B,0x55,0x3B,    //2074
                        0x0A,0xd5,0x4f,    //2075
                        0x05,0x5A,0x45,    //2076
                        0x4A,0x5d,0x38,    //2077
                        0x0A,0x5B,0x4C,    //2078
                        0x05,0x2B,0x42,    //2079
                        0x3A,0x93,0xB6,    //2080
                        0x06,0x93,0x49,    //2081
                        0x77,0x29,0xBd,    //2082
                        0x06,0xAA,0x51,    //2083
                        0x0A,0xd5,0x46,    //2084
                        0x54,0xdA,0xBA,    //2085
                        0x04,0xB6,0x4e,    //2086
                        0x0A,0x57,0x43,    //2087
                        0x45,0x27,0x38,    //2088
                        0x0d,0x26,0x4A,    //2089
                        0x8e,0x93,0x3e,    //2090
                        0x0d,0x52,0x52,    //2091
                        0x0d,0xAA,0x47,    //2092
                        0x66,0xB5,0x3B,    //2093
                        0x05,0x6d,0x4f,    //2094
                        0x04,0xAe,0x45,    //2095
                        0x4A,0x4e,0xB9,    //2096
                        0x0A,0x4d,0x4C,    //2097
                        0x0d,0x15,0x41,    //2098
                        0x2d,0x92,0xB5,    //2099
    };
    ///月份数据表
    uint8_t day_code1[9]={0x0,0x1f,0x3b,0x5a,0x78,0x97,0xb5,0xd4,0xf3};
    uint32_t day_code2[3]={0x111,0x130,0x14e};

public:
    RX8025(); // costruttore
    /**
     * 初始化
     */
    void RX8025_init(void);
    /*
    函数功能:输入BCD阳历数据,输出BCD阴历数据(只允许1901-2099年)
    调用函数示例:Conversion(c_sun,year_sun,month_sun,day_sun)
    如:计算2004年10月16日Conversion(0,0x4,0x10,0x16);
    c_sun,year_sun,month_sun,day_sun均为BCD数据,c_sun为世纪标志位,c_sun=0为21世
    纪,c_sun=1为19世纪
    调用函数后,原有数据不变,读c_moon,year_moon,month_moon,day_moon得出阴历BCD数据
    */
    char c_moon;
    uint8_t year_moon,month_moon,day_moon,week;

    void Conversion(char c,uint8_t year,uint8_t month,uint8_t day);
    /**
     * 向时钟芯片设置时间
     * @param s  秒钟
     * @param m  分钟
     * @param h 时钟
     * @param d  天
     * @param w 星期
     * @param mh 月
     * @param y 年
     */
    void setRtcTime(uint8_t s, uint8_t m, uint8_t h, uint8_t w ,uint8_t d, uint8_t mh, uint8_t y);
    /**
     * 设置定时器
     * 单位 s
     */
    void setRtcTimer(uint8_t s);
    /**
     * 获取秒钟
     */
    byte getSecond();
    /**
     * 获取分钟数
     * @return byte
     */
    byte getMinute();
    /**
     * 获取小时数
     * @return byte
     */
    byte getHour();
    /**
     * 获取星期数
     * @return byte
     */
    byte getDoW();
    /**
     * 获取日期
     * @return byte
     */
    byte getDate();
    /**
     * 获取月份
     * @return byte
     */
    byte getMonth();
    /**
     * 获取年份
     * @return byte
     */
    byte getYear();

    /**
     * 获取时间戳
     * @return long
     */
    long getUnixtime();
};


#endif
