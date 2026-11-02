#ifndef SPELLING_FILTER_H
#define SPELLING_FILTER_H

#include <rime/filter.h>
#include <rime/candidate.h>
#include <rime/context.h>
#include <rime/segmentation.h>
#include <rime/dict/reverse_lookup_dictionary.h>
#include <unordered_map>
#include <string>
#include <string_view>
#include <vector>
#include <memory>

namespace SpellingFilter {

using namespace rime;

// 优化：采用 shared_ptr 保留原生内存，各字段使用 string_view 零拷贝引用
struct DbEntry {
  std::shared_ptr<std::string> raw_buffer; 
  std::string_view charset;
  std::string_view spelling;
  std::string_view code;
  std::string_view pinyin;
  std::string_view index;

  bool empty() const { return spelling.empty() && code.empty(); }
};

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
  virtual ~SpellingFilter();

  virtual an<Translation> Apply(an<Translation> translation,
                                 CandidateList* candidates) override;

  DbEntry LookupUnifiedEntry(const std::string& key);
  
  Engine* engine() const { return engine_; }

  bool option_show_spelling() const { return show_spelling_; }
  bool option_show_code()     const { return show_code_; }
  bool option_show_index()    const { return show_index_; }
  bool option_show_pinyin()   const { return show_pinyin_; }
  bool option_gb2312()        const { return gb2312_; }
  bool option_single_char()   const { return single_char_; }

  void OnContextUpdate(rime::Context* ctx);

 private:
  void LoadReverseDb();

  Engine* engine_;
  std::string spell_db_name_;
  an<ReverseDb> spll_rvdb_;
  std::unordered_map<std::string, DbEntry> unified_cache_;

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
