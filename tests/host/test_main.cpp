#include "check.h"

#include <cstdlib>
#include <unistd.h>

int g_checks = 0;
int g_failures = 0;

std::vector<unsigned char> fromHex(const char* hex) {
    std::vector<unsigned char> out;
    for (; hex[0] && hex[1]; hex += 2) {
        unsigned v = 0;
        std::sscanf(hex, "%2x", &v);
        out.push_back((unsigned char)v);
    }
    return out;
}

std::string tempPath(const char* name) {
    const char* dir = std::getenv("TMPDIR");
    return std::string(dir ? dir : "/tmp") + "/wuxhost-" +
           std::to_string((long)getpid()) + "-" + name;
}

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);   // keep output if a test crashes
    testAes();
    testContainer();
    testFst();
    testTmd();
    testTicket();
    if (g_failures) {
        std::printf("HOST TESTS FAILED: %d of %d checks\n", g_failures, g_checks);
        return 1;
    }
    std::printf("HOST TESTS OK: %d checks\n", g_checks);
    return 0;
}
