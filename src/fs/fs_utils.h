#ifndef __FS_UTILS_H_
#define __FS_UTILS_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "common/types.h"

int LoadFileToMem(const char *filepath, u8 **inbuffer, u32 *size);

int RemoveDirectory(const char *path);
void RemoveDirectoryAndEmptyParents(const char *path, const char *stopAt);

#ifdef __cplusplus
}
#endif

#endif // __FS_UTILS_H_
