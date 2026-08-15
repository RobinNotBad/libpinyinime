#include <fstream>
#include <map>
#include <vector>
#include <iostream>
#include <locale>
#include <wctype.h>
#include <wchar.h>
#include <algorithm>
#include <cstdlib>
using namespace std;

extern map<string, vector<wchar_t>> pinyin;
extern map<wstring, long long> dictionary;
extern map<string, vector<wstring>> pinyin_to_words;

wstring string2wstring(string str)
{
	wstring result;
	setlocale(LC_ALL, "zh_CN.UTF-8");
	size_t len = mbstowcs(NULL, str.c_str(), 0);
	if (len == (size_t)-1)
		return result;
	result.resize(len);
	mbstowcs(&result[0], str.c_str(), len);
	return result;
}

void word2pinyin(wstring word, int cur_position, string pinyin_string)
{
	wchar_t c = word[cur_position];
	for (auto& i : pinyin)
		if (find(i.second.begin(), i.second.end(), c) == i.second.end())
			continue;
		else
		{
			if (cur_position == word.length() - 1)
				pinyin_to_words[pinyin_string + i.first].push_back(word);
			else
				word2pinyin(word, cur_position + 1, pinyin_string + i.first + "'");
		}
}

void txt2data()
{
	cout << "初始化语料中\n";
	map <wchar_t, string> hanzi_to_pinyin;
	for (auto& i : pinyin)
		for (auto& j : i.second)
			hanzi_to_pinyin[j] = i.first;
	setlocale(LC_ALL, "zh_CN.UTF-8");
	wifstream fin("data.txt", ios::in);
	fin.imbue(locale("zh_CN.UTF-8"));
	wstring word;
	while (fin >> word)
	{
		bool hanzi = 1;
		for (int i = 0; i < word.length() and hanzi; i++)
			if (iswupper(word[i]) or iswlower(word[i]) or iswdigit(word[i]) or iswpunct(word[i]) or iswspace(word[i]) or hanzi_to_pinyin.find(word[i]) == hanzi_to_pinyin.end())
				hanzi = 0;
		if (hanzi)
		{
			if (dictionary.find(word) == dictionary.end())
				dictionary[word] = 0, word2pinyin(word, 0, "");
			else
				dictionary[word]++;
		}
	}
	fin.close();
	fin.clear();
	wofstream fout("dictionary.data", ios::out);
	fout.imbue(locale("zh_CN.UTF-8"));
	for (auto& i : pinyin_to_words)
	{
		fout << string2wstring(i.first) << L"  ";
		fout << i.second.size() << L"  ";
		for (auto& j : i.second)
		{
			fout << j << L" ";
			fout.imbue(locale("C"));
			fout << dictionary[j] << L"  ";
			fout.imbue(locale("zh_CN.UTF-8"));
		}
		fout << endl;
	}
	fout.close();
	fout.clear();
}