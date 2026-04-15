#include "time_module.h"
#include "logger.h"
#include <time.h>
#include <sys/time.h>

// Serial1 由 main.cpp 已配置，此处直接使用

void timeModuleInit() {
  // 默认设置 UTC+8，待 SIM 或 NTP 同步后覆盖
  setenv("TZ", "UTC-8", 1);
  tzset();
  LOG("Time", "时间模块已初始化（默认时区 UTC+8）");
}

void timeModuleSyncFromSIM() {
  // 清空串口缓冲区
  while (Serial1.available()) Serial1.read();

  Serial1.println("AT+CCLK?");
  unsigned long start = millis();
  String resp;
  while (millis() - start < 3000) {
    while (Serial1.available()) {
      char c = Serial1.read();
      resp += c;
    }
    if (resp.indexOf("+CCLK:") >= 0 && resp.indexOf("OK") >= 0) break;
    if (resp.indexOf("ERROR") >= 0) break;
  }

  // 解析 +CCLK: "YY/MM/DD,HH:MM:SS±TZ"
  int cclkIdx = resp.indexOf("+CCLK:");
  if (cclkIdx < 0) {
    LOG("Time", "AT+CCLK? 无响应，跳过 SIM 时间同步");
    return;
  }

  // 提取引号内字符串
  int q1 = resp.indexOf('"', cclkIdx);
  int q2 = resp.indexOf('"', q1 + 1);
  if (q1 < 0 || q2 <= q1) {
    LOG("Time", "CCLK 格式解析失败");
    return;
  }
  String ts = resp.substring(q1 + 1, q2);  // 例: "26/04/15,10:30:00+32"

  int yy, mo, dd, hh, mi, ss, tz = 0;
  // 格式: YY/MM/DD,HH:MM:SS[+/-]TZ
  int parsed = sscanf(ts.c_str(), "%d/%d/%d,%d:%d:%d%d", &yy, &mo, &dd, &hh, &mi, &ss, &tz);
  if (parsed < 6) {
    LOG("Time", "CCLK 时间字段解析失败，原始: %s", ts.c_str());
    return;
  }

  // 构建 struct tm（本地时间，基于 SIM 时区）
  struct tm t;
  memset(&t, 0, sizeof(t));
  t.tm_year = yy + 100;  // 从 1900 起
  t.tm_mon  = mo - 1;
  t.tm_mday = dd;
  t.tm_hour = hh;
  t.tm_min  = mi;
  t.tm_sec  = ss;
  t.tm_isdst = 0;

  // mktime 视为 UTC（因为我们手动处理时区）
  time_t localTime = mktime(&t);
  // tz 单位为 15 分钟（1/4 小时），有符号
  time_t utcTime = localTime - (time_t)(tz * 15 * 60);

  struct timeval tv = { utcTime, 0 };
  settimeofday(&tv, nullptr);

  // 按 SIM 返回的时区设置 TZ（偏移小时 = tz/4）
  int tzHours = tz / 4;
  // setenv TZ 格式：UTC-8 表示东八区（POSIX 符号相反）
  char tzStr[16];
  snprintf(tzStr, sizeof(tzStr), "UTC%+d", -tzHours);
  setenv("TZ", tzStr, 1);
  tzset();

  LOG("Time", "SIM时间同步成功: %s, 时区偏移: %d (UTC%+d)", ts.c_str(), tz, tzHours);
}

void timeModuleSyncNTP() {
  // 使用 UTC+8（多个国内 NTP 服务），固定 UTC+8
  configTime(8 * 3600, 0, "pool.ntp.org", "ntp.ntsc.ac.cn", "ntp.aliyun.com");
  // 覆盖为 UTC+8
  setenv("TZ", "UTC-8", 1);
  tzset();

  // 等待时间同步（最多 10 秒）
  int retries = 0;
  while (time(nullptr) < 1000000 && retries < 100) {
    delay(100);
    retries++;
  }
  if (time(nullptr) >= 1000000) {
    LOG("Time", "NTP时间同步成功，UTC+8: %ld", (long)time(nullptr));
  } else {
    LOG("Time", "NTP时间同步超时");
  }
}

String timeModuleGetDateStr() {
  time_t now = time(nullptr);
  if (now < 1000000) {
    return "未知";
  }
  struct tm t;
  localtime_r(&now, &t);
  char buf[24];
  snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
           t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
           t.tm_hour, t.tm_min, t.tm_sec);
  return String(buf);
}
