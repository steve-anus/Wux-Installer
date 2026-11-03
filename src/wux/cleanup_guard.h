#ifndef _WUX_CLEANUP_GUARD_H
#define _WUX_CLEANUP_GUARD_H

// Cleanup that runs on every way out of a scope: a return, a loop
// `continue`, or an exception such as std::bad_alloc.

#include "fs/fs_utils.h"

#include <unistd.h>

namespace wux {

// Owns an open descriptor and closes it when the scope ends. Given a path,
// it also unlinks that file unless commit() kept it.
class FileGuard {
public:
    explicit FileGuard(int fd, const char* removePath = nullptr)
        : fd_(fd), path_(removePath) {}
    ~FileGuard() {
        if (fd_ >= 0) ::close(fd_);
        if (path_) ::unlink(path_);
    }
    FileGuard(const FileGuard&) = delete;
    FileGuard& operator=(const FileGuard&) = delete;

    int fd() const { return fd_; }

    // Close the file and keep it. A failed close counts as a failed write:
    // returns false and the file is still unlinked.
    bool commit() {
        int r = ::close(fd_);
        fd_ = -1;
        if (r != 0) return false;
        path_ = nullptr;
        return true;
    }

private:
    int fd_;
    const char* path_;
};

// Removes a title's output folder (and emptied parents up to root) when the
// scope ends, unless keep() ran. RemoveDirectoryAndEmptyParents logs its own
// failures and cannot throw (C code).
class FolderGuard {
public:
    FolderGuard(const char* dir, const char* root) : dir_(dir), root_(root) {}
    ~FolderGuard() {
        if (!kept_) RemoveDirectoryAndEmptyParents(dir_, root_);
    }
    FolderGuard(const FolderGuard&) = delete;
    FolderGuard& operator=(const FolderGuard&) = delete;

    void keep() { kept_ = true; }

private:
    const char* dir_;
    const char* root_;
    bool kept_ = false;
};

} // namespace wux

#endif // _WUX_CLEANUP_GUARD_H
