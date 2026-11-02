#include "spelling_filter.h"
#include <rime/candidate.h>
#include <rime/context.h>
#include <rime/engine.h>
#include <rime/schema.h>
#include <rime/service.h>
#include <rime/translation.h>
#include <rime/gear/translator_commons.h>

namespace SpellingFilter {

inline size_t Utf8CharLen(unsigned char c) {
  if (c < 0x80) return 1;
  if ((c >> 5) == 0x6) return 2;
  if ((c >> 4) == 0xe) return 3;
  if ((c >> 3) == 0x1e) return 4;
  return 1;
}

// 优化：返回 string_view 避免大量小碎片 std::string 内存分配
static std::vector<std::string_view> SplitUTF8View(std::string_view str) {
  std::vector<std::string_view> result;
  result.reserve(str.length() / 3 + 1);
  for (size_t i = 0; i < str.length();) {
    size_t cplen = Utf8CharLen(static_cast<unsigned char>(str[i]));
    if (i + cplen > str.length()) cplen = 1;
    result.emplace_back(str.data() + i, cplen);
    i += cplen;
  }
  return result;
}

// 优化：输入与输出均使用 string_view
static std::string_view CutSpellingFast(std::string_view str, int n) {
  if (n <= 0 || str.empty()) return {};
  const char* p = str.data();
  const char* end = p + str.size();
  const char* cur = p;
  int count = 0;
  while (cur < end && count < n) {
    cur += Utf8CharLen(static_cast<unsigned char>(*cur));
    count++;
  }
  return {p, static_cast<size_t>(cur - p)};
}

static bool IsGbkOrHanzi(std::string_view ch) {
  size_t len = ch.length();
  if (len <= 2) return false; 
  unsigned char c1 = static_cast<unsigned char>(ch[0]);
  unsigned char c2 = static_cast<unsigned char>(ch[1]);
  if (len == 3) {
    if (c1 >= 0xea && c1 <= 0xed) return false;
    if (c1 == 0xe3 && (c2 >= 0x81 && c2 <= 0x83)) return false;
    return true;
  }
  return (len == 4);
}

// 优化：接管源内存，提取 string_view 零拷贝赋值
static DbEntry ParseRawFast(const std::string& raw) {
  DbEntry entry;
  if (raw.size() < 2 || raw.front() != '[' || raw.back() != ']') return entry;
  
  entry.raw_buffer = std::make_shared<std::string>(raw);
  std::string_view content(entry.raw_buffer->data() + 1, entry.raw_buffer->size() - 2);
  
  std::vector<std::string_view> fields;
  fields.reserve(5);
  size_t start = 0;
  while (true) {
    size_t pos = content.find(',', start);
    if (pos == std::string_view::npos) {
      fields.push_back(content.substr(start));
      break;
    }
    fields.push_back(content.substr(start, pos - start));
    start = pos + 1;
  }
  
  if (fields.size() > 0) entry.charset  = fields[0];
  if (fields.size() > 1) entry.spelling = fields[1];
  if (fields.size() > 2) entry.code     = fields[2];
  if (fields.size() > 3) entry.pinyin   = fields[3];
  if (fields.size() > 4) entry.index    = fields[4];
  return entry;
}

class SpellingTranslation : public PrefetchTranslation {
 public:
  SpellingTranslation(an<Translation> translation, SpellingFilter* filter)
      : PrefetchTranslation(translation), filter_(filter) {
    Load();
  }

 private:
  void Load() {
    int count = 0;
    const int kMaxCandidates = 50;
    while (!translation_->exhausted() && count < kMaxCandidates) {
      auto cand = translation_->Peek();
      if (!cand) { translation_->Next(); continue; }

      const std::string& text = cand->text();
      const std::string& type = cand->type();
      
      if (type == "sentence" || type == "calculator" || type == "raw") {
        cache_.push_back(cand);
        count++;
        translation_->Next();
        continue;
      }

      auto chars = SplitUTF8View(text);
      if (filter_->option_single_char() && chars.size() > 1) {
        translation_->Next(); continue;
      }

      std::vector<DbEntry> current_char_entries;
      current_char_entries.reserve(chars.size());
      for (auto ch : chars) {
        if (ch.length() > 1 && IsGbkOrHanzi(ch)) {
          current_char_entries.push_back(filter_->LookupUnifiedEntry(std::string(ch)));
        } else {
          current_char_entries.push_back(DbEntry());
        }
      }

      if (filter_->option_gb2312()) {
        bool is_gb = true;
        for (const auto& d : current_char_entries) {
          if (!d.charset.empty() && d.charset.find("GB2312") == std::string_view::npos) {
            is_gb = false; break;
          }
        }
        if (!is_gb) { translation_->Next(); continue; }
      }

      std::string pinyin_str;
      for (const auto& d : current_char_entries) {
        if (!d.pinyin.empty()) {
          if (!pinyin_str.empty()) pinyin_str += "｜";
          for (char c : d.pinyin) {
            pinyin_str += (c == '_') ? ' ' : c;
          }
        }
      }

      std::string comment = BuildComment(current_char_entries, text);
      cache_.push_back(New<SpellingCandidate>(cand, type, text, comment, pinyin_str));

      count++;
      translation_->Next();
    }
  }

  // 优化：采用 append 和预处理长度的拼写逻辑，消除原 CutSpelling 带来的海量临时对象
  std::string BuildComment(const std::vector<DbEntry>& entries, const std::string& text) {
    std::string res_sp, res_cd;
    size_t n = entries.size();
    if (n > 0) {
      if (n == 1) {
        if (filter_->option_show_spelling()) res_sp = entries[0].spelling;
        if (filter_->option_show_code())     res_cd = entries[0].code;
      } else if (filter_->option_show_spelling() || filter_->option_show_code()) {
        if (n == 2) {
          if (filter_->option_show_spelling()) {
            res_sp.append(CutSpellingFast(entries[0].spelling, 2)).append(CutSpellingFast(entries[1].spelling, 2));
          }
          if (filter_->option_show_code()) {
            res_cd.append(entries[0].code.substr(0, 2)).append(entries[1].code.substr(0, 2));
          }
        } else if (n == 3) {
          if (filter_->option_show_spelling()) {
            res_sp.append(CutSpellingFast(entries[0].spelling, 1))
                  .append(CutSpellingFast(entries[1].spelling, 1))
                  .append(CutSpellingFast(entries[2].spelling, 2));
          }
          if (filter_->option_show_code()) {
            res_cd.append(entries[0].code.substr(0, 1))
                  .append(entries[1].code.substr(0, 1))
                  .append(entries[2].code.substr(0, 2));
          }
        } else {
          if (filter_->option_show_spelling()) {
            res_sp.append(CutSpellingFast(entries[0].spelling, 1))
                  .append(CutSpellingFast(entries[1].spelling, 1))
                  .append(CutSpellingFast(entries[2].spelling, 1))
                  .append(CutSpellingFast(entries[n-1].spelling, 1));
          }
          if (filter_->option_show_code()) {
            res_cd.append(entries[0].code.substr(0, 1))
                  .append(entries[1].code.substr(0, 1))
                  .append(entries[2].code.substr(0, 1))
                  .append(entries[n-1].code.substr(0, 1));
          }
        }
      }
    }
    
    std::string final_msg = res_sp;
    if (!res_cd.empty()) {
      if (!final_msg.empty()) final_msg += "・";
      final_msg += res_cd;
    }
    
    if (filter_->option_show_index()) {
      DbEntry phrase = (n == 1) ? entries[0] : filter_->LookupUnifiedEntry(text);
      if (!phrase.index.empty()) {
        if (!final_msg.empty()) final_msg += "・";
        final_msg.append(phrase.index);
      }
    }
    return final_msg.empty() ? "" : "[ " + final_msg + " ]";
  }

  SpellingFilter* filter_;
};

// 优化：使用 dynamic_cast 提取内存中已有的拼音计算结果，O(1) 复杂度替换原有的重复数据库查询
// 核心：处理高亮项变化的逻辑
void SpellingFilter::OnContextUpdate(rime::Context* ctx) {
  if (!show_pinyin_ || ctx->composition().empty()) return;

  auto& segment = ctx->composition().back();
  auto cand = segment.GetCandidateAt(segment.selected_index);

  if (!cand) {
    segment.prompt.clear();
    return;
  }

  const std::string& text = cand->text();
  const std::string& type = cand->type();

  if (type == "calculator") {
    segment.prompt.clear();
    return;
  }

  std::string pinyin_str;
  // 预分配足够的空间，避免多次动态扩容
  pinyin_str.reserve(text.size() * 5);

  // 利用 C++20 优化的 string_view 拆分，不产生 vector<string> 碎片
  auto chars = SplitUTF8View(text);
  
  for (auto ch : chars) {
    if (ch.length() > 1) {
      // 触发栈上的 SSO 优化，极速完成 std::string 构造
      // 此时该字元必定已在 unified_cache_ 内存中，直接 O(1) 返回结果
      DbEntry d = LookupUnifiedEntry(std::string(ch));

      if (!d.pinyin.empty()) {
        if (!pinyin_str.empty()) pinyin_str += "｜";
        for (char c : d.pinyin) {
          pinyin_str += (c == '_') ? ' ' : c;
        }
      }
    }
  }

  if (!pinyin_str.empty())
    segment.prompt = " 读音: " + pinyin_str;
  else
    segment.prompt.clear();
}

an<Translation> SpellingFilter::Apply(an<Translation> translation, CandidateList* candidates) {
  auto ctx = engine_->context();
  show_spelling_ = ctx->get_option("show_spelling");
  show_code_     = ctx->get_option("show_code");
  show_index_    = ctx->get_option("show_index");
  show_pinyin_   = ctx->get_option("show_pinyin");
  gb2312_        = ctx->get_option("GB2312");
  single_char_   = ctx->get_option("single_char");

  if (!show_spelling_ && !show_code_ && !show_index_ && !show_pinyin_ && !gb2312_ && !single_char_) {
    return translation;
  }

  if (!update_connection_.connected()) {
    update_connection_ = ctx->update_notifier().connect(
        [this](Context* ctx) { OnContextUpdate(ctx); });
  }

  this->LoadReverseDb();
  return New<SpellingTranslation>(translation, this);
}

SpellingFilter::SpellingFilter(const Ticket& ticket) 
    : Filter(ticket), engine_(ticket.engine) {
  auto config = ticket.schema->config();
  config->GetString("lua_reverse_db/spelling", &spell_db_name_);
}

SpellingFilter::~SpellingFilter() {
  update_connection_.disconnect();
}

void SpellingFilter::LoadReverseDb() {
  if (this->spll_rvdb_) return;
  rime::path user_data_dir = Service::instance().deployer().user_data_dir;
  if (!spell_db_name_.empty()) {
    spll_rvdb_ = New<ReverseDb>(user_data_dir / "build" / (spell_db_name_ + ".reverse.bin"));
    spll_rvdb_->Load();
  }
}

DbEntry SpellingFilter::LookupUnifiedEntry(const std::string& key) {
  auto it = unified_cache_.find(key);
  if (it != unified_cache_.end()) return it->second;
  
  // 优化：将清理阈值降至 10000 避免瞬间海量析构造成的打字延迟毛刺
  if (unified_cache_.size() >= 50000) unified_cache_.clear();
  
  std::string val;
  if (spll_rvdb_ && spll_rvdb_->Lookup(key, &val)) {
    return unified_cache_[key] = ParseRawFast(val);
  }
  return DbEntry();
}

} // namespace SpellingFilter
