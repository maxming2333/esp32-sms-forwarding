/**
 * @file phone_utils.h
 * @brief 电话号码规范化与黑名单匹配工具（内联 helper）
 *
 * 从 sms.cpp 提取，供 call.cpp 和 sms.cpp 共同引用，避免代码重复。
 * 函数均为 inline，无需独立 .cpp 文件。
 *
 * 依赖: config/config.h, sim/sim_dispatcher.h（用于获取本机号码区号推断）
 */

#pragma once
#include <Arduino.h>
#include "config/config.h"
#include "sim/sim.h"

/**
 * @brief 从本机号码推断区号（如 "86"），用于短号码补齐比对。
 *        使用 Sim::phoneNum() 返回的缓存值（在 SIM_READY 后由 sim 模块抓取并定期更新）。
 *
 * 关键设计约束：
 *   - 不可在此调用 Sim::queryPhoneNumber() / SimDispatcher::sendCommand()。
 *     来电 +CLIP URC 路径在 SIM reader task 中执行，
 *     若再发起 AT 指令将自我阻塞 reader task → 死锁。
 *   - 每个传入号码做黑名单匹配时会调用本函数 N+1 次（N=黑名单条目数），
 *     必须保持 O(1) 且零阻塞。
 */
inline String phoneExtractAreaCode() {
    String phone = Sim::phoneNum();
    if (phone.length() == 0 || phone.equals("未知") || phone.equals("未知号码")) return "";
    if (phone.startsWith("+")) phone = phone.substring(1);
    if (phone.startsWith("00")) phone = phone.substring(2);
    if ((int)phone.length() <= 11) return "";
    return phone.substring(0, phone.length() - 11);
}

/**
 * @brief 规范化电话号码（去除 + / 00 前缀，补齐区号）。
 */
inline String phoneNormalize(const String& raw, const String& areaCode) {
    String s = raw;
    s.trim();
    if (s.startsWith("+")) s = s.substring(1);
    if (s.startsWith("00")) s = s.substring(2);
    if (areaCode.length() > 0 && (int)s.length() <= 11) {
        s = areaCode + s;
    }
    return s;
}

/**
 * @brief 检查号码是否在黑名单中。
 *
 * @param incoming  来电/来信号码（原始格式）
 * @return          true 表示命中黑名单，应拦截
 *
 * @note 区号在函数入口预先抓取一次（O(1)），避免每条黑名单条目都重复获取。
 */
inline bool phoneMatchesBlacklist(const String& incoming) {
    if (config.blacklistCount <= 0) return false;
    String areaCode = phoneExtractAreaCode();
    String normIn = phoneNormalize(incoming, areaCode);
    for (int i = 0; i < config.blacklistCount; i++) {
        if (normIn.equals(phoneNormalize(config.blacklist[i], areaCode))) {
            return true;
        }
    }
    return false;
}
