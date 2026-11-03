#pragma once
#include <wut.h>
class GuiSound
{
public:
    GuiSound(const u8 *sound, int length);
    void SetLoop(bool loop);
    void Play();
    void SetVolume(int volume);
};
