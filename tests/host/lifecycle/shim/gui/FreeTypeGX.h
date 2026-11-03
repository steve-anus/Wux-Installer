#pragma once
#include <cstdint>
class FreeTypeGX
{
public:
    FreeTypeGX(const std::uint8_t *buffer, long size, bool lastFace = false);
    ~FreeTypeGX();
};
