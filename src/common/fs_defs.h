#ifndef FS_DEFS_H
#define	FS_DEFS_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* SD-card folders used by the install flow. */
#define SD_INSTALL_PATH                 "fs:/vol/external01/install"
#define SD_WUDUMP_PATH                  "fs:/vol/external01/wudump"

#ifdef __cplusplus
}
#endif

#endif	/* FS_DEFS_H */
