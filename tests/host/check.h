#ifndef HOST_CHECK_H
#define HOST_CHECK_H

#include <cstdio>
#include <string>
#include <vector>

extern int g_checks;
extern int g_failures;

#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_failures; \
    std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

std::vector<unsigned char> fromHex(const char* hex);
std::string tempPath(const char* name);

void testAes();
void testContainer();
void testFst();
void testTmd();
void testTicket();
void testKeyFile();
void testChecked();
void testCleanup();
void testGuiText();

#endif
