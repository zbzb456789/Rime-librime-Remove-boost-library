#ifndef RIME_BUILD_MANIFEST_H_
#define RIME_BUILD_MANIFEST_H_

#include <rime/common.h>

namespace rime {

class Deployer;
class Dictionary;
class DictCompiler;

// Persistent incremental-build state stored in the staging directory.
// It records only source/build metadata; dictionary artifacts remain the
// authoritative outputs consumed at runtime.
class BuildManifest {
 public:
  explicit BuildManifest(Deployer* deployer);

  void Load();
  bool Save();

  bool IsSchemaUpToDate(const string& schema_id, const path& schema_path);

  void RecordSchema(const string& schema_id,
                    const path& schema_path,
                    const path& compiled_schema_path,
                    Dictionary* dictionary,
                    const DictCompiler* compiler);

 private:
  struct SchemaEntry;

  path manifest_path_;
  map<string, an<SchemaEntry>> schemas_;
  bool loaded_ = false;
  bool dirty_ = false;
};

}  // namespace rime

#endif  // RIME_BUILD_MANIFEST_H_
