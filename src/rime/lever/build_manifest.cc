#include "build_manifest.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>
#include <string_view>

#include <rime/config.h>
#include <rime/algo/utilities.h>
#include <rime/dict/dict_compiler.h>
#include <rime/dict/dictionary.h>
#include <rime/dict/prism.h>
#include <rime/dict/table.h>
#include <rime/resource.h>
#include <rime/service.h>

namespace fs = std::filesystem;

namespace rime {

namespace {

constexpr const char* kManifestMagic = "RIME_BUILD_MANIFEST";
constexpr int kManifestVersion = 1;

struct FileStamp {
  path file_path;
  bool exists = false;
  uint64_t size = 0;
  int64_t mtime = 0;
  uint32_t checksum = 0;
};

string PathToUtf8(const path& value) {
  auto utf8 = value.lexically_normal().u8string();
  return string(reinterpret_cast<const char*>(utf8.data()), utf8.size());
}

path PathFromUtf8(const string& value) {
  return path(value);
}

int64_t FileTimeTicks(const path& file_path) {
  std::error_code ec;
  auto value = fs::last_write_time(file_path, ec);
  if (ec) {
    return 0;
  }
  return static_cast<int64_t>(value.time_since_epoch().count());
}

FileStamp CaptureStamp(const path& file_path, bool with_checksum) {
  FileStamp stamp;
  stamp.file_path = file_path;
  std::error_code ec;
  stamp.exists = fs::exists(file_path, ec) && !ec;
  if (!stamp.exists) {
    return stamp;
  }
  auto size = fs::file_size(file_path, ec);
  if (ec) {
    stamp.exists = false;
    return stamp;
  }
  stamp.size = size;
  stamp.mtime = FileTimeTicks(file_path);
  if (with_checksum) {
    stamp.checksum = Checksum(file_path);
  }
  return stamp;
}

void AddStamp(vector<FileStamp>* stamps,
              set<string>* paths,
              const path& file_path,
              bool with_checksum) {
  string key = PathToUtf8(file_path);
  if (!paths->insert(key).second) {
    return;
  }
  stamps->push_back(CaptureStamp(file_path, with_checksum));
}

void SortStamps(vector<FileStamp>* stamps) {
  std::sort(stamps->begin(), stamps->end(),
            [](const FileStamp& left, const FileStamp& right) {
              return PathToUtf8(left.file_path) < PathToUtf8(right.file_path);
            });
}

bool ParseInt(const string& text, int64_t* value) {
  try {
    size_t consumed = 0;
    long long parsed = std::stoll(text, &consumed);
    if (consumed != text.size()) {
      return false;
    }
    *value = parsed;
    return true;
  } catch (...) {
    return false;
  }
}

bool ParseUInt(const string& text, uint64_t* value) {
  try {
    size_t consumed = 0;
    unsigned long long parsed = std::stoull(text, &consumed);
    if (consumed != text.size()) {
      return false;
    }
    *value = parsed;
    return true;
  } catch (...) {
    return false;
  }
}

bool ReadField(std::istringstream* input, string* value) {
  return bool(std::getline(*input, *value, '\t'));
}

}  // namespace

struct BuildManifest::SchemaEntry {
  path schema_path;
  uint32_t prism_checksum = 0;
  vector<FileStamp> config_sources;
  vector<FileStamp> dict_sources;
  vector<FileStamp> artifacts;
};

BuildManifest::BuildManifest(Deployer* deployer)
    : manifest_path_(deployer ? deployer->staging_dir / "build_manifest.txt"
                              : path()) {}

void BuildManifest::Load() {
  schemas_.clear();
  loaded_ = false;
  dirty_ = false;
  if (manifest_path_.empty()) {
    return;
  }
  std::ifstream input(manifest_path_);
  if (!input) {
    return;
  }

  string line;
  bool header_ok = false;
  bool version_ok = false;
  while (std::getline(input, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (line.empty()) {
      continue;
    }
    std::istringstream fields(line);
    string tag;
    if (!ReadField(&fields, &tag)) {
      continue;
    }
    if (tag == kManifestMagic) {
      string version;
      header_ok = ReadField(&fields, &version) &&
                  version == std::to_string(kManifestVersion);
      continue;
    }
    if (tag == "RIME_VERSION") {
      string version;
      version_ok = ReadField(&fields, &version) && version == RIME_VERSION;
      continue;
    }
    if (!header_ok || !version_ok) {
      continue;
    }
    string schema_id;
    if (!ReadField(&fields, &schema_id)) {
      continue;
    }
    if (tag == "SCHEMA") {
      string prism_text;
      int64_t prism_checksum = 0;
      string source_path;
      if (!ReadField(&fields, &prism_text) ||
          !ParseInt(prism_text, &prism_checksum) ||
          !(fields >> std::quoted(source_path))) {
        continue;
      }
      auto& entry = schemas_[schema_id];
      if (!entry) {
        entry = New<SchemaEntry>();
      }
      entry->schema_path = PathFromUtf8(source_path);
      entry->prism_checksum = static_cast<uint32_t>(prism_checksum);
      continue;
    }
    if (tag != "CONFIG" && tag != "DICT" && tag != "ARTIFACT") {
      continue;
    }
    string exists_text;
    string size_text;
    string mtime_text;
    string checksum_text;
    string file_path_text;
    int64_t exists = 0;
    uint64_t size = 0;
    int64_t mtime = 0;
    uint64_t checksum = 0;
    if (!ReadField(&fields, &exists_text) ||
        !ReadField(&fields, &size_text) ||
        !ReadField(&fields, &mtime_text) ||
        !ReadField(&fields, &checksum_text) ||
        !ParseInt(exists_text, &exists) ||
        !ParseUInt(size_text, &size) ||
        !ParseInt(mtime_text, &mtime) ||
        !ParseUInt(checksum_text, &checksum) ||
        !(fields >> std::quoted(file_path_text))) {
      continue;
    }
    FileStamp stamp;
    stamp.file_path = PathFromUtf8(file_path_text);
    stamp.exists = exists != 0;
    stamp.size = size;
    stamp.mtime = mtime;
    stamp.checksum = static_cast<uint32_t>(checksum);
    auto found = schemas_.find(schema_id);
    if (found == schemas_.end() || !found->second) {
      continue;
    }
    if (tag == "CONFIG") {
      found->second->config_sources.push_back(std::move(stamp));
    } else if (tag == "DICT") {
      found->second->dict_sources.push_back(std::move(stamp));
    } else {
      found->second->artifacts.push_back(std::move(stamp));
    }
  }
  loaded_ = header_ok && version_ok;
}

bool BuildManifest::Save() {
  if (!dirty_ || manifest_path_.empty()) {
    return true;
  }
  std::error_code ec;
  fs::create_directories(manifest_path_.parent_path(), ec);
  if (ec) {
    LOG(ERROR) << "failed to create manifest directory: "
               << manifest_path_.parent_path();
    return false;
  }
  path temporary_path = manifest_path_;
  temporary_path += ".tmp";
  std::ofstream output(temporary_path, std::ios::binary | std::ios::trunc);
  if (!output) {
    LOG(ERROR) << "failed to write build manifest: " << temporary_path;
    return false;
  }
  output << kManifestMagic << '\t' << kManifestVersion << '\n';
  output << "RIME_VERSION\t" << RIME_VERSION << '\n';
  for (const auto& [schema_id, entry_ptr] : schemas_) {
    if (!entry_ptr) {
      continue;
    }
    const auto& entry = *entry_ptr;
    output << "SCHEMA\t" << schema_id << '\t' << entry.prism_checksum << '\t'
           << std::quoted(PathToUtf8(entry.schema_path)) << '\n';
    auto write_stamps = [&](const char* tag,
                            const vector<FileStamp>& stamps) {
      for (const auto& stamp : stamps) {
        output << tag << '\t' << schema_id << '\t' << (stamp.exists ? 1 : 0)
               << '\t' << stamp.size << '\t' << stamp.mtime << '\t'
               << stamp.checksum << '\t'
               << std::quoted(PathToUtf8(stamp.file_path)) << '\n';
      }
    };
    write_stamps("CONFIG", entry.config_sources);
    write_stamps("DICT", entry.dict_sources);
    write_stamps("ARTIFACT", entry.artifacts);
  }
  output.close();
  if (!output) {
    LOG(ERROR) << "failed to finish build manifest: " << temporary_path;
    return false;
  }
  fs::remove(manifest_path_, ec);
  ec.clear();
  fs::rename(temporary_path, manifest_path_, ec);
  if (ec) {
    LOG(ERROR) << "failed to replace build manifest: " << manifest_path_;
    return false;
  }
  dirty_ = false;
  return true;
}

bool BuildManifest::IsSchemaUpToDate(const string& schema_id,
                                     const path& schema_path) {
  if (!loaded_) {
    return false;
  }
  auto found = schemas_.find(schema_id);
  if (found == schemas_.end()) {
    return false;
  }
  if (!found->second) {
    return false;
  }
  auto& entry = *found->second;
  if (PathToUtf8(entry.schema_path) != PathToUtf8(schema_path) ||
      entry.config_sources.empty() || entry.artifacts.empty()) {
    return false;
  }

  auto validate_source = [this](FileStamp* stamp, bool always_hash) {
    std::error_code ec;
    bool exists = fs::exists(stamp->file_path, ec) && !ec;
    if (exists != stamp->exists) {
      return false;
    }
    if (!exists) {
      return true;
    }
    auto size = fs::file_size(stamp->file_path, ec);
    if (ec || size != stamp->size) {
      return false;
    }
    int64_t mtime = FileTimeTicks(stamp->file_path);
    if (!always_hash && mtime == stamp->mtime) {
      return true;
    }
    uint32_t checksum = Checksum(stamp->file_path);
    if (checksum != stamp->checksum) {
      return false;
    }
    if (mtime != stamp->mtime || stamp->checksum != checksum) {
      stamp->mtime = mtime;
      stamp->checksum = checksum;
      dirty_ = true;
    }
    return true;
  };

  for (auto& stamp : entry.config_sources) {
    if (!validate_source(&stamp, true)) {
      return false;
    }
  }
  for (auto& stamp : entry.dict_sources) {
    if (!validate_source(&stamp, false)) {
      return false;
    }
  }
  for (const auto& artifact : entry.artifacts) {
    std::error_code ec;
    if (!fs::exists(artifact.file_path, ec) || ec) {
      return false;
    }
    auto size = fs::file_size(artifact.file_path, ec);
    if (ec || !artifact.exists || size != artifact.size ||
        FileTimeTicks(artifact.file_path) != artifact.mtime) {
      return false;
    }
  }
  return true;
}

void BuildManifest::RecordSchema(const string& schema_id,
                                 const path& schema_path,
                                 const path& compiled_schema_path,
                                 Dictionary* dictionary,
                                 const DictCompiler* compiler) {
  Config compiled;
  if (!compiled.LoadFromFile(compiled_schema_path)) {
    LOG(WARNING) << "cannot record manifest for schema '" << schema_id
                 << "': compiled schema is unreadable.";
    schemas_.erase(schema_id);
    return;
  }
  auto timestamps = compiled.GetMap("__build_info/timestamps");
  if (!timestamps) {
    // Without the config dependency list, a safe fast-path is impossible.
    LOG(WARNING) << "manifest disabled for schema '" << schema_id
                 << "': build timestamps are unavailable.";
    schemas_.erase(schema_id);
    return;
  }

  auto entry = New<SchemaEntry>();
  entry->schema_path = schema_path;
  entry->prism_checksum =
      DictCompiler::ComputePrismConfigChecksum(compiled_schema_path);

  the<ResourceResolver> config_resolver(
      Service::instance().CreateResourceResolver(
          {"config_source_file", "", ".yaml"}));
  set<string> config_paths;
  for (const auto& timestamp : *timestamps) {
    AddStamp(&entry->config_sources, &config_paths,
             config_resolver->ResolvePath(timestamp.first), true);
  }
  AddStamp(&entry->config_sources, &config_paths,
           config_resolver->ResolvePath(schema_id + ".custom"), true);
  AddStamp(&entry->config_sources, &config_paths, schema_path, true);
  if (entry->config_sources.empty()) {
    schemas_.erase(schema_id);
    return;
  }

  set<string> artifact_paths;
  AddStamp(&entry->artifacts, &artifact_paths, compiled_schema_path, false);

  if (dictionary && compiler) {
    the<ResourceResolver> source_resolver(
        Service::instance().CreateResourceResolver({"source_file", "", ""}));
    set<string> dict_paths;
    AddStamp(&entry->dict_sources, &dict_paths,
             source_resolver->ResolvePath(dictionary->name() + ".dict.yaml"),
             true);
    for (const auto& source : compiler->source_files()) {
      AddStamp(&entry->dict_sources, &dict_paths, source, true);
    }
    for (const auto& table : dictionary->tables()) {
      if (table) {
        AddStamp(&entry->artifacts, &artifact_paths, table->file_path(), false);
      }
    }
    if (dictionary->prism()) {
      AddStamp(&entry->artifacts, &artifact_paths,
               dictionary->prism()->file_path(), false);
    }
    the<ResourceResolver> reverse_resolver(
        Service::instance().CreateDeployedResourceResolver(
            {"find_reverse_db", "", ".reverse.bin"}));
    AddStamp(&entry->artifacts, &artifact_paths,
             reverse_resolver->ResolvePath(dictionary->name()), false);
  }

  SortStamps(&entry->config_sources);
  SortStamps(&entry->dict_sources);
  SortStamps(&entry->artifacts);
  schemas_[schema_id] = std::move(entry);
  dirty_ = true;
}

}  // namespace rime
