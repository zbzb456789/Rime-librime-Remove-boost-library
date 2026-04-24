//
// Copyright RIME Developers
// Distributed under the BSD License
//
// register components
//
// 2011-06-30 GONG Chen <chen.sst@gmail.com>
//
#include <fstream>
#include <filesystem>
#include <cstring>
#include <rime/dict/mapped_file.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace rime {

class MappedFileImpl {
 public:
  enum OpenMode {
    kOpenReadOnly,
    kOpenReadWrite,
  };

  MappedFileImpl(const path& file_path, OpenMode mode);
  ~MappedFileImpl();
  bool Flush();
  void* get_address() const { return address_; }
  size_t get_size() const { return size_; }

 private:
  void* address_ = nullptr;
  size_t size_ = 0;
#if defined(_WIN32)
  HANDLE file_ = INVALID_HANDLE_VALUE;
  HANDLE mapping_ = NULL;
#else
  int fd_ = -1;
#endif
};

#if defined(_WIN32)
MappedFileImpl::MappedFileImpl(const path& file_path, OpenMode mode) {
  DWORD desired_access = (mode == kOpenReadOnly) ? GENERIC_READ : (GENERIC_READ | GENERIC_WRITE);
  DWORD share_mode = FILE_SHARE_READ | FILE_SHARE_WRITE;
  DWORD creation_disposition = OPEN_EXISTING;
  DWORD flProtect = (mode == kOpenReadOnly) ? PAGE_READONLY : PAGE_READWRITE;
  DWORD map_access = (mode == kOpenReadOnly) ? FILE_MAP_READ : FILE_MAP_WRITE;

  file_ = CreateFileW(file_path.c_str(), desired_access, share_mode, NULL, creation_disposition, FILE_ATTRIBUTE_NORMAL, NULL);
  if (file_ != INVALID_HANDLE_VALUE) {
    LARGE_INTEGER size;
    if (GetFileSizeEx(file_, &size)) {
      size_ = size.QuadPart;
      if (size_ > 0) {
        mapping_ = CreateFileMappingW(file_, NULL, flProtect, 0, 0, NULL);
        if (mapping_ != NULL) {
          address_ = MapViewOfFile(mapping_, map_access, 0, 0, 0);
        }
      }
    }
  }
}

MappedFileImpl::~MappedFileImpl() {
  if (address_) UnmapViewOfFile(address_);
  if (mapping_) CloseHandle(mapping_);
  if (file_ != INVALID_HANDLE_VALUE) CloseHandle(file_);
}

bool MappedFileImpl::Flush() {
  if (address_) return FlushViewOfFile(address_, 0) != 0;
  return false;
}
#else
MappedFileImpl::MappedFileImpl(const path& file_path, OpenMode mode) {
  int flags = (mode == kOpenReadOnly) ? O_RDONLY : O_RDWR;
  fd_ = open(file_path.c_str(), flags);
  if (fd_ != -1) {
    struct stat st;
    if (fstat(fd_, &st) == 0) {
      size_ = st.st_size;
      if (size_ > 0) {
        int prot = (mode == kOpenReadOnly) ? PROT_READ : (PROT_READ | PROT_WRITE);
        address_ = mmap(nullptr, size_, prot, MAP_SHARED, fd_, 0);
        if (address_ == MAP_FAILED) {
          address_ = nullptr;
        }
      }
    }
  }
}

MappedFileImpl::~MappedFileImpl() {
  if (address_) munmap(address_, size_);
  if (fd_ != -1) close(fd_);
}

bool MappedFileImpl::Flush() {
  if (address_) return msync(address_, size_, MS_SYNC) == 0;
  return false;
}
#endif

MappedFile::MappedFile(const path& file_path) : file_path_(file_path) {}

MappedFile::~MappedFile() {
  if (file_) {
    file_.reset();
  }
}

bool MappedFile::Create(size_t capacity) {
  if (Exists()) {
    LOG(INFO) << "overwriting file '" << file_path_ << "'.";
    Resize(capacity);
  } else {
    LOG(INFO) << "creating file '" << file_path_ << "'.";
    std::filebuf fbuf;
    fbuf.open(file_path_.c_str(), std::ios_base::in | std::ios_base::out |
                                      std::ios_base::trunc |
                                      std::ios_base::binary);
    if (capacity > 0) {
      fbuf.pubseekoff(capacity - 1, std::ios_base::beg);
      fbuf.sputc(0);
    }
    fbuf.close();
  }
  LOG(INFO) << "opening file for read/write access.";
  file_.reset(new MappedFileImpl(file_path_, MappedFileImpl::kOpenReadWrite));
  size_ = 0;
  return bool(file_);
}

bool MappedFile::OpenReadOnly() {
  if (!Exists()) {
    LOG(ERROR) << "attempt to open non-existent file '" << file_path_ << "'.";
    return false;
  }
  file_.reset(new MappedFileImpl(file_path_, MappedFileImpl::kOpenReadOnly));
  size_ = file_->get_size();
  return bool(file_);
}

bool MappedFile::OpenReadWrite() {
  if (!Exists()) {
    LOG(ERROR) << "attempt to open non-existent file '" << file_path_ << "'.";
    return false;
  }
  file_.reset(new MappedFileImpl(file_path_, MappedFileImpl::kOpenReadWrite));
  size_ = 0;
  return bool(file_);
}

void MappedFile::Close() {
  if (file_) {
    file_.reset();
    size_ = 0;
  }
}

bool MappedFile::Exists() const {
  return std::filesystem::exists(file_path_);
}

bool MappedFile::IsOpen() const {
  return bool(file_);
}

bool MappedFile::Flush() {
  if (!file_)
    return false;
  return file_->Flush();
}

bool MappedFile::ShrinkToFit() {
  LOG(INFO) << "shrinking file to fit data size. capacity: " << capacity();
  return Resize(size_);
}

bool MappedFile::Remove() {
  if (IsOpen())
    Close();
  std::error_code ec;
  return std::filesystem::remove(file_path_, ec);
}

bool MappedFile::Resize(size_t capacity) {
  LOG(INFO) << "resize file to: " << capacity;
  if (IsOpen())
    Close();
  try {
    std::filesystem::resize_file(file_path_, capacity);
  } catch (...) {
    return false;
  }
  return true;
}

String* MappedFile::CreateString(const string& str) {
  String* ret = Allocate<String>();
  if (ret && !str.empty()) {
    CopyString(str, ret);
  }
  return ret;
}

bool MappedFile::CopyString(const string& src, String* dest) {
  if (!dest)
    return false;
  size_t size = src.length() + 1;
  char* ptr = Allocate<char>(size);
  if (!ptr)
    return false;
  std::strncpy(ptr, src.c_str(), size);
  dest->data = ptr;
  return true;
}

size_t MappedFile::capacity() const {
  return file_->get_size();
}

char* MappedFile::address() const {
  return reinterpret_cast<char*>(file_->get_address());
}

}  // namespace rime
