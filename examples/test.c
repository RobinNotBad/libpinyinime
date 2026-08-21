#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pinyin_ime.h"

static void show(pinyin_ime_t* ime, const char* what)
{
	printf("[%s] 剩余分词: '%s'  结果: '%s'  候选(%d):",
	       what,
	       pinyin_ime_get_segments(ime) ? pinyin_ime_get_segments(ime) : "",
	       pinyin_ime_get_result(ime) ? pinyin_ime_get_result(ime) : "",
	       pinyin_ime_get_candidate_count(ime));
	int n = pinyin_ime_get_candidate_count(ime);
	for (int i = 0; i < n; i++)
		printf(" %d.%s", i, pinyin_ime_get_candidate(ime, i));
	printf("\n");
}

int main(int argc, char** argv)
{
	const char* pinyin = argc > 1 ? argv[1] : "sjie";

	pinyin_ime_t* ime = pinyin_ime_init("pinyin.txt", "dictionary.data");
	if (!ime)
	{
		fprintf(stderr, "初始化失败\n");
		return 1;
	}

	if (pinyin_ime_input(ime, pinyin) != PINYIN_IME_OK)
	{
		fprintf(stderr, "输入非法: %s\n", pinyin);
		pinyin_ime_destroy(ime);
		return 1;
	}
	show(ime, "input");

	int step = 0;
	while (!pinyin_ime_is_finished(ime) && pinyin_ime_get_candidate_count(ime) > 0)
	{
		int idx;
		printf("请选择: ");
	    scanf("%d", &idx);
		if (pinyin_ime_select(ime, idx) != PINYIN_IME_OK)
		{
			fprintf(stderr, "选择候选失败\n");
			break;
		}
		show(ime, "select");
		if (++step > 100)
			break;
	}

	printf("最终结果: %s\n", pinyin_ime_get_result(ime));

	if (pinyin_ime_save(ime, NULL) != PINYIN_IME_OK)
		fprintf(stderr, "保存词库失败\n");

	pinyin_ime_destroy(ime);
	return 0;
}
