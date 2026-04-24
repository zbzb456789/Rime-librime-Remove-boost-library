#include <rime/engine.h>
#include <rime/context.h>
#include <rime/key_event.h>
#include <rime/schema.h>
#include <rime/config.h>
#include <rime/service.h>
#include <rime/deployer.h>
#include <rime/candidate.h>
#include <rime/dict/reverse_lookup_dictionary.h>
#include <rime/gear/coinage_symbol_processor.h>
#include <rime/common.h>
#include <rime/key_table.h>
#include <rime/service.h>
#include <cctype>
#include <fstream>
#include <sstream>
#include <regex>

namespace rime {

CoinageSymbolProcessor::CoinageSymbolProcessor(const Ticket& ticket)
    : Processor(ticket) {}

// 获取用户目录下的扩展词库路径
std::string CoinageSymbolProcessor::GetDictPath() {
  // 使用 rime::path 的重载运算符来拼接路径，完美兼容 Windows 
  rime::path p = Service::instance().deployer().user_data_dir;
  p /= "wubi.extended.dict.yaml";
  return p.string();
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
      std::smatch match;
      if (std::regex_search(raw_code, match, letter_regex)) {
        raw_codes.push_back(match.str());
      } else {
        raw_codes.push_back("");
      }
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

  // --- 触发重新部署逻辑 ---
  Deployer& deployer(Service::instance().deployer());

  // 检查是否已经在运行部署任务，避免重复触发
  if (!deployer.IsWorking()) {
    // 它会检测 YAML 变动并重新生成 bin 文件
    deployer.ScheduleTask("workspace_update");
    
    // 启动后台线程执行上述任务
    deployer.StartMaintenance();
  }

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
            if (AppendEntry(text, code, 20)) {
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