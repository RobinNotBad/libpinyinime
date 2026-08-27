# libpinyinime  

一个简易、轻量的中文拼音输入法库，用于我的 Linux 嵌入式小程序。  
当然，你也可以随意使用哦。  

## 这个库能干什么？  

当然是用来拼音输入啦喵。  
这个库集成了 分词、匹配 两大主要功能。  
  
分词会优先寻找最长的有效音节  
支持输入裸拼音，比如 `keai`  
也支持手动分词，比如 `xi'an`  
  
分词结束后，将会遍历词表进行匹配  
（我确实想不到更好的方法了，不然就上sqlite数据库呗）  
然后根据 词语长度、拼音长度、频率 进行排序。  
  
因此，本库可以让某些性能打不过安卓，但又比单片机强一点的奇葩设备 ~~比如 Linux 词典笔~~ 拥有跟安卓输入法接近的拼音输入能力，远超 LVGL 自带的输入法。  
  
## 如何使用这个库？  

通过这个创建并初始化喵：  
```
pinyin_ime_t* pinyin_ime_init(const char* pinyin_path, const char* dictionary_path);
```
  
通过这个函数进行输入喵：  
```
int pinyin_ime_input(pinyin_ime_t* ime, const char* pinyin);
```
  
使用这些函数，可以获知候选词、分词结果和输入法状态喵：  
```
/* 获取当前剩余的拼音分词结果（UTF-8，如 "ni'hao'shi'jie"）*/
const char* pinyin_ime_get_segments(pinyin_ime_t* ime);

/* 获取目前已选中的汉字结果（UTF-8） */
const char* pinyin_ime_get_result(pinyin_ime_t* ime);

/* 获取当前候选词数量 */
int pinyin_ime_get_candidate_count(pinyin_ime_t* ime);

/* 获取第 index 个候选词（UTF-8），越界返回 NULL */
const char* pinyin_ime_get_candidate(pinyin_ime_t* ime, int index);

/* 是否已结束当前输入（无剩余分词） */
int pinyin_ime_is_finished(pinyin_ime_t* ime);
```
  
通过这个函数可以选词，以推进输入喵：  
```
int pinyin_ime_select(pinyin_ime_t* ime, int index);
```
  
使用完可以保存词库喵：  
```
int pinyin_ime_save(pinyin_ime_t* ime, const char* dictionary_path);
```
  
别忘了销毁喵：  
```
void pinyin_ime_destroy(pinyin_ime_t* ime);
```
  
## 如何编译这个库？  

1.可以./build-xxx.sh  
2.也可以make  
  
## Special Thanks  

原项目：`https://github.com/syhien/pinyin-IME`  
我对该项目进行了极大的魔改。  
（似乎已经完全不是原来的样子了，连核心算法都换了……  
但依然可以算是基于它，大概？  
  
词库：`https://github.com/zispace/hanzi-words-cycb`  
其实就是现代汉语常用词表，一共50000条左右。  
这个词库质量很高，比原项目那个好太多了。  
有少量词语出错，已经手动纠正了喵。  
  
