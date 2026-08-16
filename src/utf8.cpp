#include "utf8.h"

/* 不依赖全局 locale 的 UTF-8 <-> UTF-32 转换。
 * 在 Linux 上 wchar_t 为 32 位，可直接存储一个 Unicode 码点。 */

std::string wstring_to_utf8(const std::wstring& ws)
{
	std::string out;
	out.reserve(ws.size() * 3);
	for (wchar_t ch : ws)
	{
		unsigned int cp = (unsigned int)ch;
		if (cp < 0x80)
		{
			out += (char)cp;
		}
		else if (cp < 0x800)
		{
			out += (char)(0xC0 | (cp >> 6));
			out += (char)(0x80 | (cp & 0x3F));
		}
		else if (cp < 0x10000)
		{
			out += (char)(0xE0 | (cp >> 12));
			out += (char)(0x80 | ((cp >> 6) & 0x3F));
			out += (char)(0x80 | (cp & 0x3F));
		}
		else
		{
			out += (char)(0xF0 | (cp >> 18));
			out += (char)(0x80 | ((cp >> 12) & 0x3F));
			out += (char)(0x80 | ((cp >> 6) & 0x3F));
			out += (char)(0x80 | (cp & 0x3F));
		}
	}
	return out;
}

std::wstring utf8_to_wstring(const std::string& s)
{
	std::wstring out;
	out.reserve(s.size());
	size_t i = 0, n = s.size();
	while (i < n)
	{
		unsigned char c = (unsigned char)s[i];
		unsigned int cp = 0;
		int extra = 0;
		if (c < 0x80)
		{
			cp = c;
		}
		else if ((c & 0xE0) == 0xC0)
		{
			cp = c & 0x1F;
			extra = 1;
		}
		else if ((c & 0xF0) == 0xE0)
		{
			cp = c & 0x0F;
			extra = 2;
		}
		else if ((c & 0xF8) == 0xF0)
		{
			cp = c & 0x07;
			extra = 3;
		}
		else
		{
			out += L'\uFFFD';
			i++;
			continue;
		}
		if (i + extra >= n)
		{
			out += L'\uFFFD';
			break;
		}
		bool ok = true;
		for (int k = 1; k <= extra; k++)
		{
			unsigned char cc = (unsigned char)s[i + k];
			if ((cc & 0xC0) != 0x80)
			{
				ok = false;
				break;
			}
			cp = (cp << 6) | (cc & 0x3F);
		}
		if (!ok)
		{
			out += L'\uFFFD';
			i++;
			continue;
		}
		out += (wchar_t)cp;
		i += extra + 1;
	}
	return out;
}
