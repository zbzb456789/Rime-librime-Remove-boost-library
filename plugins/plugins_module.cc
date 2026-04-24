//
// Copyright RIME Developers
// Distributed under the BSD License
//

#include <algorithm>
#include <filesystem>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <rime/build_config.h>
#include <rime/common.h>
#include <rime/component.h>
#include <rime/module.h>
#include <rime/registry.h>
#include <rime_api.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace fs = std::filesystem;

namespace rime {

#ifdef _WIN32
inline constexpr std::string_view kSharedLibrarySuffix = ".dll";
#elif defined(__APPLE__)
inline constexpr std::string_view kSharedLibrarySuffix = ".dylib";
#else
inline constexpr std::string_view kSharedLibrarySuffix = ".so";
#endif

class SharedLibrary {
 public:
  explicit SharedLibrary(const path& p) {
#ifdef _WIN32
    handle_ = LoadLibraryW(p.wstring().c_str());
    if (!handle_) throw std::runtime_error("LoadLibrary failed");
#else
    handle_ = dlopen(p.c_str(), RTLD_LAZY | RTLD_LOCAL);
    if (!handle_) {
      const char* err = dlerror();
      throw std::runtime_error(err ? err : "dlopen failed");
    }
#endif
  }

  ~SharedLibrary() {
    if (handle_) {
#ifdef _WIN32
      FreeLibrary(static_cast<HMODULE>(handle_));
#else
      dlclose(handle_);
#endif
    }
  }

  SharedLibrary(const SharedLibrary&) = delete;
  SharedLibrary& operator=(const SharedLibrary&) = delete;

  SharedLibrary(SharedLibrary&& other) noexcept : handle_(other.handle_) {
    other.handle_ = nullptr;
  }

  SharedLibrary& operator=(SharedLibrary&& other) noexcept {
    if (this != &other) {
      if (handle_) {
#ifdef _WIN32
        FreeLibrary(static_cast<HMODULE>(handle_));
#else
        dlclose(handle_);
#endif
      }
      handle_ = other.handle_;
      other.handle_ = nullptr;
    }
    return *this;
  }

 private:
  void* handle_ = nullptr;
};

class PluginManager {
 public:
  void LoadPlugins(path plugins_dir);

  static string plugin_name_of(path plugin_file);

  static PluginManager& instance();

 private:
  PluginManager() = default;

  map<string, SharedLibrary> plugin_libs_;
};

void PluginManager::LoadPlugins(path plugins_dir) {
  ModuleManager& mm = ModuleManager::instance();
  if (!fs::is_directory(plugins_dir)) {
    return;
  }
  LOG(INFO) << "loading plugins from " << plugins_dir;
  for (fs::directory_iterator iter(plugins_dir), end; iter != end; ++iter) {
    path plugin_file = iter->path();
    if (plugin_file.extension().string() == kSharedLibrarySuffix) {
      fs::file_status plugin_file_status = fs::status(plugin_file);
      if (fs::is_regular_file(plugin_file_status)) {
        DLOG(INFO) << "found plugin: " << plugin_file;
        string plugin_name = plugin_name_of(plugin_file);
        if (plugin_libs_.find(plugin_name) == plugin_libs_.end()) {
          LOG(INFO) << "loading plugin '" << plugin_name << "' from "
                    << plugin_file;
          try {
            plugin_libs_.emplace(plugin_name, SharedLibrary(plugin_file));
          } catch (const std::exception& ex) {
            LOG(ERROR) << "error loading plugin " << plugin_name << ": "
                       << ex.what();
            continue;
          }
        }
        if (RimeModule* module = mm.Find(plugin_name)) {
          mm.LoadModule(module);
          LOG(INFO) << "loaded plugin: " << plugin_name;
        } else {
          LOG(WARNING) << "module '" << plugin_name
                       << "' is not provided by plugin library " << plugin_file;
        }
      }
    }
  }
}

string PluginManager::plugin_name_of(path plugin_file) {
  string name = plugin_file.stem().string();
  // remove prefix "(lib)rime-"
  if (name.starts_with("librime-")) {
    name.erase(0, 8);
  } else if (name.starts_with("rime-")) {
    name.erase(0, 5);
  }
  // replace dash with underscore, for the plugin name is part of the module
  // initializer function name.
  std::replace(name.begin(), name.end(), '-', '_');
  return name;
}

PluginManager& PluginManager::instance() {
  static the<PluginManager> s_instance;
  if (!s_instance) {
    s_instance.reset(new PluginManager);
  }
  return *s_instance;
}

}  // namespace rime

#ifdef _WIN32
// TODO: implement this when ready to support DLL plugins on Windows.
inline static rime::path current_module_path() {
  return rime::path{};
}
#else
inline static rime::path symbol_location(const void* symbol) {
  Dl_info info;
  // Some of the libc headers miss `const` in `dladdr(const void*, Dl_info*)`
  const int res = dladdr(const_cast<void*>(symbol), &info);
  if (res) {
    return rime::path{info.dli_fname};
  } else {
    return rime::path{};
  }
}

inline static rime::path current_module_path() {
  void rime_require_module_plugins();
  return symbol_location(
      reinterpret_cast<const void*>(&rime_require_module_plugins));
}
#endif

static void rime_plugins_initialize() {
  rime::PluginManager::instance().LoadPlugins(
      current_module_path().remove_filename() / RIME_PLUGINS_DIR);
}

static void rime_plugins_finalize() {}

RIME_REGISTER_MODULE(plugins)