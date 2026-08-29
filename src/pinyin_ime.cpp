#include "pinyin_ime.h"
#include "utf8.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <new>
#include <set>
#include <string>
#include <vector>
#include <utility>

static const char * k9_map[] = {"", "", "abc", "def", "ghi", "jkl", "mno", "pqrs", "tuv", "wxyz"};

// ---- 拼音 Trie 树（前缀索引） ----

struct TrieNode
{
	TrieNode* children[26];
	bool is_word;
	std::string word;

	TrieNode() : is_word(false)
	{
		std::memset(children, 0, sizeof(children));
	}

	~TrieNode()
	{
		for (int i = 0; i < 26; i++)
			if(children[i]) delete children[i];
	}
};

/**
 * @brief 拼音输入法实例（不透明句柄）
 *
 * 每个实例管理：
 *   - 拼音 -> 单字列表  的映射表（pinyin）
 *   - 拼音串 -> 词语列表 的映射表（pinyin_to_words）
 *   - 词 -> 词频        的全局词典（dictionary）
 *   - 当前输入会话的临时状态
 */
struct pinyin_ime_t
{
	// K9 -> 拼音列表
	std::map<int, std::vector<std::string>> k9_to_pinyin;
	// 拼音串 -> 词语列表
	std::map<std::string, std::set<std::wstring>> pinyin_to_words;
	// 词 -> 累计词频
	std::map<std::wstring, uint32_t> dictionary;


	// 拼音表文件路径
	std::string pinyin_path;
	// 词典文件路径
	std::string dict_path;

	// 拼音前缀索引（Trie 树）
	TrieNode* trie_root;


	// 用户输入的原始拼音字符串（未过滤分隔符）
	std::string raw_pinyin;

	// 全拼模式下分词后的音节列表，如 ["shu", "li", "kou"]
	std::vector<std::string> segments;

	// 已确认（已选词）的音节个数，即 segments 中前 solved_yin 个音节已处理
	uint32_t solved_yin;

	// 已选中的最终汉字结果
	std::wstring final_word;

	// 当前候选词列表（拼音, 候选词）
	std::vector<std::pair<std::string, std::wstring>> candidates;

	// 当前输入是否已完成（无剩余音节待处理）
	bool finished;

	// ---- UTF-8 缓存（供 C 接口返回 const char* 使用） ----

	// 当前剩余拼音分词的 UTF-8 缓存
	std::string segments_cache;

	// 最终汉字结果的 UTF-8 缓存
	std::string result_cache;

	// 候选词列表的 UTF-8 缓存（按索引对应 candidates）
	std::vector<std::string> candidate_cache;

	// 构造函数：初始化状态变量
	pinyin_ime_t() : solved_yin(0), finished(true), trie_root(nullptr) {}
	~pinyin_ime_t() { delete trie_root; }
};

namespace
{

void trie_insert(TrieNode* root, const std::string& word)
{
	TrieNode* node = root;
	for (char c : word)
	{
		uint32_t idx = c - 'a';
		if (idx >= 26) return;
		if (!node->children[idx])
			node->children[idx] = new TrieNode();
		node = node->children[idx];
	}
	node->is_word = true;
	node->word = word;
}

TrieNode* trie_find_prefix(TrieNode* root, const std::string& prefix)
{
	TrieNode* node = root;
	for (char c : prefix)
	{
		uint32_t idx = c - 'a';
		if (idx >= 26 || !node->children[idx])
			return nullptr;
		node = node->children[idx];
	}
	return node;
}

void trie_collect_words(TrieNode* node, std::vector<std::string>& result)
{
	if (!node)
		return;
	if (node->is_word)
		result.push_back(node->word);
	for (uint32_t i = 0; i < 26; i++)
	{
		if (node->children[i])
			trie_collect_words(node->children[i], result);
	}
}

/**
 * @brief 加载拼音表文件
 *
 * @param ime  输入法实例
 * @param path 拼音表文件路径
 * @return 成功加载且至少有一条记录时返回 true，否则 false
 */
bool load_pinyin_table(pinyin_ime_t* ime, const std::string& path)
{
	std::ifstream fin(path.c_str());
	if (!fin) return false;

	if (ime->trie_root) delete ime->trie_root;
	ime->trie_root = new TrieNode();

	std::string line;
	while (std::getline(fin, line))
	{
		// 查找逗号分隔符（左边是拼音，右边是汉字串）
		int comma = line.find(',');
		if (comma == std::string::npos)
			continue;
		std::string en = line.substr(0, comma);
		trie_insert(ime->trie_root, en);

		uint32_t k9_id = 0;
		for (uint32_t i = 0; i < comma; i++)
		{
			for (uint32_t digit = 2; digit < 10; digit++)
			{
				std::string btn_txt = k9_map[digit];
				if (btn_txt.find(en[i]) != std::string::npos) 
				{
					k9_id = k9_id * 10 + digit;
					break;
				}
			}
		}
		ime->k9_to_pinyin[k9_id].push_back(en);

		std::wstring chars = utf8_to_wstring(line.substr(comma + 1));
		for (wchar_t ch : chars)
		{
			// 跳过空白字符
			if (ch == L' ' || ch == L'\t' || ch == L'\r' || ch == L'\n')
				continue;
			std::wstring ch_as_str = std::wstring(1, ch);
			ime->pinyin_to_words[en].insert(ch_as_str);
			// 初始化单字词频为 0
			ime->dictionary[ch_as_str] = 0;
		}
	}
	fin.close();

	/*
	for (auto &it : ime->k9_to_pinyin)
	{
		std::cout<<it.first<<":";
		for (auto &jt : it.second)
		{
			std::cout<<jt<<",";
		}
		std::cout<<std::endl;
	}
	*/

	return true;
}

/**
 * @brief 加载词典文件
 *
 * @param ime  输入法实例
 * @param path 词典文件路径
 * @return 始终返回 true（即使文件为空也允许）
 */
bool load_dictionary(pinyin_ime_t* ime, const std::string& path)
{
	std::ifstream fin(path.c_str());
	if (!fin) return false;

	std::string pinxie;
	uint32_t word_count;
	std::string word_utf8;
	uint32_t word_freq;
	while (fin >> pinxie)
	{
		if (!(fin >> word_count))
			break;
		for (uint32_t i = 0; i < word_count; i++)
		{
			if (!(fin >> word_utf8))
				break;
			if (!(fin >> word_freq))
				break;
			std::wstring word = utf8_to_wstring(word_utf8);

			// 使用 set 容器，自身即保证不重复
			ime->pinyin_to_words[pinxie].insert(word);
			ime->dictionary[word] = word_freq;
		}
	}
	fin.close();

	return true;
}

/**
 * @brief 将字符串向量从指定位置开始拼接
 *
 * @param v    字符串向量
 * @param from 起始索引（包含）
 * @param sep  分隔符
 * @return 拼接后的字符串，如 join({"shu","li","kou"}, 1, "'") => "li'kou"
 */
std::string join(const std::vector<std::string>& v, uint32_t from, const std::string& sep)
{
	std::string s;
	for (uint32_t i = from; i < v.size(); i++)
	{
		if (i > from)
			s += sep;
		s += v[i];
	}
	return s;
}

/**
 * @brief 候选词比较函数（用于排序）
 *
 * 排序规则（依次）：
 *   1. 字数多的优先
 *   2. 拼音长度短的优先（拼音长度短的，匹配度更高）
 *   3. 频率高的优先
 *
 * 用于 std::sort，配合 lambda 使用。
 *
 * @param ime 输入法实例（用于获取词频）
 * @param a   候选词 A
 * @param b   候选词 B
 * @return a 应排在 b 前面时返回 true
 */
bool words_compare(const pinyin_ime_t* ime, 
		std::pair<std::string, std::wstring>& a, std::pair<std::string, std::wstring>& b)
{
	auto da = ime->dictionary.find(a.second);
	auto db = ime->dictionary.find(b.second);
	uint32_t fa = (da == ime->dictionary.end()) ? 0 : da->second;
	uint32_t fb = (db == ime->dictionary.end()) ? 0 : db->second;
	if (a.second.length() != b.second.length())
	    return a.second.length() > b.second.length();
	if (a.first.length() != b.first.length())
	    return a.first.length() < b.first.length();
	return fa > fb;
}

std::vector<std::string> guess_pinyin(const pinyin_ime_t* ime, const std::string& seg_str)
{
	std::vector<std::string> result;
	TrieNode* node = trie_find_prefix(ime->trie_root, seg_str);
	if (node)
		trie_collect_words(node, result);
	return result;
}

/**
 * @brief 全拼音节分词（最长优先 + 回溯）
 *
 * 将用户输入的拼音字符串（可能包含 ' 分隔符）切分为音节序列。
 * 采用贪心最长匹配策略，失败时回溯尝试更短的音节。
 *
 * @param ime 输入法实例（用于查询拼音表）
 * @param s   待分词的拼音字符串
 * @param pos 当前处理位置
 * @param out 输出音节列表（递归调用时从后往前插入）
 * @return 分词成功返回 true
 */
bool segment(const pinyin_ime_t* ime, const std::string& s, int pos, std::vector<std::string>& out)
{
	// 已处理完所有字符，成功
	if (pos >= s.size())
		return true;
		
	// 跳过显式的 ' 分隔符
	if (s[pos] == '\'')
		return segment(ime, s, pos + 1, out);
		
	// 找到下一个显式分隔符或字符串末尾，作为当前音节的最大可能长度
	int end = s.find('\'', pos);
	if (end == std::string::npos)
		end = s.size();

	// 从最长可能音节开始尝试，逐步缩短
	for (int len = end - pos; len >= 1; len--)
	{
		std::string seg_str = s.substr(pos, len);

		bool valid = (trie_find_prefix(ime->trie_root, seg_str) != nullptr);

		//std::cout<<seg_str<<","<<valid<<","<<len<<std::endl;

		// 匹配到后进行后续分词，看是否成功
		if (valid && segment(ime, s, pos + len, out))
		{
			// 如果成功，递归返回时在开头插入当前音节（保证最终顺序正确）
			out.insert(out.begin(), seg_str);
			return true;
		}
		
		// 后续分词不成功，缩短长度继续
	}
	// 所有长度都失败，分词失败
	return false;
}

/**
 * @brief k26模式下，遍历 拼音串->词语表，寻找匹配的词语
 * @param ime 输入法实例
 */
void guess_cand_k26(pinyin_ime_t* ime)
{
	uint32_t start = ime->solved_yin;

	for (uint32_t i = start; i < ime->segments.size(); i++)
	{
		for (auto &it : ime->pinyin_to_words)
		{
			//std::cout<<std::endl;
			//std::cout<<it.first<< ":";
			
			// 首字母不对直接跳，避免后续耗时
			if (it.first[0] != ime->segments[start][0]) continue;
			
			// 把拼音转换为音节列表，方便计算
			// 正常情况下必定一次成功，耗时不会多
			// 不成功的也直接跳，说明词表这一条有问题
			std::vector<std::string> word_segs;
			if (!segment(ime, it.first, 0, word_segs)) continue;

			// 音节的数量不符合，跳
			if (word_segs.size() != i - start + 1) continue;
			
			// 按开头匹配看每单个拼音是否合法
			bool valid = true;
			for (uint32_t j = 0; j < word_segs.size(); j++)
			{
				if (word_segs[j].rfind(ime->segments[start + j], 0) != 0) 
				{
					valid = false;
					break;
				}
			}
			if (!valid) continue;

			for (auto &cand : it.second)
			{
				//std::cout<<wstring_to_utf8(cand)<< ",";
				ime->candidates.push_back(std::make_pair(it.first, cand));
			}
			
		}
		//std::cout<<std::endl;
	}
	
}

/**
 * @brief 计算当前候选词列表
 * 查找 pinyin_to_words 中所有匹配的词语，最后排序。
 * 如果已无剩余音节，标记 finished = true。
 */
void compute_candidates(pinyin_ime_t* ime)
{
	ime->candidates.clear();
	if (ime->finished || ime->solved_yin >= (int)ime->segments.size())
	{
		ime->finished = true;
		return;
	}

	guess_cand_k26(ime);

	std::sort(ime->candidates.begin(), ime->candidates.end(),
		[ime](std::pair<std::string, std::wstring>& a, std::pair<std::string, std::wstring>& b)
		 { return words_compare(ime, a, b); });
}

/**
 * @brief 更新所有 UTF-8 缓存
 *
 * 将内部宽字符串状态转换为 UTF-8 缓存，供 C 接口返回 const char* 使用。
 * 包括：
 *   - segments_cache: 剩余拼音分词
 *   - result_cache:   已选汉字结果
 *   - candidate_cache: 候选词列表
 */
void update_caches(pinyin_ime_t* ime)
{
	ime->segments_cache = join(ime->segments, ime->solved_yin, "'");

	ime->result_cache = wstring_to_utf8(ime->final_word);

	ime->candidate_cache.clear();
	ime->candidate_cache.reserve(ime->candidates.size());
	for (auto& cand : ime->candidates)
		ime->candidate_cache.push_back(wstring_to_utf8(cand.second));
}

} // namespace


extern "C"
{

/**
 * @brief 初始化拼音输入法实例
 *
 * 加载拼音表与词典文件，建立内部数据结构。
 * 加载失败时自动释放已分配资源并返回 NULL。
 *
 * @param pinyin_path     拼音表文件路径（如 "pinyin.txt"）
 * @param dictionary_path 词典文件路径（如 "dictionary.data"）
 * @return 成功返回不透明句柄，失败返回 NULL
 */
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
		return ime;
	}
	catch (...)
	{
		return nullptr;
	}
}

/**
 * @brief 销毁输入法实例并释放所有内存
 *
 * 销毁后，所有通过 getter 接口获取的字符串指针将失效。
 *
 * @param ime 输入法实例句柄
 */
void pinyin_ime_destroy(pinyin_ime_t* ime)
{
	delete ime;
}

/**
 * @brief 输入拼音字符串，触发分词与候选词生成
 *
 * 清空当前会话状态，对输入进行合法性校验，然后执行分词（全拼优先，失败则走简拼/混拼），
 * 生成候选词列表。结果通过 pinyin_ime_get_* 系列接口获取。
 *
 * 输入要求：
 *   - 仅允许小写字母 a-z 和音节分隔符 '
 *   - 不允许为空字符串
 *
 * @param ime         输入法实例句柄
 * @param pinyin_utf8 拼音字符串（UTF-8 编码）
 * @return PINYIN_IME_OK 成功，PINYIN_IME_ERR_BAD_PINYIN 格式错误
 */
int pinyin_ime_input(pinyin_ime_t* ime, const char* pinyin_utf8)
{
	if (!ime || !pinyin_utf8)
		return PINYIN_IME_ERR_INVALID_ARG;
	try
	{
		std::string raw_pinyin = pinyin_utf8;
		// 清空上次输入的会话状态
		ime->raw_pinyin = raw_pinyin;
		ime->segments.clear();
		ime->solved_yin = 0;
		ime->final_word.clear();
		ime->candidates.clear();
		ime->segments_cache.clear();
		ime->finished = true;
		
		// 空输入非法
		if (raw_pinyin.empty())
			return PINYIN_IME_ERR_BAD_PINYIN;
		// 输入合法性校验：仅允许小写字母和 ' 分隔符
		for (char c : raw_pinyin)
			if (!((c >= 'a' && c <= 'z') || c == '\''))
				return PINYIN_IME_ERR_BAD_PINYIN;

		// 全拼分词：最长优先 + 回溯；无法完整分词则走简拼/混拼
		if (segment(ime, raw_pinyin, 0, ime->segments))
		{
			// 全拼分词成功，计算候选词
			ime->finished = false;
			compute_candidates(ime);
		}
		else
		{
			return PINYIN_IME_ERR_BAD_PINYIN;
		}

		update_caches(ime);
		return PINYIN_IME_OK;
	}
	catch (...)
	{
		return PINYIN_IME_ERR_INVALID_ARG;
	}
}

/**
 * @brief 选择候选词，推进输入状态
 *
 * 用户从候选词列表中选择一个（序号从 0 开始），
 * 该词被追加到最终结果，对应音节被标记为已处理。
 *
 * 行为细节：
 *   - 简拼/混拼模式：选中后立即完成（一次选择一个整词）。
 *   - 全拼模式：选中后的音节数 = 词的字数（每个汉字对应一个音节），
 *     若还有剩余音节则重新计算候选词；否则标记完成。
 *   - 新词自学习：全拼模式下的新词组合会被自动加入词典。
 *   - 词频更新：每次选中后，对应词的词频 +1。
 *
 * @param ime   输入法实例句柄
 * @param index 候选词序号（0-based）
 * @return PINYIN_IME_OK 成功，PINYIN_IME_ERR_INDEX 序号越界
 */
int pinyin_ime_select(pinyin_ime_t* ime, uint32_t index)
{
	if (!ime)
		return PINYIN_IME_ERR_INVALID_ARG;
	try
	{
		if (index < 0 || index >= (int)ime->candidates.size())
			return PINYIN_IME_ERR_INDEX;

		std::wstring chosen = ime->candidates[index].second;
		// 词频 +1
		ime->dictionary[chosen]++;

		// 按词的字数推进音节
		ime->final_word += chosen;
		ime->solved_yin += (int)chosen.length();
		if (ime->solved_yin >= (int)ime->segments.size())
		{
			// 所有音节处理完毕
			ime->finished = true;
			ime->candidates.clear();
			// 新词自学习：如果是词典中不存在的新组合，写入词典
			std::string segments_string = join(ime->segments, 0, "'");
			if (ime->dictionary.find(ime->final_word) == ime->dictionary.end())
			{
				// 初始词频暂且设为 1
				ime->pinyin_to_words[segments_string].insert(ime->final_word);
				ime->dictionary[ime->final_word] = 1;
			}
		}
		else
		{
			// 还有剩余音节，继续计算下一批候选词
			compute_candidates(ime);
		}

		update_caches(ime);
		return PINYIN_IME_OK;
	}
	catch (...)
	{
		return PINYIN_IME_ERR_INVALID_ARG;
	}
}

/**
 * @brief 保存词库到文件
 *
 * 将当前 pinyin_to_words 和 dictionary 的内容写入文件。
 * 格式：每行 pinxie  word_freq  word1  freq1  word2  freq2  ...
 *
 * 通过此接口，用户使用过程中自学习的新词和更新的词频得以持久化。
 *
 * @param ime             输入法实例句柄
 * @param dictionary_path 目标保存路径，传 NULL 则写回初始化时的路径
 * @return PINYIN_IME_OK 成功，PINYIN_IME_ERR_IO 写入失败
 */
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
		// 遍历每个拼音串及其对应的词语列表
		for (const auto& e : ime->pinyin_to_words)
		{
			fout << e.first << "  " << e.second.size() << "  ";
			for (const std::wstring& w : e.second)
				fout << wstring_to_utf8(w) << " " << ime->dictionary[w] << "  ";
			fout << "\n";
		}
		fout.close();
		return PINYIN_IME_OK;
	}
	catch (...)
	{
		return PINYIN_IME_ERR_IO;
	}
}

/**
 * @brief 获取当前剩余拼音分词
 *
 * 全拼模式：返回未处理音节，如 "li'kou"（已选 "shu" 时）
 * 简拼/混拼模式：未完成时返回原始输入，完成后返回空串
 *
 * @param ime 输入法实例句柄
 * @return UTF-8 字符串指针（im 生命周期内有效），im 为 NULL 时返回 NULL
 */
const char* pinyin_ime_get_segments(pinyin_ime_t* ime)
{
	if (!ime)
		return nullptr;
	return ime->segments_cache.c_str();
}

/**
 * @brief 获取当前已选中的汉字结果
 *
 * @param ime 输入法实例句柄
 * @return UTF-8 字符串指针（im 生命周期内有效），im 为 NULL 时返回 NULL
 */
const char* pinyin_ime_get_result(pinyin_ime_t* ime)
{
	if (!ime)
		return nullptr;
	return ime->result_cache.c_str();
}

/**
 * @brief 获取当前候选词数量
 *
 * @param ime 输入法实例句柄
 * @return 候选词数量，im 为 NULL 时返回 0
 */
int pinyin_ime_get_candidate_count(pinyin_ime_t* ime)
{
	if (!ime)
		return 0;
	return (int)ime->candidates.size();
}

/**
 * @brief 获取指定序号的候选词
 *
 * @param ime   输入法实例句柄
 * @param index 候选词序号（0-based）
 * @return UTF-8 字符串指针（im 生命周期内有效），越界或 im 为 NULL 时返回 NULL
 */
const char* pinyin_ime_get_candidate(pinyin_ime_t* ime, uint32_t index)
{
	if (!ime || index >= (int)ime->candidate_cache.size())
		return nullptr;
	return ime->candidate_cache[index].c_str();
}

/**
 * @brief 判断当前输入是否已完成（无剩余音节待处理）
 *
 * @param ime 输入法实例句柄
 * @return 1 表示已完成，0 表示未完成；im 为 NULL 时返回 1
 */
int pinyin_ime_is_finished(pinyin_ime_t* ime)
{
	if (!ime)
		return 1;
	return ime->finished ? 1 : 0;
}

} // extern "C"