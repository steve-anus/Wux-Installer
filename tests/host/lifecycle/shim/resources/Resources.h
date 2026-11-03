#pragma once
#include <wut.h>
class Resources
{
public:
    static bool LoadFiles(const char *path);
    static const u8 *GetFile(const char *name);
    static u32 GetFileSize(const char *name);
    static void Clear();
};
