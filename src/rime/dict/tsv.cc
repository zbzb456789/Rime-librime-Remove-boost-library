//
// Copyright RIME Developers
// Distributed under the BSD License
//
// 2013-04-14 GONG Chen <chen.sst@gmail.com>
//
#include <fstream>
#include <rime/common.h>
#include <rime/dict/db_utils.h>
#include <rime/dict/tsv.h>

namespace rime {

int TsvReader::operator()(Sink* sink) {
  if (!sink)
    return 0;
  LOG(INFO) << "reading tsv file: " << file_path_;
  std::ifstream fin(file_path_.c_str());
  string line, key, value;
  Tsv row;
  int line_no = 0;
  int num_entries = 0;
  bool enable_comment = true;
  
  auto split_tsv = [](const string& s, Tsv& result) {
    result.clear();
    for (size_t start = 0, end = 0; end != string::npos; start = end + 1) {
      end = s.find('\t', start);
      result.emplace_back(s, start, end - start);
    }
  };

  while (getline(fin, line)) {
    ++line_no;
    line.erase(line.find_last_not_of(" \n\r\t\f\v") + 1);
    // skip empty lines and comments
    if (line.empty())
      continue;
    if (enable_comment && line[0] == '#') {
      if (line.starts_with("#@")) {
        // metadata
        line.erase(0, 2);
        split_tsv(line, row);
        if (row.size() != 2 || !sink->MetaPut(row[0], row[1])) {
          LOG(WARNING) << "invalid metadata at line " << line_no
                       << " in file: " << file_path_ << ".";
        }
      } else if (line == "# no comment") {
        // a "# no comment" line disables further comments
        enable_comment = false;
      }
      continue;
    }
    // read a tsv entry
    split_tsv(line, row);
    if (!parser_(row, &key, &value) || !sink->Put(key, value)) {
      LOG(WARNING) << "invalid entry at line " << line_no
                   << " in file: " << file_path_ << ".";
      continue;
    }
    ++num_entries;
  }
  fin.close();
  return num_entries;
}

int TsvWriter::operator()(Source* source) {
  if (!source)
    return 0;
  LOG(INFO) << "writing tsv file: " << file_path_;
  std::ofstream fout(file_path_.c_str());
  if (!file_description.empty()) {
    fout << "# " << file_description << std::endl;
  }
  string key, value;
  while (source->MetaGet(&key, &value)) {
    fout << "#@" << key << '\t' << value << std::endl;
  }
  Tsv row;
  int num_entries = 0;
  while (source->Get(&key, &value)) {
    row.clear();
    if (formatter_(key, value, &row) && !row.empty()) {
      for (auto it = row.cbegin(); it != row.cend(); ++it) {
        if (it != row.cbegin())
          fout << '\t';
        fout << *it;
      }
      fout << std::endl;
      ++num_entries;
    }
  }
  fout.close();
  return num_entries;
}

}  // namespace rime
