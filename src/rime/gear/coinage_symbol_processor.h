#ifndef RIME_GEAR_CUSTOM_SWITCH_PROCESSOR_H_
#define RIME_GEAR_CUSTOM_SWITCH_PROCESSOR_H_

#include <rime/component.h>
#include <rime/processor.h>
#include <string>
#include <vector>

namespace rime {

class CoinageSymbolProcessor : public Processor {
 public:
  CoinageSymbolProcessor(const Ticket& ticket);
  virtual ProcessResult ProcessKeyEvent(const KeyEvent& key_event);

 private:
  // 辅助功能声明
  std::string GetDictPath();
  bool EntryExists(const std::string& path, const std::string& text);
  std::string GenerateWubiCode(const std::string& text);
  bool AppendEntry(const std::string& text, const std::string& code, int weight);
  // 把新词写进 librime 用户词典，写入即生效（跨平台，无需部署）
  bool MemorizeEntry(const std::string& text, const std::string& code);
  
  // UTF-8 分字工具
  std::vector<std::string> SplitUTF8(const std::string& str);
  // 安全的字符串截取
  std::string SafeSubstr(const std::string& s, size_t pos, size_t len);
};

}  // namespace rime

#endif  // RIME_GEAR_CUSTOM_SWITCH_PROCESSOR_H_
