#ifndef PINYIN_IME_UTF8_H
#define PINYIN_IME_UTF8_H

#include <string>

/* 宽字符串（UTF-32）转 UTF-8 字符串 */
std::string wstring_to_utf8(const std::wstring& ws);

/* UTF-8 字符串转宽字符串（UTF-32） */
std::wstring utf8_to_wstring(const std::string& s);

#endif /* PINYIN_IME_UTF8_H */
