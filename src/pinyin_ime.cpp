#include "pinyin_ime.h"
#include "utf8.h"

#include <algorithm>
#include <fstream>
#include <map>
#include <new>
#include <set>
#include <string>
#include <vector>

struct pinyin_ime_t
{
	std::map<std::string, std::vector<wchar_t>> pinyin;
	std::map<std::string, std::vector<std::wstring>> pinyin_to_words;
	std::map<std::wstring, long long> dictionary;

	std::string pinyin_path;
	std::string dict_path;

	// 会话状态
	std::string raw_pinyin;
	std::vector<std::string> yinjie;
	int solved_yin;
	std::wstring final_word;
	std::vector<std::wstring> candidates;
	bool jianpin_mode;
	bool finished;

	// UTF-8 结果缓存（访问器返回其 c_str()）
	std::string segments_cache;
	std::string result_cache;
	std::vector<std::string> candidate_cache;

	pinyin_ime_t() : solved_yin(0), jianpin_mode(false), finished(false) {}
};

namespace
{

bool load_pinyin_table(pinyin_ime_t* ime, const std::string& path)
{
	std::ifstream fin(path.c_str());
	if (!fin)
		return false;
	std::string line;
	while (std::getline(fin, line))
	{
		if (!line.empty() && line.back() == '\r')
			line.pop_back();
		size_t comma = line.find(',');
		if (comma == std::string::npos)
			continue;
		std::string en = line.substr(0, comma);
		std::wstring chars = utf8_to_wstring(line.substr(comma + 1));
		for (wchar_t ch : chars)
		{
			if (ch == L' ' || ch == L'\t' || ch == L'\r' || ch == L'\n')
				continue;
			ime->pinyin[en].push_back(ch);
			ime->dictionary[std::wstring(1, ch)] = 0;
		}
	}
	return !ime->pinyin.empty();
}

bool load_dictionary(pinyin_ime_t* ime, const std::string& path)
{
	std::ifstream fin(path.c_str());
	if (!fin)
		return false;
	std::string pinxie;
	long long words_number;
	std::string word_utf8;
	long long word_count;
	while (fin >> pinxie)
	{
		if (!(fin >> words_number))
			break;
		for (long long i = 0; i < words_number; i++)
		{
			if (!(fin >> word_utf8))
				break;
			if (!(fin >> word_count))
				break;
			std::wstring word = utf8_to_wstring(word_utf8);
			ime->pinyin_to_words[pinxie].push_back(word);
			ime->dictionary[word] = word_count;
		}
	}
	return true;
}

void ensure_single_chars(pinyin_ime_t* ime)
{
	for (const auto& e : ime->pinyin)
	{
		std::vector<std::wstring>& words = ime->pinyin_to_words[e.first];
		std::set<std::wstring> existing(words.begin(), words.end());
		for (wchar_t ch : e.second)
		{
			std::wstring w(1, ch);
			if (existing.find(w) == existing.end())
				words.push_back(w);
		}
	}
}

std::string join(const std::vector<std::string>& v, size_t from, const std::string& sep)
{
	std::string s;
	for (size_t i = from; i < v.size(); i++)
	{
		if (i > from)
			s += sep;
		s += v[i];
	}
	return s;
}

bool words_compare(const pinyin_ime_t* ime, const std::wstring& a, const std::wstring& b)
{
	auto da = ime->dictionary.find(a);
	auto db = ime->dictionary.find(b);
	long long fa = (da == ime->dictionary.end()) ? 0 : da->second;
	long long fb = (db == ime->dictionary.end()) ? 0 : db->second;
	if (fa != fb)
		return fa > fb;
	return a.length() > b.length();
}

bool mixed_match_rec(const std::string& in, size_t ip, const std::string& key, size_t kp)
{
	if (kp >= key.size())
		return ip == in.size();
	size_t end = key.find('\'', kp);
	if (end == std::string::npos)
		end = key.size();
	size_t syl_len = end - kp;
	// 完整音节
	if (syl_len <= in.size() - ip &&
	    in.compare(ip, syl_len, key, kp, syl_len) == 0 &&
	    mixed_match_rec(in, ip + syl_len, key, end + 1))
		return true;
	// 首字母简拼
	if (ip < in.size() && in[ip] == key[kp] &&
	    mixed_match_rec(in, ip + 1, key, end + 1))
		return true;
	return false;
}

bool mixed_match(const std::string& input, const std::string& key)
{
	return mixed_match_rec(input, 0, key, 0);
}

bool segment(const pinyin_ime_t* ime, const std::string& s, size_t pos, std::vector<std::string>& out)
{
	if (pos >= s.size())
		return true;
	if (s[pos] == '\'')
		return segment(ime, s, pos + 1, out);
	size_t end = s.find('\'', pos);
	if (end == std::string::npos)
		end = s.size();
	for (size_t len = end - pos; len >= 1; len--)
	{
		if (ime->pinyin.count(s.substr(pos, len)) && segment(ime, s, pos + len, out))
		{
			out.insert(out.begin(), s.substr(pos, len));
			return true;
		}
	}
	return false;
}

void compute_candidates(pinyin_ime_t* ime)
{
	ime->candidates.clear();
	if (ime->finished || ime->solved_yin >= (int)ime->yinjie.size())
	{
		ime->finished = true;
		return;
	}
	size_t s = (size_t)ime->solved_yin;
	std::string cur;
	for (size_t i = 0; s + i < ime->yinjie.size(); i++)
	{
		if (i == 0)
			cur = ime->yinjie[s];
		else
			cur += "'" + ime->yinjie[s + i];
		auto it = ime->pinyin_to_words.find(cur);
		if (it != ime->pinyin_to_words.end())
			for (const std::wstring& w : it->second)
				ime->candidates.push_back(w);
	}

	std::sort(ime->candidates.begin(), ime->candidates.end(),
		[ime](const std::wstring& a, const std::wstring& b) { return words_compare(ime, a, b); });
}

void update_caches(pinyin_ime_t* ime)
{
	if (ime->jianpin_mode)
		ime->segments_cache = ime->finished ? std::string() : ime->raw_pinyin;
	else
		ime->segments_cache = join(ime->yinjie, (size_t)ime->solved_yin, "'");

	ime->result_cache = wstring_to_utf8(ime->final_word);

	ime->candidate_cache.clear();
	ime->candidate_cache.reserve(ime->candidates.size());
	for (const std::wstring& w : ime->candidates)
		ime->candidate_cache.push_back(wstring_to_utf8(w));
}

} // namespace

extern "C"
{

pinyin_ime_t* pinyin_ime_init(const char* pinyin_path, const char* dictionary_path)
{
	if (!pinyin_path || !dictionary_path)
		return nullptr;
	try
	{
		pinyin_ime_t* ime = new (std::nothrow) pinyin_ime_t();
		if (!ime)
			return nullptr;
		ime->pinyin_path = pinyin_path;
		ime->dict_path = dictionary_path;
		if (!load_pinyin_table(ime, pinyin_path))
		{
			delete ime;
			return nullptr;
		}
		if (!load_dictionary(ime, dictionary_path))
		{
			delete ime;
			return nullptr;
		}
		ensure_single_chars(ime);
		return ime;
	}
	catch (...)
	{
		return nullptr;
	}
}

void pinyin_ime_destroy(pinyin_ime_t* ime)
{
	delete ime;
}

int pinyin_ime_input(pinyin_ime_t* ime, const char* pinyin_utf8)
{
	if (!ime || !pinyin_utf8)
		return PINYIN_IME_ERR_INVALID_ARG;
	try
	{
		std::string pinxie = pinyin_utf8;
		// 清空缓存
		ime->raw_pinyin = pinxie;
		ime->yinjie.clear();
		ime->solved_yin = 0;
		ime->final_word.clear();
		ime->candidates.clear();
		ime->segments_cache.clear();
		ime->jianpin_mode = false;
		ime->finished = false;
		
		if (pinxie.empty())
			return PINYIN_IME_ERR_BAD_PINYIN;
		for (char c : pinxie)
			if (!((c >= 'a' && c <= 'z') || c == '\''))
				return PINYIN_IME_ERR_BAD_PINYIN;

		// 全拼分词：最长优先 + 回溯；无法完整分词则走简拼/混拼
		if (segment(ime, pinxie, 0, ime->yinjie))
		{
			compute_candidates(ime);
		}
		else
		{
			// 简拼 / 半简拼半全拼：按“每个音节可用完整拼音或首字母”匹配整词
			ime->jianpin_mode = true;
			std::string clean = pinxie;
			clean.erase(std::remove(clean.begin(), clean.end(), '\''), clean.end());

			std::vector<std::wstring> words;
			std::set<std::wstring> seen;
			for (const auto& e : ime->pinyin_to_words)
			{
				if (!mixed_match(clean, e.first))
					continue;
				for (const std::wstring& w : e.second)
					if (seen.insert(w).second)
						words.push_back(w);
			}
			std::sort(words.begin(), words.end(),
				[ime](const std::wstring& a, const std::wstring& b) { return words_compare(ime, a, b); });
			ime->candidates = words;
			ime->finished = words.empty();
		}

		update_caches(ime);
		return PINYIN_IME_OK;
	}
	catch (...)
	{
		return PINYIN_IME_ERR_INVALID_ARG;
	}
}

int pinyin_ime_select(pinyin_ime_t* ime, int index)
{
	if (!ime)
		return PINYIN_IME_ERR_INVALID_ARG;
	try
	{
		if (index < 0 || index >= (int)ime->candidates.size())
			return PINYIN_IME_ERR_INDEX;

		std::wstring chosen = ime->candidates[index];
		ime->dictionary[chosen]++;

		if (ime->jianpin_mode)
		{
			ime->final_word += chosen;
			ime->finished = true;
			ime->candidates.clear();
		}
		else
		{
			ime->final_word += chosen;
			ime->solved_yin += (int)chosen.length();
			if (ime->solved_yin >= (int)ime->yinjie.size())
			{
				ime->finished = true;
				ime->candidates.clear();
				std::string yinjie_string = join(ime->yinjie, 0, "'");
				if (ime->dictionary.find(ime->final_word) == ime->dictionary.end())
				{
					ime->pinyin_to_words[yinjie_string].push_back(ime->final_word);
					ime->dictionary[ime->final_word] = 393939 + 1;
				}
			}
			else
			{
				compute_candidates(ime);
			}
		}

		update_caches(ime);
		return PINYIN_IME_OK;
	}
	catch (...)
	{
		return PINYIN_IME_ERR_INVALID_ARG;
	}
}

int pinyin_ime_save(pinyin_ime_t* ime, const char* dictionary_path)
{
	if (!ime)
		return PINYIN_IME_ERR_INVALID_ARG;
	try
	{
		std::string path = dictionary_path ? dictionary_path : ime->dict_path;
		if (path.empty())
			return PINYIN_IME_ERR_IO;
		std::ofstream fout(path.c_str(), std::ios::out | std::ios::trunc);
		if (!fout)
			return PINYIN_IME_ERR_IO;
		for (const auto& e : ime->pinyin_to_words)
		{
			fout << e.first << "  " << e.second.size() << "  ";
			for (const std::wstring& w : e.second)
				fout << wstring_to_utf8(w) << " " << ime->dictionary[w] << "  ";
			fout << "\n";
		}
		return fout.good() ? PINYIN_IME_OK : PINYIN_IME_ERR_IO;
	}
	catch (...)
	{
		return PINYIN_IME_ERR_IO;
	}
}

const char* pinyin_ime_get_segments(pinyin_ime_t* ime)
{
	if (!ime)
		return nullptr;
	return ime->segments_cache.c_str();
}

const char* pinyin_ime_get_result(pinyin_ime_t* ime)
{
	if (!ime)
		return nullptr;
	return ime->result_cache.c_str();
}

int pinyin_ime_get_candidate_count(pinyin_ime_t* ime)
{
	if (!ime)
		return 0;
	return (int)ime->candidates.size();
}

const char* pinyin_ime_get_candidate(pinyin_ime_t* ime, int index)
{
	if (!ime || index < 0 || index >= (int)ime->candidate_cache.size())
		return nullptr;
	return ime->candidate_cache[index].c_str();
}

int pinyin_ime_is_finished(pinyin_ime_t* ime)
{
	if (!ime)
		return 1;
	return ime->finished ? 1 : 0;
}

} // extern "C"
