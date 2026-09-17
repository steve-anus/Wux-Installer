#ifndef __FS_UTILS_H_
#define __FS_UTILS_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "common/types.h"

int LoadFileToMem(const char *filepath, u8 **inbuffer, u32 *size);

int RemoveDirectory(const char *path);
// Returns 0 when the target is gone afterwards (removed or already
// absent), -1 when it could not be removed (logged).
int RemoveDirectoryAndEmptyParents(const char *path, const char *stopAt);

#ifdef __cplusplus
}
#endif

#endif // __FS_UTILS_H_
