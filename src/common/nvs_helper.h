#pragma once
#include <Arduino.h>
#include <Preferences.h>

// 轻量 NVS Preferences 封装：自动 begin/end，单次读写。
// 适合一次性读/写；同一命名空间多次操作请改用 NvsScope，避免重复 begin/end 开销。
// 全部方法静态、线程安全（Preferences 本身串行化）。
class Nvs {
public:
  static String getStr(const char* ns, const char* key, const String& def = "") {
    Preferences p;
    if (!p.begin(ns, true)) {
      return def;
    }
    String v = p.isKey(key) ? p.getString(key, def) : def;
    p.end();
    return v;
  }
  static int32_t getInt(const char* ns, const char* key, int32_t def = 0) {
    Preferences p;
    if (!p.begin(ns, true)) {
      return def;
    }
    int32_t v = p.isKey(key) ? p.getInt(key, def) : def;
    p.end();
    return v;
  }
  static long getLong(const char* ns, const char* key, long def = 0) {
    Preferences p;
    if (!p.begin(ns, true)) {
      return def;
    }
    long v = p.isKey(key) ? p.getLong(key, def) : def;
    p.end();
    return v;
  }
  static bool getBool(const char* ns, const char* key, bool def = false) {
    Preferences p;
    if (!p.begin(ns, true)) {
      return def;
    }
    bool v = p.isKey(key) ? p.getBool(key, def) : def;
    p.end();
    return v;
  }
  static uint32_t getUInt(const char* ns, const char* key, uint32_t def = 0) {
    Preferences p;
    if (!p.begin(ns, true)) {
      return def;
    }
    uint32_t v = p.isKey(key) ? p.getUInt(key, def) : def;
    p.end();
    return v;
  }

  static void putStr(const char* ns, const char* key, const String& val) {
    Preferences p;
    if (!p.begin(ns, false)) {
      return;
    }
    p.putString(key, val);
    p.end();
  }
  static void putInt(const char* ns, const char* key, int32_t val) {
    Preferences p;
    if (!p.begin(ns, false)) {
      return;
    }
    p.putInt(key, val);
    p.end();
  }
  static void putLong(const char* ns, const char* key, long val) {
    Preferences p;
    if (!p.begin(ns, false)) {
      return;
    }
    p.putLong(key, val);
    p.end();
  }
  static void putBool(const char* ns, const char* key, bool val) {
    Preferences p;
    if (!p.begin(ns, false)) {
      return;
    }
    p.putBool(key, val);
    p.end();
  }
  static void putUInt(const char* ns, const char* key, uint32_t val) {
    Preferences p;
    if (!p.begin(ns, false)) {
      return;
    }
    p.putUInt(key, val);
    p.end();
  }
  static void clearNamespace(const char* ns) {
    Preferences p;
    if (!p.begin(ns, false)) {
      return;
    }
    p.clear();
    p.end();
  }
};

// 单一命名空间内批量读写的 RAII 作用域，避免重复 begin/end 开销。
// 用法：
//   { NvsScope s("sms_config"); if (s.ok()) { s->putString("k1", v1); s->putInt("k2", v2); } }
class NvsScope {
public:
  NvsScope(const char* ns, bool readOnly = false) : ok_(prefs_.begin(ns, readOnly)) {}
  ~NvsScope() { if (ok_) { prefs_.end(); } }
  bool ok() const { return ok_; }
  Preferences& p() { return prefs_; }
  Preferences* operator->() { return &prefs_; }
private:
  Preferences prefs_;
  bool ok_;
};
