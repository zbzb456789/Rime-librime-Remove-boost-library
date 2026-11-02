#include <rime/engine.h>
#include <rime/context.h>
#include <rime/key_event.h>
#include <rime/schema.h>
#include <rime/config.h>
#include <rime/service.h>
#include <rime/deployer.h>
#include <rime/ticket.h>
#include <rime/candidate.h>
#include <rime/dict/reverse_lookup_dictionary.h>
#include <rime/dict/user_dictionary.h>
#include <rime/dict/vocabulary.h>
#include <rime/gear/coinage_symbol_processor.h>
#include <rime/common.h>
#include <rime/key_table.h>
#include <algorithm>
#include <filesystem>
#include <string>
#include <string_view>
#include <cctype>
#include <fstream>
#include <sstream>
#include <regex>

namespace rime {

// 追加词条时写入词库的权重。
//
// 同一编码下的词条是按权重降序排的（ShortDictEntry::operator<），
// 而 wubi.dict.yaml 里的权重上限是 210（english / quick_symbols 没有权重列，
// 视为 0）。取 1000 就能让手工造出来的词稳定压过同码的所有静态词条，
// 排在候选第一位。以后想给某些词库让路，把它调小即可。
static constexpr int kCoinageWeight = 500;

CoinageSymbolProcessor::CoinageSymbolProcessor(const Ticket& ticket)
    : Processor(ticket) {}

// 获取用户目录下的扩展词库路径
std::string CoinageSymbolProcessor::GetDictPath() {
  // 使用 rime::path 的重载运算符来拼接路径，完美兼容 Windows 
  rime::path p = Service::instance().deployer().user_data_dir;
  p /= "wubi.extended.dict.yaml";
  return p.string();
}

// 把新词写进 librime 用户词典：写库即生效，全平台零部署。
//
// 为什么不再走「改静态词库 + 重新编译」：
//   静态词库必须编译成 build/*.table.bin 才能查。而重编译要求宿主 IME 先卸载
//   正在使用的词典（这些文件已经被 mmap），编译完还要重新加载会话；在 Windows
//   上直接重写一个本进程正在映射的 table.bin，文件句柄会拿到 ERROR_USER_MAPPED_FILE，
//   编译根本落不了盘。「谁卸载、谁重载」各平台做法不同，只能由宿主完成 —— 这就是
//   原来那段 #ifdef 存在的根本原因，也是它无法既跨平台又立刻生效的原因。
//   用户词典是 librime 自带的 LevelDB：UpdateEntry 直接写库，LookupWords
//   每次查询都实时读同一份库，所以写完这一瞬间就能打出来。
//
// 前置条件：方案里要给 translator 打开 enable_user_dict（默认 true，
// 但 wubi.schema.yaml 显式写成了 false）。没打开时 UserDictionaryComponent::Create
// 直接返回 NULL，这里会立刻返回 false，不会有任何额外开销。
bool CoinageSymbolProcessor::MemorizeEntry(const std::string& text,
                                           const std::string& code) {
  if (text.empty() || code.empty() || !engine_ || !engine_->schema()) {
    return false;
  }
  auto user_dictionary = UserDictionary::Require("user_dictionary");
  if (!user_dictionary) {
    return false;
  }
  // name space 必须和 translator 一致：UserDictionaryComponent 内部按词典名共享
  // 同一个 LevelDB，只有取到同一本用户词典，当前会话才能立刻看到新词。
  Ticket ticket(engine_, "translator");
  the<UserDictionary> user_dict(user_dictionary->Create(ticket));
  if (!user_dict) {
    return false;  // 方案没打开 enable_user_dict
  }
  if (!user_dict->Load() || user_dict->readonly()) {
    return false;
  }

  DictEntry entry;
  entry.text = text;
  // 用户词典的 key 是 "<音节 音节 ... >\t<词>"：音节之间空格分隔、末尾还有一个
  // 空格（UserDictionary::TranslateCodeToString 就是这么拼的）。表形码/五笔把整串
  // 码当成一个音节，所以补一个空格就与「平时打字选词自动学习」写进去的条目
  // 完全同构：输入整串码时 full_code 去掉尾空格后长度等于输入长度，
  // remaining_code_length == 0，会被 TableTranslation 当作精确匹配、
  // 并以 is_user_phrase 优先于同码的静态词条取出。
  entry.custom_code = code;
  entry.custom_code.push_back(' ');
  // commits = 1，与「打字选词自动学习」一致。
  const bool ok = user_dict->UpdateEntry(entry, 1);
  LOG(INFO) << "coinage: memorize '" << text << "' as code '" << code
            << "': " << (ok ? "ok" : "failed");
  return ok;
}

// 检查词条是否存在
bool CoinageSymbolProcessor::EntryExists(const std::string& path, const std::string& text) {
  std::ifstream ifs(path);
  if (!ifs.is_open()) return false;

  std::string line;
  while (std::getline(ifs, line)) {
    // 忽略 YAML 头部和注释行
    if (line.empty() || line.starts_with('#') || line.starts_with("---") || line.starts_with("...")) {
      continue;
    }
    // 提取第一个 Tab 前的内容
    size_t tab_pos = line.find('\t');
    if (tab_pos != std::string::npos) {
      if (std::string_view(line).substr(0, tab_pos) == text) {
        return true;
      }
    }
  }
  return false;
}

// UTF-8 字符分割
std::vector<std::string> CoinageSymbolProcessor::SplitUTF8(const std::string& str) {
  std::vector<std::string> chars;
  for (size_t i = 0; i < str.length();) {
    int cplen = 1;

    unsigned char c = static_cast<unsigned char>(str[i]); 
    if ((c & 0xf8) == 0xf0) cplen = 4;
    else if ((c & 0xf0) == 0xe0) cplen = 3;
    else if ((c & 0xe0) == 0xc0) cplen = 2;
    
    chars.emplace_back(str, i, cplen);
    i += cplen;
  }
  return chars;
}

// 安全截取，避免越界
std::string CoinageSymbolProcessor::SafeSubstr(const std::string& s, size_t pos, size_t len) {
  if (pos >= s.length()) return "";
  return s.substr(pos, std::min(len, s.length() - pos));
}

// 根据单字反查生成五笔 4 码
std::string CoinageSymbolProcessor::GenerateWubiCode(const std::string& text) {
  // 安全的路径拼接
  rime::path rime_db_path = Service::instance().deployer().user_data_dir;
  rime_db_path /= "build";
  rime_db_path /= "wubi.extended.reverse.bin";
  
  ReverseDb db(rime_db_path);
  if (!db.Load()) return "";

  std::vector<std::string> utf8_chars = SplitUTF8(text);
  std::vector<std::string> raw_codes;
  
  // 用于提取字母部分的正则表达式
  std::regex letter_regex("[a-z]+");

  for (const auto& ch : utf8_chars) {
    std::string raw_code;
    if (db.Lookup(ch, &raw_code)) {
      std::string best_code;
      // 使用 sregex_iterator 遍历所有的正则匹配项
      std::sregex_iterator next(raw_code.begin(), raw_code.end(), letter_regex);
      std::sregex_iterator end;
      
      while (next != end) {
        // 提取最长的编码作为全码（例如从 "m mm mmm mmmm" 中提取 "mmmm"）
        if (next->str().length() > best_code.length()) {
          best_code = next->str();
        }
        next++;
      }
      
      raw_codes.push_back(best_code);
    } else {
      raw_codes.push_back("");
    }
  }

  size_t n = raw_codes.size();
  if (n == 0) return "";

  // 五笔组词规则
  if (n == 2) {
    return SafeSubstr(raw_codes[0], 0, 2) + SafeSubstr(raw_codes[1], 0, 2);
  } else if (n == 3) {
    return SafeSubstr(raw_codes[0], 0, 1) + SafeSubstr(raw_codes[1], 0, 1) + SafeSubstr(raw_codes[2], 0, 2);
  } else if (n >= 4) {
    return SafeSubstr(raw_codes[0], 0, 1) + SafeSubstr(raw_codes[1], 0, 1) + 
           SafeSubstr(raw_codes[2], 0, 1) + SafeSubstr(raw_codes[n - 1], 0, 1);
  } else {
    return raw_codes[0];
  }
}

// 追加写入词库
bool CoinageSymbolProcessor::AppendEntry(const std::string& text, const std::string& code, int weight) {
  std::string path = GetDictPath();
  
  if (EntryExists(path, text)) {
    return true; 
  }

    // 使用 fstream 同时开启读写和追加模式
    std::fstream fs(path, std::ios::in | std::ios::out | std::ios::ate);
    if (!fs.is_open()) return false;

    // 防粘连检测：检查文件末尾是否有换行符
    if (fs.tellg() > 0) {
        fs.seekg(-1, std::ios::end);
        char last_char;
        fs.get(last_char);
        if (last_char != '\n') {
            // 如果最后一行没有换行符，先补一个
            fs.clear(); // 清除 EOF 标志
            fs.seekp(0, std::ios::end);
            fs << "\n";
        }
    }

    // 将游标切回末尾，安全写入新词条
    fs.clear();
    fs.seekp(0, std::ios::end);
    fs << text << "\t" << code << "\t" << weight << "\n";

    // 让新词立刻可用：直接写进 librime 用户词典（全平台一致，无部署、无重启）。
    // wubi.extended.dict.yaml 里那行仍然保留，作为「下次完整部署时并入静态表」
    // 的归档，所以词库文件的工作流不变。
    MemorizeEntry(text, code);

  return true;
}

ProcessResult CoinageSymbolProcessor::ProcessKeyEvent(const KeyEvent& key_event) {
  Context* ctx = engine_->context();
  int ch = key_event.keycode();

  // 1. 原有逻辑：分号快速符号
  if (ctx->input() == ";" && std::isalpha(static_cast<unsigned char>(ch))) {
    ctx->PushInput(ch); 
    if (ctx->HasMenu()) {
      ctx->ConfirmCurrentSelection();
      ctx->Commit();
      return kAccepted; 
    }
    return kNoop;
  }

  // 2. 造词 (~ 开头 + 空格触发)
  // keycode 为 32 即空格键
  if (ch == 32 && !key_event.release() && !key_event.ctrl() && !key_event.alt()) {
    if (ctx->IsComposing()) {
      std::string input = ctx->input();
      
      // 检查是否以 `~` 引导
      if (input.length() > 1 && input[0] == '~') {
        auto cand = ctx->GetSelectedCandidate();
        if (cand) {
          std::string text = cand->text();
          std::string code = GenerateWubiCode(text);

          // 如果成功获取到编码
          if (!code.empty()) {
            if (AppendEntry(text, code, kCoinageWeight)) {

              // 发送文本到客户端屏幕
              engine_->CommitText(text);
              // 清理当前的 '~xxx' 编码状态
              ctx->Clear();
             
              return kAccepted;
            }
          }
        }
      }
    }
  }

  return kNoop; 
}

}  // namespace rime}  // namespace rime
