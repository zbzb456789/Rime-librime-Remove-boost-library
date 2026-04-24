#ifndef RIME_CALCULATOR_TRANSLATOR_H_
#define RIME_CALCULATOR_TRANSLATOR_H_

#include <rime/translator.h>
#include <rime/translation.h>
#include <rime/candidate.h>
#include <rime/segmentation.h>
#include <string>
#include <vector>

namespace calculator {

using namespace rime;

class CalculatorTranslator : public Translator {
 public:
  explicit CalculatorTranslator(const Ticket& ticket);
  an<Translation> Query(const string& input, const Segment& segment) override;

 private:
 
 // --- 新增：自定义短语功能 ---
  void LoadSnippets();
  std::string ReplaceNewline(const std::string& str);
  std::map<std::string, std::string> snippets_; // 用于在内存中缓存短语
 
 // 日期相关
void AppendDateCandidates(an<FifoTranslation> translation, const Segment& segment);
void AppendTimeCandidates(an<FifoTranslation> translation, const Segment& segment);
void AppendWeekCandidates(an<FifoTranslation> translation, const Segment& segment);

// 工具函数
std::string GetCurrentDate(const char* fmt);
int GetWeekDay();

  // --- 核心逻辑 ---
  double ExecuteExpression(const std::string& expr);
  
  std::string FormatWithThousands(double num);


  // 【修改】将解析后的 Unicode 码位直接转为 UTF-8 字符
  std::string CodePointToUTF8(uint32_t cp);
  // --- 中文转换逻辑 ---
  // 将数字转为中文（包含金额、大小写等处理）
  std::string NumberToChinese(double num, bool is_money, bool is_upper);
  // 处理整数部分的读法（含万、亿分节）
  std::string GetIntPartChinese(const std::string& int_str, bool is_upper);
  // 处理小数部分
  std::string GetDecPartChinese(const std::string& dec_str, bool is_money, bool is_upper);

  // --- 表达式解析器变量 ---
  std::string expr_str_;
  size_t pos_;

  // --- 解析器辅助函数 ---
  char peek();
  char get();
  double parse_expression();
  double parse_term();
  double parse_factor();
  // 注意：之前的 .cc 代码将 parse_function 逻辑内联到了 parse_factor 中
  // 所以这里不再需要 parse_function 的声明，或者您可以保留它以便后续扩展

  // --- 数学函数实现 (Implementation) ---
  // 这些函数在 .cc 底部已经实现，可以在 parse_factor 中调用
  double abs_func(double x);
  double floor_func(double x);
  double ceil_func(double x);
  double round_func(double x, double dc);
  double mod_func(double x, double y);
  double trunc_func(double x, double dc);
  
  double cos_func(double x);
  double sin_func(double x);
  double tan_func(double x);
  double acos_func(double x);
  double asin_func(double x);
  double atan_func(double x);
  double deg_func(double x);
  double rad_func(double x);

  double sum_func(const std::vector<double>& arr);
  double avg_func(const std::vector<double>& arr);
};

}  // namespace calculator

#endif