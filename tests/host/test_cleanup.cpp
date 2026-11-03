#include "check.h"
#include "wux/cleanup_guard.h"
#include "wux/key_file.h"

#include <cstdio>
#include <cstdlib>
#include <dirent.h>
#include <fcntl.h>
#include <new>
#include <stdexcept>
#include <string>
#include <unistd.h>

// Allocation failure on demand: the next g_allocsLeft calls to operator new
// succeed, the one after throws std::bad_alloc. -1 never fails.
static long g_allocsLeft = -1;

void* operator new(std::size_t n) {
    if (g_allocsLeft == 0) throw std::bad_alloc();
    if (g_allocsLeft > 0) --g_allocsLeft;
    void* p = std::malloc(n ? n : 1);
    if (!p) throw std::bad_alloc();
    return p;
}
void* operator new[](std::size_t n) { return operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

// The folder remover from src/fs/fs_utils.c, which needs the console SDK;
// this records the calls instead.
static int g_removeCalls = 0;
static std::string g_removedDir, g_removedRoot;
extern "C" int RemoveDirectoryAndEmptyParents(const char* path, const char* stopAt) {
    ++g_removeCalls;
    g_removedDir = path;
    g_removedRoot = stopAt ? stopAt : "";
    return 0;
}

static int openFdCount() {
    int n = 0;
    DIR* d = opendir("/proc/self/fd");
    if (!d) return -1;
    while (readdir(d)) ++n;
    closedir(d);
    return n;
}

static bool fdOpen(int fd) { return fcntl(fd, F_GETFD) != -1; }
static bool exists(const std::string& p) { return access(p.c_str(), F_OK) == 0; }

void testCleanup() {
    std::string p = tempPath("guard.bin");
    {   // an exception leaves no descriptor and no file
        int fd = open(p.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        CHECK(fd >= 0);
        try {
            wux::FileGuard file(fd, p.c_str());
            throw std::runtime_error("x");
        } catch (const std::runtime_error&) {}
        CHECK(!fdOpen(fd));
        CHECK(!exists(p));
    }
    {   // commit keeps the file and closes it
        int fd = open(p.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        bool ok = false;
        {
            wux::FileGuard file(fd, p.c_str());
            ok = file.commit();
        }
        CHECK(ok);
        CHECK(!fdOpen(fd));
        CHECK(exists(p));
    }
    {   // without a path the file is only closed
        int fd = open(p.c_str(), O_RDONLY);
        { wux::FileGuard file(fd); }
        CHECK(!fdOpen(fd));
        CHECK(exists(p));
        unlink(p.c_str());
    }

    {   // the folder is removed on a continue and on an exception, kept by keep()
        g_removeCalls = 0;
        for (int i = 0; i < 3; ++i) {
            wux::FolderGuard partial("/install/0005000010101A00", "/install");
            if (i == 1) continue;
            if (i == 2) partial.keep();
        }
        CHECK(g_removeCalls == 2);
        CHECK(g_removedDir == "/install/0005000010101A00");
        CHECK(g_removedRoot == "/install");
        g_removeCalls = 0;
        try {
            wux::FolderGuard partial("/install/x", "/install");
            throw std::bad_alloc();
        } catch (const std::bad_alloc&) {}
        CHECK(g_removeCalls == 1);
    }

    {   // readWhole closes its file whichever allocation fails
        std::vector<unsigned char> data(300 * 1024, 0x5a);
        FILE* fp = std::fopen(p.c_str(), "wb");
        std::fwrite(data.data(), 1, data.size(), fp);
        std::fclose(fp);
        int before = openFdCount();
        int threw = 0;
        bool finished = false;
        for (long k = 0; k < 64 && !finished; ++k) {
            std::vector<unsigned char> out;
            g_allocsLeft = k;
            try {
                finished = wux::readWhole(p.c_str(), out) == wux::Error::Ok;
            } catch (const std::bad_alloc&) {
                ++threw;
            }
            g_allocsLeft = -1;
            CHECK(openFdCount() == before);
            if (finished) CHECK(out.size() == data.size());
        }
        CHECK(threw >= 2);   // the read buffer and at least one growth of out
        CHECK(finished);
        unlink(p.c_str());
    }
}
