#ifndef SPELLING_FILTER_H
#define SPELLING_FILTER_H

#include <rime/filter.h>
#include <rime/candidate.h>
#include <rime/context.h>        // 必须：识别 Context
#include <rime/segmentation.h>   // 必须：识别 Segment
#include <rime/dict/reverse_lookup_dictionary.h>
#include <unordered_map>
#include <string>
#include <vector>

namespace SpellingFilter {

using namespace rime;

// 数据条目结构
struct DbEntry {
  std::string charset;
  std::string spelling;
  std::string code;
  std::string pinyin;
  std::string index;

  bool empty() const { return spelling.empty() && code.empty(); }
};

// 自定义候选词类：用于缓存读音，方便动态显示
class SpellingCandidate : public ShadowCandidate {
 public:
  SpellingCandidate(const an<Candidate>& original, 
                    const std::string& type, 
                    const std::string& text, 
                    const std::string& comment, 
                    const std::string& pinyin)
      : ShadowCandidate(original, type, text, comment), pinyin_(pinyin) {}

  const std::string& pinyin() const { return pinyin_; }

 private:
  std::string pinyin_;
};

class SpellingFilter : public Filter {
 public:
  explicit SpellingFilter(const Ticket& ticket);

  virtual an<Translation> Apply(an<Translation> translation,
                                 CandidateList* candidates) override;

  DbEntry LookupUnifiedEntry(const std::string& key);
  
  Engine* engine() const { return engine_; }

  // 选项获取接口
  bool option_show_spelling() const { return show_spelling_; }
  bool option_show_code()     const { return show_code_; }
  bool option_show_index()    const { return show_index_; }
  bool option_show_pinyin()   const { return show_pinyin_; }
  bool option_gb2312()        const { return gb2312_; }
  bool option_single_char()   const { return single_char_; }

  // 核心：处理高亮变化的监听回调
  void OnContextUpdate(rime::Context* ctx);

 private:
  void LoadReverseDb();

  Engine* engine_;
  std::string spell_db_name_;
  an<ReverseDb> spll_rvdb_;
  std::unordered_map<std::string, DbEntry> unified_cache_;

  // 监听连接对象
  connection update_connection_;

  bool show_spelling_ = false;
  bool show_code_     = false;
  bool show_index_    = false;
  bool show_pinyin_   = false;
  bool gb2312_        = false;
  bool single_char_   = false;
};

} // namespace SpellingFilter

#endif // SPELLING_FILTER_H