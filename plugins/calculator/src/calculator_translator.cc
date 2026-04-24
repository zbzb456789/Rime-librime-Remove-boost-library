#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include "calculator_translator.h"
#include <rime/registry.h>
#include <rime/translation.h>
#include <sstream>
#include <iomanip>
#include <functional>
#include <algorithm>
#include <cctype>
#include <ctime>
#include <fstream>
#include <rime/service.h>
#include <rime/deployer.h>
#include <chrono>
#include "tyme.h"

namespace calculator {

// 常量定义
static const char* kDigitsLower[] = {"〇", "一", "二", "三", "四", "五", "六", "七", "八", "九"};
static const char* CN_NUM[]={"零","一","二","三","四","五","六","七","八","九"};
static const char* kDigitsUpper[] = {"零", "壹", "贰", "叁", "肆", "伍", "陆", "柒", "捌", "玖"};
static const char* kUnitsLower[] = {"", "十", "百", "千"};
static const char* kUnitsUpper[] = {"", "拾", "佰", "仟"};
static const char* kGroupUnits[] = {"", "万", "亿", "兆"};

std::string NumToCN(int n){

    if(n<10)
        return CN_NUM[n];

    if(n==10)
        return "十";

    if(n<20)
        return std::string("十")+CN_NUM[n%10];

    int a=n/10;
    int b=n%10;

    if(b==0)
        return std::string(CN_NUM[a])+"十";

    return std::string(CN_NUM[a])+"十"+CN_NUM[b];
}

std::string YearToCN(const std::string& y){

    std::string r;

    for(char c:y)
        r+=kDigitsLower[c-'0'];

    return r;
}

int DaysOfYear(int y){

    if((y%400==0)||(y%4==0&&y%100!=0))
        return 366;

    return 365;
}


CalculatorTranslator::CalculatorTranslator(const Ticket& ticket)
    : Translator(ticket) {LoadSnippets();}

an<Translation> CalculatorTranslator::Query(const string& input, const Segment& segment) {
	
	  // =============================
// 日期 / 时间 / 星期
// =============================

if (input == "date") {
    auto translation = New<FifoTranslation>();
    AppendDateCandidates(translation, segment);
    return translation;
}

if (input == "time") {
    auto translation = New<FifoTranslation>();
    AppendTimeCandidates(translation, segment);
    return translation;
}

if (input == "week") {
    auto translation = New<FifoTranslation>();
    AppendWeekCandidates(translation, segment);
    return translation;
}




if (input == "nong") {
  auto translation = New<FifoTranslation>();

  time_t t = time(nullptr);
  tm* now = localtime(&t);

  int y = now->tm_year + 1900;
  int m = now->tm_mon + 1;
  int d = now->tm_mday;
  int h = now->tm_hour;
  int min = now->tm_min;
  int s = now->tm_sec;

  using namespace tyme;

  // 当前时间
  SolarTime st = SolarTime::from_ymd_hms(y, m, d, h, min, s);
  LunarHour lh = st.get_lunar_hour();
  LunarDay lunar = lh.get_lunar_day();

  LunarYear ly = LunarYear::from_year(lunar.get_year());

  // 干支年
  std::string ganzhi_year = ly.get_sixty_cycle().get_name();

  // 生肖
  std::string zodiac = ly.get_sixty_cycle()
                         .get_earth_branch()
                         .get_zodiac()
                         .get_name();

  // 月日
  std::string month_name =
      LunarMonth::from_ym(lunar.get_year(), lunar.get_month()).get_name();

  std::string day_name = lunar.get_name();

  // 时辰
  std::string hour_gz = lh.get_sixty_cycle().get_name();

  static const std::string TIME_DESCS[] = {
    "(夜半｜三更)", "(鸡鸣｜四更)", "(平旦｜五更)",
    "(日出)", "(食时)", "(隅中)",
    "(日中)", "(日昳)", "(哺时)",
    "(日入)", "(黄昏｜一更)", "(人定｜二更)"
  };

  int hour_index =
      lh.get_sixty_cycle().get_earth_branch().get_index();

  std::string time_desc = TIME_DESCS[hour_index];

  // 八字
  std::string eight_char = lh.get_eight_char().to_string();

  // ===== 3个候选 =====

  // 1 农历日期
  std::string c1 =
      "农历" + ganzhi_year + "年(" + zodiac + ")" +
      month_name + day_name;

  // 2 农历 + 时辰
  std::string c2 =
      "农历" + ganzhi_year + "年(" + zodiac + ")" +
      month_name + day_name +
      hour_gz + "时" + time_desc;

  // 3 八字
  std::string c3 = eight_char;

  std::vector<std::string> out = {c1, c2, c3};

  for (auto& s : out) {
    auto cand = New<SimpleCandidate>(
        "calculator", segment.start, segment.end, s);
    cand->set_comment("〈农历〉");
    translation->Append(cand);
  }

  return translation;
}


if (input == "jieq") {
  auto translation = New<FifoTranslation>();

  time_t t = time(nullptr);
  tm* now = localtime(&t);

  int y = now->tm_year + 1900;
  int m = now->tm_mon + 1;
  int d = now->tm_mday;
  int h = now->tm_hour;
  int min = now->tm_min;
  int s = now->tm_sec;

  using namespace tyme;

  // 当前时间
  SolarTime st = SolarTime::from_ymd_hms(y, m, d, h, min, s);

  // 当前节气
  SolarTerm current = st.get_term();

  // 输出未来24个节气
  for (int i = 0; i < 25; i++) {

    SolarTerm jq = current.next(i);

    SolarDay day = jq.get_solar_day();

    std::string s =
      jq.get_name() + " " + day.to_string();

    auto cand = New<SimpleCandidate>(
      "calculator", segment.start, segment.end, s);

    cand->set_comment("〈节气〉");

    translation->Append(cand);
  }

  return translation;
}







// =============================
    // 新增：自定义短语匹配
    // =============================
    auto it = snippets_.find(input);
    if (it != snippets_.end()) {
        auto translation = New<FifoTranslation>();
        
        // 使用你统一的 "calculator" 候选词类型
        auto cand = New<SimpleCandidate>(
            "calculator", 
            segment.start, 
            segment.end, 
            it->second
        );
        cand->set_comment("〈短语〉");
        translation->Append(cand);
        
        // 匹配到了短语就直接返回，不再往下走数学计算
        return translation;
    }






  // 限定必须是大写的 'V'、'R' 或 'U'
  if (input.empty() || (input[0] != 'V' && input[0] != 'R' && input[0] != 'U')) 
    return nullptr;

  char trigger = input[0]; // 记录是 V, R 还是 U
  std::string exp = input.substr(1);
  if (exp.empty()) return nullptr;

  auto translation = New<FifoTranslation>();
  // 【关键修改】将 type 从 "number" 改为 "calculator"，让 filter 识别
  std::string cand_type = "calculator";

  // ==========================================
  // U 触发 Unicode 转换逻辑
  // ==========================================
  if (trigger == 'U') {
    if (exp.empty()) return nullptr;
    
    // 校验输入是否全为合法的十六进制字符
    for (char c : exp) {
        if (!std::isxdigit(c)) return nullptr; 
    }

    try {
        // 将十六进制字符串转为无符号整数
        uint32_t cp = std::stoul(exp, nullptr, 16);
        
        // Unicode 最大有效码位是 0x10FFFF
        if (cp > 0x10FFFF) {
            auto cand = New<SimpleCandidate>(cand_type, segment.start, segment.end, "超出Unicode范围");
            cand->set_comment("〈提示〉");
            translation->Append(cand);
            return translation;
        }
        
        // 正常转换
        std::string utf8_char = CodePointToUTF8(cp);
        if (!utf8_char.empty()) {
            auto cand = New<SimpleCandidate>(cand_type, segment.start, segment.end, utf8_char);
            // --- 细化识别私用区 ---
            if (cp >= 0xE000 && cp <= 0xF8FF) {
                cand->set_comment("〈Unicode:BMP区〉");
            } 
            else if (cp >= 0xF0000 && cp <= 0xFFFFD) {
                cand->set_comment("〈Unicode:PUP-A区〉"); // 对应第15平面
            } 
            else if (cp >= 0x100000 && cp <= 0x10FFFD) {
                cand->set_comment("〈Unicode:PUP-B区〉"); // 对应第16平面
            } 
            else {
                cand->set_comment("〈Unicode〉");
            }
            // ---------------------
            translation->Append(cand);
            return translation;
        }
    } catch (...) {
        // 捕获异常：如果输入的十六进制太长，超出了 stoul 的处理极限，也会走到这里
        auto cand = New<SimpleCandidate>(cand_type, segment.start, segment.end, "超出Unicode范围");
        cand->set_comment("〈提示〉");
        translation->Append(cand);
        return translation;
    }
    
    return nullptr;
  }

  // ==========================================
  // V 和 R 的数学计算逻辑
  // ==========================================
  double result = 0.0;
  try {
    // 增加长度保护，超过 30 位直接放弃，防止极端计算导致的计算卡死
    if (exp.length() > 30) return nullptr;
    result = ExecuteExpression(exp);
  } catch (...) { return nullptr; }

  if (trigger == 'V') {
    std::stringstream ss;
    ss << std::setprecision(10) << result;
    // 1. 等式
    auto cand1 = New<SimpleCandidate>(cand_type, segment.start, segment.end, exp + "=" + ss.str());
    cand1->set_comment("〈等式〉");
    translation->Append(cand1);
    
    // 2. 结果
    auto cand2 = New<SimpleCandidate>(cand_type, segment.start, segment.end, ss.str());
    cand2->set_comment("〈结果〉");
    translation->Append(cand2);
  } 
  else if (trigger == 'R') {
    // 增加数值溢出检查：double 转 long long 超过 10^16 (千万亿) 就停止转换中文
    if (std::abs(result) >= 1e16) {
        auto cand = New<SimpleCandidate>(cand_type, segment.start, segment.end, "数值过大");
        translation->Append(cand);
        return translation;
    }
    // 1. 大写金额
    auto cand1 = New<SimpleCandidate>(cand_type, segment.start, segment.end, NumberToChinese(result, true, true));
    cand1->set_comment("〈大写金额〉");
    translation->Append(cand1);
    
    // 2. 大写数字
    auto cand2 = New<SimpleCandidate>(cand_type, segment.start, segment.end, NumberToChinese(result, false, true));
    cand2->set_comment("〈大写数字〉");
    translation->Append(cand2);

    // 3. 小写金额
    auto cand3 = New<SimpleCandidate>(cand_type, segment.start, segment.end, NumberToChinese(result, true, false));
    cand3->set_comment("〈小写金额〉");
    translation->Append(cand3);
    
    // 4. 小写数字
    auto cand4 = New<SimpleCandidate>(cand_type, segment.start, segment.end, NumberToChinese(result, false, false));
    cand4->set_comment("〈小写数字〉");
    translation->Append(cand4);
	
	// 5. 财务千分位纯数字
    auto cand5 = New<SimpleCandidate>(cand_type, segment.start, segment.end, FormatWithThousands(result));
	cand5->set_comment("〈千分位格式〉");
	translation->Append(cand5);
  }
  
  return translation;
}

// --- 正式中文读数算法 (壹佰贰拾叁元) ---
std::string CalculatorTranslator::NumberToChinese(double num, bool is_money, bool is_upper) {
    if (std::abs(num) < 1e-9) return is_upper ? "零" : "〇"; //
    
    std::string prefix = (num < 0) ? "负" : ""; //
    num = std::abs(num); //

    double int_part;
    // 使用精准的 0.5 偏移防止精度损失
    double dec_part = std::modf(num + 0.000000001, &int_part); //

    char buf[64];
    snprintf(buf, sizeof(buf), "%.0f", int_part); //
    std::string int_str = buf;

    // --- 核心修复：精准提取小数位 ---
    std::string dec_str = "";

	if (is_money) {
		// ===== 金额：截断到角分（不四舍五入）=====
		long long fractional = (long long)((num + 1e-9) * 100) % 100;

		if (fractional > 0) {
			dec_str = std::to_string(fractional);
			if (fractional < 10)
				dec_str = "0" + dec_str;
		}

	} else {
		// ===== 普通数字：保留5位小数（截断，不四舍五入）=====
		double scaled = (num - int_part);

		// 放大 5 位后直接截断
		long long fractional = (long long)((scaled + 1e-12) * 100000);

		if (fractional > 0) {
			dec_str = std::to_string(fractional);

			// 补齐前导零（例如 0.00012）
			while (dec_str.length() < 5)
				dec_str = "0" + dec_str;

			// 去掉尾随 0
			dec_str.erase(dec_str.find_last_not_of('0') + 1);
		}
	}


    std::string s_int = GetIntPartChinese(int_str, is_upper); //
    std::string s_dec = GetDecPartChinese(dec_str, is_money, is_upper); //
    
    if (is_money) {
        return prefix + s_int + "元" + (s_dec.empty() ? "整" : s_dec); //
    } else {
        return prefix + s_int + (s_dec.empty() ? "" : "点" + s_dec); //
    }
}

std::string CalculatorTranslator::FormatWithThousands(double num) {
    bool negative = num < 0;
    num = std::abs(num);

    std::stringstream ss;
    ss << std::fixed << std::setprecision(2) << num;  // 普通数字保留3位小数
    std::string s = ss.str();

    // 分离整数和小数
    std::string int_part, dec_part;
    size_t pos = s.find('.');
    if (pos != std::string::npos) {
        int_part = s.substr(0, pos);
        dec_part = s.substr(pos);  // 包含 .
    } else {
        int_part = s;
    }

    // 加千分位
    std::string formatted;
    int count = 0;
    for (int i = int_part.size() - 1; i >= 0; --i) {
        formatted.insert(formatted.begin(), int_part[i]);
        count++;
        if (count == 3 && i != 0) {
            formatted.insert(formatted.begin(), ',');
            count = 0;
        }
    }

    if (negative)
        formatted = "-" + formatted;

    return formatted + dec_part;
}


std::string CalculatorTranslator::GetIntPartChinese(const std::string& int_str, bool is_upper) {
    if (int_str == "0" || int_str.empty()) return is_upper ? "零" : "〇"; //

    const char** digits = is_upper ? kDigitsUpper : kDigitsLower; //
    const char** units = is_upper ? kUnitsUpper : kUnitsLower; //
    const char* g_units[] = {"", "万", "亿", "兆"}; //

    std::string res = "";
    long long val = std::stoll(int_str); //
    int group_count = 0;
    bool last_group_is_zero = false;

    while (val > 0) {
        int part = val % 10000; //
        if (part == 0) {
            last_group_is_zero = true; //
        } else {
            std::string part_s = "";
            bool zero_flag = false;
            int temp = part;
            for (int i = 0; i < 4; ++i) {
                int d = temp % 10; //
                if (d == 0) {
                    if (!part_s.empty()) zero_flag = true; //
                } else {
                    if (zero_flag) { part_s = std::string(digits[0]) + part_s; zero_flag = false; }
                    part_s = std::string(digits[d]) + units[i] + part_s; //
                }
                temp /= 10;
            }
            // 如果之前有全零节，补一个零
            if (last_group_is_zero && !res.empty()) res = std::string(digits[0]) + res; 
            res = part_s + g_units[group_count] + res; //
            last_group_is_zero = false;
        }
        val /= 10000;
        group_count++;
    }
    return res;
}

std::string CalculatorTranslator::GetDecPartChinese(
    const std::string& dec_str,
    bool is_money,
    bool is_upper) 
{
    if (dec_str.empty()) return "";

    const char** digits = is_upper ? kDigitsUpper : kDigitsLower;
    std::string res;

    if (is_money) {
        int jiao = dec_str.size() > 0 ? dec_str[0] - '0' : 0;
        int fen  = dec_str.size() > 1 ? dec_str[1] - '0' : 0;

        if (jiao > 0) {
            res += digits[jiao];
            res += "角";
        } else if (fen > 0) {
            res += is_upper ? "零" : "〇";
        }

        if (fen > 0) {
            res += digits[fen];
            res += "分";
        }
    } else {
        for (char c : dec_str) {
            if (c >= '0' && c <= '9')
                res += digits[c - '0'];
        }
    }

    return res;
}

// 【修改】将合法的 Unicode 码位直接转为 UTF-8 字符串
std::string CalculatorTranslator::CodePointToUTF8(uint32_t cp) {
    std::string res;
    
    // 按照 UTF-8 编码规则将 Unicode 码点(Code Point)写入字符串
    if (cp <= 0x7F) {
        res.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FF) {
        res.push_back(static_cast<char>(0xC0 | ((cp >> 6) & 0x1F)));
        res.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        res.push_back(static_cast<char>(0xE0 | ((cp >> 12) & 0x0F)));
        res.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        res.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0x10FFFF) {
        res.push_back(static_cast<char>(0xF0 | ((cp >> 18) & 0x07)));
        res.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        res.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        res.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
    
    return res;
}

std::string CalculatorTranslator::GetCurrentDate(const char* fmt) {
    char buf[64];
    time_t t = time(nullptr);
    strftime(buf, sizeof(buf), fmt, localtime(&t));
    return buf;
}

void CalculatorTranslator::AppendDateCandidates(
    an<FifoTranslation> translation,
    const Segment& segment) {

    time_t t=time(nullptr);
    tm* now=localtime(&t);

    int Y=now->tm_year+1900;
    int M=now->tm_mon+1;
    int D=now->tm_mday;

    int week=now->tm_yday/7+1;
    int day_of_year=now->tm_yday+1;

    char buf[64];

    std::vector<std::string> out;

    //1
    sprintf(buf,"%04d年%02d月%02d日",Y,M,D);
    out.push_back(buf);

    //2
    sprintf(buf,"%04d-%02d-%02d",Y,M,D);
    out.push_back(buf);

    //3
    sprintf(buf,"%04d%02d%02d",Y,M,D);
    out.push_back(buf);

    //4
    sprintf(buf,"%04d.%02d.%02d",Y,M,D);
    out.push_back(buf);

    //5 周数
    sprintf(buf,"%04d-%02d-%02d 第%02d周",Y,M,D,week);
    out.push_back(buf);

    //6 中文日期
    std::string cn=
        YearToCN(std::to_string(Y))+
        "年"+
        NumToCN(M)+"月"+
        NumToCN(D)+"日";

    out.push_back(cn);

    //7 年积日
    sprintf(buf,"%04d-%02d-%02d｜%03d/%d",
        Y,M,D,
        day_of_year,
        DaysOfYear(Y));

    out.push_back(buf);


    for(auto& s:out){

        auto cand=New<SimpleCandidate>(
            "calculator",
            segment.start,
            segment.end,
            s);

        cand->set_comment("〈日期〉");

        translation->Append(cand);
    }
}


void CalculatorTranslator::AppendTimeCandidates(
    an<FifoTranslation> translation,
    const Segment& segment) {

    time_t t=time(nullptr);
    tm* now=localtime(&t);

    int Y=now->tm_year+1900;
    int M=now->tm_mon+1;
    int D=now->tm_mday;

    int h=now->tm_hour;
    int m=now->tm_min;
    int s=now->tm_sec;

    char buf[64];

    std::vector<std::string> out;

    //1
    sprintf(buf,"%04d-%02d-%02d %02d:%02d:%02d",
        Y,M,D,h,m,s);

    out.push_back(buf);

    //2
    sprintf(buf,"%02d:%02d:%02d",h,m,s);
    out.push_back(buf);

    //3
    sprintf(buf,"%04d年%02d月%02d日 %02d时%02d分%02d秒",
        Y,M,D,h,m,s);

    out.push_back(buf);

    //4
    sprintf(buf,"%d时%d分%d秒",h,m,s);
    out.push_back(buf);

    //5 中文时间
    std::string cn=
        NumToCN(h)+"时"+
        NumToCN(m)+"分"+
        NumToCN(s)+"秒";

    out.push_back(cn);


    for(auto& s:out){

        auto cand=New<SimpleCandidate>(
            "calculator",
            segment.start,
            segment.end,
            s);

        cand->set_comment("〈时间〉");

        translation->Append(cand);
    }
}


int CalculatorTranslator::GetWeekDay() {
    time_t t = time(nullptr);
    tm* now = localtime(&t);
    return now->tm_wday;
}

void CalculatorTranslator::AppendWeekCandidates(
    an<FifoTranslation> translation,
    const Segment& segment) {

    const char* names[]={"日","一","二","三","四","五","六"};
    int w = GetWeekDay();

    std::vector<std::string> out = {
        std::string("周") + names[w],
        std::string("星期") + names[w],
        std::string("礼拜") + names[w]
    };

    for (auto& s : out) {
        auto cand = New<SimpleCandidate>(
            "calculator",
            segment.start,
            segment.end,
            s);
        cand->set_comment("〈星期〉");
        translation->Append(cand);
    }
}



// --- 新增：文件加载逻辑 ---
void CalculatorTranslator::LoadSnippets() {
  snippets_.clear();

  // 获取用户目录
  std::string user_dir = Service::instance().deployer().user_data_dir.string();
  if (user_dir.empty()) user_dir = ".";
  std::string file_path = user_dir + "/phrase.txt";

  std::ifstream fin(file_path);
  if (!fin.is_open()) return; // 找不到文件就不管它

  std::string line;
  while (std::getline(fin, line)) {
    size_t tab_pos = line.find('\t');
    if (tab_pos != std::string::npos && tab_pos > 0) {
      std::string key = line.substr(0, tab_pos);
      std::string val = line.substr(tab_pos + 1);
      snippets_[key] = ReplaceNewline(val); // 存入内存
    }
  }
  fin.close();
}

// --- 新增：换行符转换逻辑 ---
std::string CalculatorTranslator::ReplaceNewline(const std::string& str) {
  std::string result = str;
  std::string target = "\\n";
  std::string replacement = "\r\n";
  size_t pos = 0;
  while ((pos = result.find(target, pos)) != std::string::npos) {
    result.replace(pos, target.length(), replacement);
    pos += replacement.length();
  }
  return result;
}



// 表达式解析逻辑 (保持不变)
double CalculatorTranslator::ExecuteExpression(const std::string& expr) {
  expr_str_ = expr;
  expr_str_.erase(std::remove_if(expr_str_.begin(), expr_str_.end(), ::isspace), expr_str_.end());
  pos_ = 0;
  return parse_expression();
}
char CalculatorTranslator::peek() { return pos_ < expr_str_.length() ? expr_str_[pos_] : 0; }
char CalculatorTranslator::get() { return expr_str_[pos_++]; }
double CalculatorTranslator::parse_expression() {
  double res = parse_term();
  while (peek() == '+' || peek() == '-') res = (get() == '+') ? res + parse_term() : res - parse_term();
  return res;
}
double CalculatorTranslator::parse_term() {
  double res = parse_factor();
  while (peek() == '*' || peek() == '/' || peek() == '%') {
    char op = get(); 
    if (op == '%') { double next = parse_factor(); res = mod_func(res, next); }
    else { double next = parse_factor(); res = (op == '*') ? res * next : (next != 0 ? res / next : 0); }
  }
  return res;
}
double CalculatorTranslator::parse_factor() {
  if (std::isdigit(peek())) {
    size_t sz; double v = std::stod(expr_str_.substr(pos_), &sz);
    pos_ += sz; return v;
  } else if (peek() == '(') { get(); double v = parse_expression(); get(); return v; }
  else if (peek() == '-') { get(); return -parse_factor(); }
  else if (std::isalpha(peek())) {
    std::string f; while (std::isalpha(peek())) f += get();
    if (get() != '(') return 0;
    double a = parse_expression(); 
    double b = 0; 
    if (peek() == ',') { get(); b = parse_expression(); } // 支持双参数如 mod, round
    get();
    if (f == "sin") return sin_func(a);
    if (f == "cos") return cos_func(a);
    if (f == "tan") return tan_func(a);
    if (f == "sqrt") return std::sqrt(a);
    if (f == "round") return round_func(a, b == 0 ? 1 : b);
  }
  return 0;
}

// 数学函数实现
double CalculatorTranslator::abs_func(double x) { return std::fabs(x); }
double CalculatorTranslator::floor_func(double x) { return std::floor(x); }
double CalculatorTranslator::ceil_func(double x) { return std::ceil(x); }
double CalculatorTranslator::round_func(double x, double dc) { if (dc==0) return x; double f=1.0/dc; return std::round(x*f)/f; }
double CalculatorTranslator::mod_func(double x, double y) { return std::fmod(x, y); }
double CalculatorTranslator::cos_func(double x) { return std::cos(x); }
double CalculatorTranslator::sin_func(double x) { return std::sin(x); }
double CalculatorTranslator::tan_func(double x) { return std::tan(x); }
double CalculatorTranslator::acos_func(double x) { return std::acos(x); }
double CalculatorTranslator::asin_func(double x) { return std::asin(x); }
double CalculatorTranslator::atan_func(double x) { return std::atan(x); }
double CalculatorTranslator::deg_func(double x) { return x * 180.0 / M_PI; }
double CalculatorTranslator::rad_func(double x) { return x * M_PI / 180.0; }
double CalculatorTranslator::trunc_func(double x, double dc) { return std::trunc(x); }
double CalculatorTranslator::sum_func(const std::vector<double>& arr) { return 0; }
double CalculatorTranslator::avg_func(const std::vector<double>& arr) { return 0; }

} // namespace calculator