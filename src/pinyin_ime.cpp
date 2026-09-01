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

const char * const k9_map[] = {"", "", "abc", "def", "ghi", "jkl", "mno", "pqrs", "tuv", "wxyz"};

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
	std::map<std::string, std::vector<std::string>> k9_to_pinyin;
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

	bool use_k9;

	// ---- UTF-8 缓存（供 C 接口返回 const char* 使用） ----

	// 当前剩余拼音分词的 UTF-8 缓存
	std::string segments_cache;

	// 最终汉字结果的 UTF-8 缓存
	std::string result_cache;

	// 候选词列表的 UTF-8 缓存（按索引对应 candidates）
	std::vector<std::string> candidate_cache;

	// k9 精确拼音列表
	std::vector<std::string> k9_exact_cache;

	// 构造函数：初始化状态变量
	pinyin_ime_t() : solved_yin(0), finished(true), use_k9(false), trie_root(nullptr) {}
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
		ime->k9_to_pinyin[std::to_string(k9_id)].push_back(en);

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
 * @brief 将拼音字符串切分为音节序列。
 * 
 * 优先匹配最长音节，失败时回溯尝试更短的音节。
 * k9 模式下，先切分前半部分的精确拼音（如果有），数字部分看作一个音节
 * 
 * for example:
 * k26无分隔 你好 "nihao" -> [ni'hao]
 * k26有分隔 西安市 "xi'anshi" -> [xi'an'shi]
 * k9未精确选择 猫娘 "62664264" -> [62664264]
 * k9有精确选择 嵌入式 "qian'78744" -> [qian'78744]
 *
 * @param ime 输入法实例（用于查询拼音表）
 * @param s   待分词的拼音字符串
 * @param pos 当前处理位置
 * @param out 输出音节列表（递归调用时从后往前插入）
 * @return 分词成功返回 true
 */
bool segmentation(const pinyin_ime_t* ime, const std::string& s, int pos, std::vector<std::string>& out)
{
	// 已处理完所有字符，成功
	if (pos >= s.size())
		return true;
		
	// 跳过当前这一位的 ' 分隔符
	if (s[pos] == '\'')
		return segmentation(ime, s, pos + 1, out);
		
	// 找到下一个分隔符或字符串末尾，作为当前音节的最大可能长度
	int end = s.find('\'', pos);
	if (end == std::string::npos) end = s.size();
	// 一个拼音音节最长的长度只有6
	if (end > pos + 6) end = pos + 6;

	// 从最长可能音节开始尝试，逐步缩短
	for (int len = end - pos; len >= 1; len--)
	{
		std::string seg_str = s.substr(pos, len);

		// 用 Trie 判断该拼音是否合法
		bool valid = (trie_find_prefix(ime->trie_root, seg_str) != nullptr);

		//std::cout<<seg_str<<","<<valid<<","<<len<<std::endl;

		// 如果合法，进行后续分词，看是否成功
		if (valid && segmentation(ime, s, pos + len, out))
		{
			// 如果成功，递归返回时在开头插入当前音节（保证最终顺序正确）
			out.insert(out.begin(), seg_str);
			return true;
		}
		// 如果不合法，检查后面是否是九键数字
		if (!valid && ime->use_k9 && (seg_str[0] >= '2' && seg_str[0] <= '9'))
		{
			// 如果是，直接截取到字符串结尾然后返回
			// 因为九键选择精确拼音是从前往后选，只能前半段是字母，后半段一定是数字
			out.insert(out.begin(), s.substr(pos));
			return true;
		}
		
		// 后续分词不成功，缩短长度继续
	}
	// 所有长度都失败，分词失败
	return false;
}

/**
 * @brief 判断候选词能不能匹配k9数字串
 * 这玩意不加注释我过两天就看不懂咯
 * 
 * @param ime 输入法实例
 * @param _k9_idx 当前k9_str处理到哪一位
 * @param k9_str k9数字串
 * @param _seg_idx 当前候选词处理到哪个音节
 * @param word_segs 候选词的音节列表
 */
bool match_cand_k9(pinyin_ime_t* ime, uint32_t _k9_idx, std::string k9_str, uint32_t _seg_idx, std::vector<std::string> word_segs)
{
	std::string& segment_curr = word_segs[_seg_idx];
	uint32_t k9_idx = _k9_idx;

	// 遍历该音节中的每一位，判断当前音节能否完美匹配
	for (uint32_t i = 0; i < segment_curr.length(); i++)
	{
		// 获取该位数字值
		uint32_t k9_num_curr = k9_str[k9_idx] - '0';

		// 当前音节中这一位可以与数字匹配
		if (strchr(k9_map[k9_num_curr], segment_curr[i]) != NULL) {
			// 索引++，下一次循环时判断下一位
			k9_idx++;
			// 如果k9数字串消耗完
			if (k9_idx == k9_str.length()) {
				// 如果是最后一个音节的最后一个字符，那么匹配成功，否则失败
				if ((_seg_idx == word_segs.size() - 1) && (i == segment_curr.length() - 1)) return true;
				else return false;
			}
		}
		// 当前音节中这一位不能与数字匹配，匹配失败
		else {
			return false;
		}
	}

	// 如果是最后一个音节，那么匹配成功
	// 即使后续还有k9数字串，也是选词后的事了
	if (_seg_idx == word_segs.size() - 1) return true;
	// 如果不是最后一个音节，那就去匹配下一个音节吧
	// 上面的循环里已经考虑过k9数字串提前消耗完的情况了
	return match_cand_k9(ime, k9_idx, k9_str, _seg_idx + 1, word_segs);
	
}

/**
 * @brief 遍历 拼音串->词语表，寻找匹配的词语
 * @param ime 输入法实例
 */
void guess_cand(pinyin_ime_t* ime)
{
	uint32_t start_seg = ime->solved_yin;
	uint32_t seg_cnt = ime->segments.size() - start_seg;

	for (auto &it : ime->pinyin_to_words)
	{
		// 如果不满足：9键模式且剩余只有一个数字音节，那么首字母不对直接跳，避免后续耗时
		if (!(ime->use_k9 && seg_cnt == 1) && (it.first[0] != ime->segments[start_seg][0])) continue;

		// 把拼音转换为音节列表，方便计算
		// 正常情况下必定一次成功，耗时不会多
		// 不成功的也直接跳，说明词表这一条有问题
		std::vector<std::string> word_segs;
		if (!segmentation(ime, it.first, 0, word_segs)) continue;

		// 26键，只要输入音节数小于该词音节数，直接跳
		// 9键，到最后的数字音节时，考虑到数字串里不一定有多少音节，需要匹配大于等于音节数的词
		if ((!ime->use_k9) && (seg_cnt < word_segs.size())) continue;

		// 按开头匹配看每单个拼音是否合法
		bool valid = true;
		for (uint32_t j = 0; j < word_segs.size(); j++)
		{
			// 9键需要特殊处理最后的数字音节
			if ((ime->use_k9) && (start_seg + j == ime->segments.size() - 1))
			{
				//std::cout<<ime->segments[start_seg + j]<<std::endl;
				valid = match_cand_k9(ime, 0, ime->segments[start_seg + j], j, word_segs);
				break;
			}
			// 26键，判断是否以当前音节为开头，这样就可以支持模糊拼音
			if (word_segs[j].rfind(ime->segments[start_seg + j], 0) != 0)
			{
				valid = false;
				break;
			}
		}
		if (!valid) continue;

		for (auto &cand : it.second)
		{
			ime->candidates.push_back(std::make_pair(it.first, cand));
		}
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

	guess_cand(ime);

	std::sort(ime->candidates.begin(), ime->candidates.end(),
		[ime](std::pair<std::string, std::wstring>& a, std::pair<std::string, std::wstring>& b)
		 { return words_compare(ime, a, b); });
}

/**
 * @brief 计算 k9 精确拼音候选
 */
void compute_k9_exact(pinyin_ime_t* ime)
{
	ime->k9_exact_cache.clear();
	if (!ime->use_k9) return;
	std::string k9_str = ime->segments.back();
	for (auto &it : ime->k9_to_pinyin)
	{
		if (k9_str.rfind(it.first, 0) == 0) {
			for (auto &pinyin : it.second)
			{
				ime->k9_exact_cache.push_back(pinyin);
			}
		}
	}

	std::sort(ime->k9_exact_cache.begin(), ime->k9_exact_cache.end(),
		[ime](std::string& a, std::string& b)
		 { return (a.length() != b.length()) ? (a.length() > b.length()) : (a > b); });
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

	compute_k9_exact(ime);

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
		ime->use_k9 = false;
		
		// 空输入非法（但可以用于清空数据）
		if (raw_pinyin.empty())
			return PINYIN_IME_ERR_BAD_PINYIN;
		// 允许小写字母和 ' 分隔符，如果有2~9数字则启用 k9，否则报错
		for (char& c : raw_pinyin) {
			if ((c >= 'a' && c <= 'z') || c == '\'') continue;
			else if (c >= '2' && c <= '9') ime->use_k9 = true;
			else return PINYIN_IME_ERR_BAD_PINYIN;
		}
		
		if (segmentation(ime, raw_pinyin, 0, ime->segments)) {
			// 分词成功，计算候选词
			ime->finished = false;
			compute_candidates(ime);
		}
		else {
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
		if (index < 0 || index >= ime->candidates.size())
			return PINYIN_IME_ERR_INDEX;

		std::string& chosen_yin = ime->candidates[index].first;
		std::wstring& chosen = ime->candidates[index].second;
		// 词频 +1
		ime->dictionary[chosen]++;

		// 按词的字数推进音节
		ime->final_word += chosen;
		if (ime->use_k9) {
			// k9 要考虑的就很多了
			//std::cout<<ime->solved_yin + chosen.length()<<','<<ime->segments.size()<<std::endl;
			if (ime->solved_yin + chosen.length() >= ime->segments.size()) {
				std::string k9_numstr = ime->segments.back();  // 只可拷贝，不可引用

				std::vector<std::string> chosen_segs;
				segmentation(ime, chosen_yin, 0, chosen_segs);

				// 参与此次选词的精确拼音的数量：总音节数 - 已选的音节数 - 最后的1个数字音节
				uint32_t exact_seg_count = ime->segments.size() - ime->solved_yin - 1;
				// 从已选词的音节列表里删掉最前面的精确拼音，后面就是 k9 数字串代换的部分
				if (exact_seg_count != 0)
					chosen_segs.erase(chosen_segs.begin(), chosen_segs.begin() + exact_seg_count);

				// 将后半部分转换成无分隔的字符串，其长度就是 k9 数字串里要去掉的数字数量
				std::string k9_replaced_str = join(chosen_segs, 0, "");
				uint32_t k9_consumed = k9_replaced_str.length();
				
				// 删除最后一项，即整个 k9 数字串
				ime->segments.pop_back();

				// 插入代换部分
				ime->segments.insert(ime->segments.end(), chosen_segs.begin(), chosen_segs.end());
				// 如果 k9 有剩余，再插入剩余的 k9 数字串
				if (k9_consumed < k9_numstr.length())
					ime->segments.push_back(k9_numstr.substr(k9_consumed));
				
			}
		}
		//for (auto &it : ime->segments) std::cout<<it<<',';
		
		ime->solved_yin += chosen.length();
		//std::cout<<ime->solved_yin<<std::endl;

		if (ime->solved_yin >= ime->segments.size())
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
 * @brief 获取当前 k9 对应的精确拼音数量
 */
int pinyin_ime_get_k9_exact_count(pinyin_ime_t* ime)
{
	if (!ime)
		return PINYIN_IME_ERR_INVALID_ARG;
	return ime->k9_exact_cache.size();
}

/**
 * @brief 获取第 index 个 k9 精确拼音，越界返回 NULL
 */
const char* pinyin_ime_get_k9_exact(pinyin_ime_t* ime, uint32_t index)
{
	if (!ime || index >= ime->k9_exact_cache.size())
		return NULL;
	return ime->k9_exact_cache[index].c_str();
}

/**
 * @brief 选择第 index 个 k9 精确拼音
 */
int pinyin_ime_select_k9_exact(pinyin_ime_t* ime, uint32_t index)
{
	if (!ime || index >= ime->k9_exact_cache.size())
		return PINYIN_IME_ERR_INVALID_ARG;

	std::string& chosen_yin = ime->k9_exact_cache[index];
	uint32_t k9_consumed = chosen_yin.length();

	std::string k9_numstr = ime->segments.back(); // 只可拷贝，不可引用

	// 删除最后一项，即整个 k9 数字串
	ime->segments.pop_back();
	// 重新添加被选择的拼音和截取的数字串
	ime->segments.push_back(chosen_yin);
	if (k9_consumed < k9_numstr.length()) ime->segments.push_back(k9_numstr.substr(k9_consumed));
	else ime->use_k9 = false;

	compute_candidates(ime);
	update_caches(ime);

	return PINYIN_IME_OK;
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