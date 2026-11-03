#ifndef HOST_STUB_CVIDEO_H
#define HOST_STUB_CVIDEO_H
#include "common/types.h"

class CVideo {
public:
    u32 frameCount = 0;
    u32 getFrameCount(void) const { return frameCount; }
};

#endif
