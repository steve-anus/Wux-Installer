/****************************************************************************
 * Copyright (C) 2015 Dimok
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 ****************************************************************************/
#ifndef _AXSOUND_H_
#define _AXSOUND_H_

#include <sndcore2/core.h>
#include <sndcore2/voice.h>

class Voice
{
public:

    enum VoicePriorities
    {
        PRIO_MIN = 1,
        PRIO_MAX = 31
    };

    enum VoiceStates
    {
        STATE_STOPPED,
        STATE_START,
        STATE_PLAYING,
        STATE_STOP,
    };

    Voice(int prio)
        : state(STATE_STOPPED)
    {
        lastLoopCounter = 0;
        nextBufferSize = 0;
        bytesPerSample = 2;

        voice = AXAcquireVoice(prio, 0, 0);
        if(voice)
        {
            AXSetVoiceType(voice, 0);
            setVolume(0x80000000);

            AXVoiceDeviceMixData mix[6];
            memset(mix, 0, sizeof(mix));
            mix[0].bus[0].volume = 0x8000;
            mix[0].bus[0].delta = 0;
            mix[1].bus[0].volume = 0x8000;
            mix[1].bus[0].delta = 0;

            AXSetVoiceDeviceMix(voice, 0, 0, mix);
            AXSetVoiceDeviceMix(voice, 1, 0, mix);
        }
    }

    ~Voice()
    {
        if(voice)
        {
            AXFreeVoice(voice);
        }
    }

    void play(const u8 *buffer, u32 bufferSize, const u8 *nextBuffer, u32 nextBufSize, u16 format, u32 sampleRate)
    {
        if(!voice)
            return;

        if(!buffer || bufferSize < 2)
            return;

        memset(&voiceBuffer, 0, sizeof(voiceBuffer));

        /*
         * offsets are counted in samples, so they depend on the sample width
         * (formats arrive as the low byte of the decoder format: 0x0A 16 bit,
         * 0x19 8 bit)
         */
        bytesPerSample = (format == AX_VOICE_FORMAT_LPCM8) ? 1 : 2;

        voiceBuffer.data = buffer;
        voiceBuffer.dataType = format;
        voiceBuffer.loopingEnabled = (nextBuffer == NULL) ? 0 : 1;
        voiceBuffer.currentOffset = 0;
        voiceBuffer.endOffset = (bufferSize / bytesPerSample) - 1;
        voiceBuffer.loopOffset = (nextBuffer == NULL) ? 0 :
                                 ((u32) (nextBuffer - buffer) / bytesPerSample);
        nextBufferSize = nextBufSize;

        // TODO: handle support for 3.1.0 with dynamic libs instead of static linking it
        //u32 samplesPerSec = (AXGetInputSamplesPerSec != 0) ? AXGetInputSamplesPerSec() : 32000;
        u32 samplesPerSec = AXGetInputSamplesPerSec();

        memset(&ratioBits, 0, sizeof(ratioBits));
        ratioBits.ratio = (u32)(0x00010000 * ((f32)sampleRate / (f32)samplesPerSec));

        AXSetVoiceOffsets(voice, &voiceBuffer);
        AXSetVoiceSrc(voice, &ratioBits);
        AXSetVoiceSrcType(voice, 1);
        AXSetVoiceState(voice, 1);
    }

    void stop()
    {
        if(voice)
            AXSetVoiceState(voice, 0);
    }

    void setVolume(u32 vol)
    {
        if(voice)
        {
            AXVoiceVeData data;
            data.volume = vol >> 16;
            data.delta = vol & 0xFFFF;
            AXSetVoiceVe(voice, &data);
        }
    }


    void setNextBuffer(const u8 *buffer, u32 bufferSize)
    {
        if(!voice)
            return;

        if(!buffer || bufferSize < 2)
            return;

        if(!voiceBuffer.data || buffer < (const u8 *) voiceBuffer.data)
            voiceBuffer.loopOffset = 0;
        else
            voiceBuffer.loopOffset = (u32) (buffer - (const u8 *) voiceBuffer.data) / bytesPerSample;

        nextBufferSize = bufferSize;

        AXSetVoiceLoopOffset(voice, voiceBuffer.loopOffset);
    }

    bool isBufferSwitched()
    {
        if(!voice)
            return false;

        u32 loopCounter = AXGetVoiceLoopCount(voice);
        if(lastLoopCounter != loopCounter)
        {
            lastLoopCounter = loopCounter;
            u32 nextSamples = (nextBufferSize >= bytesPerSample) ? (nextBufferSize / bytesPerSample) : 1;
            AXSetVoiceEndOffset(voice, voiceBuffer.loopOffset + nextSamples - 1);
            return true;
        }
        return false;
    }

    u32 getInternState() const {
        if(voice)
            return *(volatile const u32 *) &voice->state;
        return 0;
    }
    u32 getState() const {
        return state;
    }
    void setState(u32 s) {
        state = s;
    }

    void * getVoice() const {
        return voice;
    }

private:
    AXVoice *voice;
    AXVoiceSrc ratioBits;
    AXVoiceOffsets voiceBuffer;
    u32 state;
    u32 nextBufferSize;
    u32 lastLoopCounter;
    u32 bytesPerSample;
};

#endif // _AXSOUND_H_
