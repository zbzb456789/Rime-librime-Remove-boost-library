#include "spelling_filter.h"
#include <rime/candidate.h>
#include <rime/context.h>
#include <rime/engine.h>
#include <rime/schema.h>
#include <rime/service.h>
#include <rime/translation.h>
#include <rime/gear/translator_commons.h>
#include <string_view>

namespace SpellingFilter {

// UTF-8 分割辅助工具
static std::vector<std::string> SplitUTF8(const std::string& str) {
  std::vector<std::string> result;
  for (size_t i = 0; i < str.length();) {
    size_t cplen = 1;
    unsigned char c = (unsigned char)str[i];
    if (c >= 0xf0) cplen = 4;
    else if (c >= 0xe0) cplen = 3;
    else if (c >= 0xc0) cplen = 2;
    if (i + cplen > str.length()) cplen = 1;
    result.emplace_back(str.substr(i, cplen));
    i += cplen;
  }
  return result;
}

// ================= UTF8 快速工具 =================

inline size_t Utf8CharLen(unsigned char c) {
  if (c < 0x80) return 1;
  if ((c >> 5) == 0x6) return 2;
  if ((c >> 4) == 0xe) return 3;
  if ((c >> 3) == 0x1e) return 4;
  return 1;
}

// 截取前 n 个 UTF8 字符（零 vector）
static std::string CutSpellingFast(const std::string& str, int n) {
  if (n <= 0 || str.empty()) return "";

  const char* p = str.data();
  const char* end = p + str.size();
  const char* cur = p;

  int count = 0;
  while (cur < end && count < n) {
    size_t len = Utf8CharLen((unsigned char)*cur);
    cur += len;
    count++;
  }
  return std::string(p, cur - p);
}


// 语种过滤（跳过日韩文）
static bool IsGbkOrHanzi(const std::string& ch) {
  size_t len = ch.length();
  if (len <= 2) return false; 
  unsigned char c1 = (unsigned char)ch[0];
  unsigned char c2 = (unsigned char)ch[1];
  if (len == 3) {
    if (c1 >= 0xea && c1 <= 0xed) return false;
    if (c1 == 0xe3 && (c2 >= 0x81 && c2 <= 0x83)) return false;
    return true;
  }
  return (len == 4);
}

static std::string CutSpelling(const std::string& spell, int n) {
  if (spell.empty()) return "";
  auto chars = SplitUTF8(spell);
  std::string out;
  for (int i = 0; i < n && i < (int)chars.size(); ++i)
    out += chars[i];
  return out;
}

// 解析数据库原始字符串
static DbEntry ParseRawFast(const std::string& raw) {
  DbEntry entry;
  if (raw.size() < 2 || raw.front() != '[' || raw.back() != ']') return entry;
  std::string_view content(raw.data() + 1, raw.size() - 2);
  std::vector<std::string_view> fields;
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
  if (fields.size() > 0) entry.charset  = std::string(fields[0]);
  if (fields.size() > 1) entry.spelling = std::string(fields[1]);
  if (fields.size() > 2) entry.code     = std::string(fields[2]);
  if (fields.size() > 3) entry.pinyin   = std::string(fields[3]);
  if (fields.size() > 4) entry.index    = std::string(fields[4]);
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
    const int kMaxCandidates = 120; // 防止卡顿

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

      std::vector<std::string> chars = SplitUTF8(text);
      if (filter_->option_single_char() && chars.size() > 1) {
        translation_->Next(); continue;
      }

      // 准备字符数据
      std::vector<DbEntry> current_char_entries;
      for (const auto& ch : chars) {
        if (ch.length() > 1 && IsGbkOrHanzi(ch)) {
          current_char_entries.push_back(filter_->LookupUnifiedEntry(ch));
        } else {
          current_char_entries.push_back(DbEntry());
        }
      }

      // GB2312 过滤逻辑
      if (filter_->option_gb2312()) {
        bool is_gb = true;
        for (const auto& d : current_char_entries) {
          if (!d.charset.empty() && d.charset.find("GB2312") == std::string::npos) {
            is_gb = false; break;
          }
        }
        if (!is_gb) { translation_->Next(); continue; }
      }

      // 计算读音字符串用于缓存
      std::string pinyin_str;
      for (const auto& d : current_char_entries) {
        if (!d.pinyin.empty()) {
          if (!pinyin_str.empty()) pinyin_str += "｜";
          std::string s = d.pinyin;
          for (char& c : s) if (c == '_') c = ' ';
          pinyin_str += s;
        }
      }

      // 计算注释 (Comment)
      std::string comment = BuildComment(current_char_entries, text);
      
      // 使用 SpellingCandidate 封装，存储读音数据
      cache_.push_back(New<SpellingCandidate>(cand, type, text, comment, pinyin_str));

      count++;
      translation_->Next();
    }
  }

  std::string BuildComment(const std::vector<DbEntry>& entries, const std::string& text) {
    std::string res_sp, res_cd;
    size_t n = entries.size();
    if (n > 0) {
      if (n == 1) {
        if (filter_->option_show_spelling()) res_sp = entries[0].spelling;
        if (filter_->option_show_code())     res_cd = entries[0].code;
      } else if (filter_->option_show_spelling() || filter_->option_show_code()) {
        if (n == 2) {
          if (filter_->option_show_spelling()) res_sp = CutSpellingFast(entries[0].spelling, 2) + CutSpelling(entries[1].spelling, 2);
          if (filter_->option_show_code())     res_cd = entries[0].code.substr(0, 2) + entries[1].code.substr(0, 2);
        } else if (n == 3) {
          if (filter_->option_show_spelling()) res_sp = CutSpellingFast(entries[0].spelling, 1) + CutSpelling(entries[1].spelling, 1) + CutSpellingFast(entries[2].spelling, 2);
          if (filter_->option_show_code())     res_cd = entries[0].code.substr(0, 1) + entries[1].code.substr(0, 1) + entries[2].code.substr(0, 2);
        } else {
          if (filter_->option_show_spelling()) res_sp = CutSpellingFast(entries[0].spelling, 1) + CutSpelling(entries[1].spelling, 1) + CutSpellingFast(entries[2].spelling, 1) + CutSpellingFast(entries[n-1].spelling, 1);
          if (filter_->option_show_code())     res_cd = entries[0].code.substr(0, 1) + entries[1].code.substr(0, 1) + entries[2].code.substr(0, 1) + entries[n-1].code.substr(0, 1);
        }
      }
    }
    std::string final_msg;
    if (!res_sp.empty()) final_msg = res_sp;
    if (!res_cd.empty()) {
      if (!final_msg.empty()) final_msg += "・";
      final_msg += res_cd;
    }
    if (filter_->option_show_index()) {
      DbEntry phrase = (n == 1) ? entries[0] : filter_->LookupUnifiedEntry(text);
      if (!phrase.index.empty()) {
        if (!final_msg.empty()) final_msg += "・";
        final_msg += phrase.index;
      }
    }
    return final_msg.empty() ? "" : "[ " + final_msg + " ]";
  }

  SpellingFilter* filter_;
};

// 核心：处理高亮项变化的逻辑
void SpellingFilter::OnContextUpdate(rime::Context* ctx) {
  if (!show_pinyin_ || ctx->composition().empty())
    return;

  auto& segment = ctx->composition().back();
  auto cand = segment.GetCandidateAt(segment.selected_index);

  if (!cand) {
    segment.prompt.clear();
    return;
  }

  const std::string& text = cand->text();
  const std::string& type = cand->type();
  const char* p = text.data();
  const char* end = p + text.size();

  std::string pinyin_str;
  pinyin_str.reserve(text.size() * 2);
  if(type != "calculator"){
	  while (p < end) {
		size_t len = Utf8CharLen((unsigned char)*p);

		// 只处理多字节字符
		if (len > 1) {
		  std::string key(p, len);
		  DbEntry d = LookupUnifiedEntry(key);

		  if (!d.pinyin.empty()) {
			if (!pinyin_str.empty())
			  pinyin_str += "｜";

			for (char c : d.pinyin)
			  pinyin_str += (c == '_') ? ' ' : c;
		  }
		}

		p += len;
	  }

	  if (!pinyin_str.empty())
		segment.prompt = " 读音: " + pinyin_str;
	  else
      segment.prompt.clear();
  }
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

  // 绑定上下文更新信号，实现动态刷新
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
  if (unified_cache_.size() >= 200000) unified_cache_.clear();
  std::string val;
  if (spll_rvdb_ && spll_rvdb_->Lookup(key, &val)) {
    return unified_cache_[key] = ParseRawFast(val);
  }
  return DbEntry();
}

} // namespace SpellingFilter