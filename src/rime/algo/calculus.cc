//
// Copyright RIME Developers
// Distributed under the BSD License
//
// 2012-01-17 GONG Chen <chen.sst@gmail.com>
//

#include <utf8.h>
#include <rime/algo/calculus.h>
#include <rime/common.h>

namespace rime {

const double kAbbreviationPenalty = -0.6931471805599453;   // log(0.5)
const double kFuzzySpellingPenalty = -0.6931471805599453;  // log(0.5)
const double kCorrectionPenalty = -4.605170185988091;      // log(0.01)

Calculus::Calculus() {
  Register("xlit", &Transliteration::Parse);
  Register("xform", &Transformation::Parse);
  Register("erase", &Erasion::Parse);
  Register("derive", &Derivation::Parse);
  Register("fuzz", &Fuzzing::Parse);
  Register("abbrev", &Abbreviation::Parse);
}

void Calculus::Register(const string& token, Calculation::Factory* factory) {
  factories_[token] = factory;
}

Calculation* Calculus::Parse(const string& definition) {
  size_t sep = definition.find_first_not_of("zyxwvutsrqponmlkjihgfedcba");
  if (sep == string::npos)
    return nullptr;

  vector<string> args;

  // 替代 booost::split
  char delimiter = definition[sep];
  size_t start = 0;
  size_t end;
  while ((end = definition.find(delimiter, start)) != string::npos) {
    args.emplace_back(definition, start, end - start);
    start = end + 1;
  }
  args.emplace_back(definition, start);

  if (args.empty())
    return nullptr;
  auto it = factories_.find(args[0]);
  if (it == factories_.end())
    return nullptr;
  Calculation* result = (*it->second)(args);
  return result;
}

// Transliteration

Calculation* Transliteration::Parse(const vector<string>& args) {
  if (args.size() < 3)
    return nullptr;
  const string& left(args[1]);
  const string& right(args[2]);
  const char* pl = left.c_str();
  const char* pr = right.c_str();
  uint32_t cl, cr;
  map<uint32_t, uint32_t> char_map;
  while ((cl = utf8::unchecked::next(pl)), (cr = utf8::unchecked::next(pr)),
         cl && cr) {
    char_map[cl] = cr;
  }
  if (cl == 0 && cr == 0) {
    the<Transliteration> x(new Transliteration);
    x->char_map_.swap(char_map);
    return x.release();
  }
  return nullptr;
}

bool Transliteration::Apply(Spelling* spelling) {
  if (!spelling || spelling->str.empty())
    return false;
  bool modified = false;
  const char* p = spelling->str.c_str();
  const int buffer_len = 256;
  char buffer[buffer_len] = "";
  char* q = buffer;
  uint32_t c;
  while ((c = utf8::unchecked::next(p))) {
    if (q - buffer > buffer_len - 7) {  // insufficient space
      modified = false;
      break;
    }
    if (char_map_.contains(c)) {
      c = char_map_[c];
      modified = true;
    }
    q = utf8::unchecked::append(c, q);
  }
  if (modified) {
    *q = '\0';
    spelling->str.assign(buffer);
  }
  return modified;
}

// Transformation

Calculation* Transformation::Parse(const vector<string>& args) {
  if (args.size() < 3)
    return nullptr;
  const string& left(args[1]);
  const string& right(args[2]);
  if (left.empty())
    return nullptr;
  the<Transformation> x(new Transformation);
  x->pattern_ = std::regex(left, std::regex::optimize);
  x->replacement_.assign(right);
  return x.release();
}

bool Transformation::Apply(Spelling* spelling) {
  if (!spelling || spelling->str.empty())
    return false;
  // 使用 std::regex_replace
  string result = std::regex_replace(spelling->str, pattern_, replacement_);
  if (result == spelling->str)
    return false;
  spelling->str.swap(result);
  return true;
}

// Erasion

Calculation* Erasion::Parse(const vector<string>& args) {
  if (args.size() < 2)
    return nullptr;
  const string& pattern(args[1]);
  if (pattern.empty())
    return nullptr;
  the<Erasion> x(new Erasion);
  x->pattern_ = std::regex(pattern, std::regex::optimize);
  return x.release();
}

bool Erasion::Apply(Spelling* spelling) {
  if (!spelling || spelling->str.empty())
    return false;
  // 使用 std::regex_match
  if (!std::regex_match(spelling->str, pattern_))
    return false;
  spelling->str.clear();
  return true;
}

// Derivation

Calculation* Derivation::Parse(const vector<string>& args) {
  if (args.size() < 3)
    return nullptr;

  const string& left(args[1]);
  const string& right(args[2]);
  if (left.empty())
    return nullptr;

  if (args.size() > 3) {
    const string& tag = args[3];
    // 糾錯
    if (tag == "correction") {
      the<Correction> x(new Correction);
      x->pattern_ = std::regex(left, std::regex::optimize);
      x->replacement_.assign(right);
      return x.release();
    }
    // 簡拼
    if (tag == "abbrev") {
      the<Abbreviation> x(new Abbreviation);
      x->pattern_ = std::regex(left, std::regex::optimize);
      x->replacement_.assign(right);
      return x.release();
    }
    // 模糊音
    if (tag == "fuzz") {
      the<Fuzzing> x(new Fuzzing);
      x->pattern_ = std::regex(left, std::regex::optimize);
      x->replacement_.assign(right);
      return x.release();
    }
    // tag 無法識別, 作爲普通 derive 处理
  }

  the<Derivation> x(new Derivation);
  x->pattern_ = std::regex(left, std::regex::optimize);
  x->replacement_.assign(right);
  return x.release();
}

// Fuzzing

Calculation* Fuzzing::Parse(const vector<string>& args) {
  if (args.size() < 3)
    return nullptr;
  const string& left(args[1]);
  const string& right(args[2]);
  if (left.empty())
    return nullptr;
  the<Fuzzing> x(new Fuzzing);
  x->pattern_ = std::regex(left, std::regex::optimize);
  x->replacement_.assign(right);
  return x.release();
}

bool Fuzzing::Apply(Spelling* spelling) {
  bool result = Transformation::Apply(spelling);
  if (result) {
    spelling->properties.type = kFuzzySpelling;
    spelling->properties.credibility += kFuzzySpellingPenalty;
  }
  return result;
}

// Abbreviation

Calculation* Abbreviation::Parse(const vector<string>& args) {
  if (args.size() < 3)
    return nullptr;
  const string& left(args[1]);
  const string& right(args[2]);
  if (left.empty())
    return nullptr;
  the<Abbreviation> x(new Abbreviation);
  x->pattern_ = std::regex(left, std::regex::optimize);
  x->replacement_.assign(right);
  return x.release();
}

bool Abbreviation::Apply(Spelling* spelling) {
  bool result = Transformation::Apply(spelling);
  if (result) {
    spelling->properties.type = kAbbreviation;
    spelling->properties.credibility += kAbbreviationPenalty;
  }
  return result;
}

// Correction

bool Correction::Apply(Spelling* spelling) {
  bool result = Transformation::Apply(spelling);
  if (result) {
    spelling->properties.is_correction = true;
    spelling->properties.credibility += kCorrectionPenalty;
  }
  return result;
}

}  // namespace rime
