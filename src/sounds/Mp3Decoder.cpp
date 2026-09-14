/***************************************************************************
 * Copyright (C) 2010
 * by Dimok
 *
 * This software is provided 'as-is', without any express or implied
 * warranty. In no event will the authors be held liable for any
 * damages arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any
 * purpose, including commercial applications, and to alter it and
 * redistribute it freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you
 * must not claim that you wrote the original software. If you use
 * this software in a product, an acknowledgment in the product
 * documentation would be appreciated but is not required.
 *
 * 2. Altered source versions must be plainly marked as such, and
 * must not be misrepresented as being the original software.
 *
 * 3. This notice may not be removed or altered from any source
 * distribution.
 *
 * for WiiXplorer 2010
 ***************************************************************************/
#include <string>
#include <string.h>
#include <limits.h>
#include <unistd.h>
#include <malloc.h>
#include <math.h>
#include "common/types.h"
#include "utils/utils.h"
#include "utils/logger.h"
#include "Mp3Decoder.hpp"

Mp3Decoder::Mp3Decoder(const char * filepath)
	: SoundDecoder(filepath)
{
	SoundType = SOUND_MP3;
	ReadBuffer = NULL;
	mad_timer_reset(&Timer);
	mad_stream_init(&Stream);
	mad_frame_init(&Frame);
	mad_synth_init(&Synth);

	if(!file_fd)
		return;

	OpenFile();
}

Mp3Decoder::Mp3Decoder(const u8 * snd, int len)
	: SoundDecoder(snd, len)
{
	SoundType = SOUND_MP3;
	ReadBuffer = NULL;
	mad_timer_reset(&Timer);
	mad_stream_init(&Stream);
	mad_frame_init(&Frame);
	mad_synth_init(&Synth);

	if(!file_fd)
		return;

	OpenFile();
}

Mp3Decoder::~Mp3Decoder()
{
	ExitRequested = true;
	//! capped handoff so teardown cannot hang on a stuck decode loop
	int waitCount = 0;
	while(Decoding && waitCount < 2000)
	{
		usleep(100);
		waitCount++;
	}
	if(Decoding)
		log_printf("Mp3Decoder: timed out waiting for decode to finish\n");

	mad_synth_finish(&Synth);
	mad_frame_finish(&Frame);
	mad_stream_finish(&Stream);

	if(ReadBuffer)
		free(ReadBuffer);
	ReadBuffer = NULL;
}

void Mp3Decoder::OpenFile()
{
	GuardPtr = NULL;
	//! libmad reads MAD_BUFFER_GUARD bytes of zero padding past the stream data
	ReadBuffer = (u8 *) memalign(32, ALIGN32(SoundBlockSize*SoundBlocks) + MAD_BUFFER_GUARD);
	if(!ReadBuffer)
	{
		if(file_fd)
			delete file_fd;
		file_fd = NULL;
		return;
	}

	u8 dummybuff[4096];
	int ret = Read(dummybuff, 4096, 0);
	if(ret <= 0)
	{
		if(file_fd)
			delete file_fd;
		file_fd = NULL;
		return;
	}

	SampleRate = (u32) Frame.header.samplerate;
	Format = ((MAD_NCHANNELS(&Frame.header) == 2) ? (u16)((u16)CHANNELS_STEREO | (u16)FORMAT_PCM_16_BIT) : (u16)((u16)CHANNELS_MONO | (u16)FORMAT_PCM_16_BIT));
	Rewind();
}

int Mp3Decoder::Rewind()
{
	mad_synth_finish(&Synth);
	mad_frame_finish(&Frame);
	mad_stream_finish(&Stream);
	mad_timer_reset(&Timer);
	mad_stream_init(&Stream);
	mad_frame_init(&Frame);
	mad_synth_init(&Synth);
	SynthPos = 0;
	GuardPtr = NULL;

	if(!file_fd)
		return -1;

	return SoundDecoder::Rewind();
}

static inline s16 FixedToShort(mad_fixed_t Fixed)
{
	/* Clipping */
	if(Fixed>=MAD_F_ONE)
		return(SHRT_MAX);
	if(Fixed<=-MAD_F_ONE)
		return(-SHRT_MAX);

	Fixed=Fixed>>(MAD_F_FRACBITS-15);
	return((s16)Fixed);
}

int Mp3Decoder::Read(u8 * buffer, int buffer_size, int pos)
{
	if(!file_fd)
		return -1;

	//! the frame channel count can differ from the cached format, so keep the
	//! sample alignment at 16 bit and bound every store below instead
	buffer_size &= ~0x0001;

	u8 * write_pos = buffer;
	u8 * write_end = buffer+buffer_size;

	while(1)
	{
		while(SynthPos < Synth.pcm.length)
		{
			if(write_pos + 2 > write_end)
				return write_pos-buffer;

			*((s16 *) write_pos) = FixedToShort(Synth.pcm.samples[0][SynthPos]);
			write_pos += 2;

			if(MAD_NCHANNELS(&Frame.header) == 2)
			{
				if(write_pos + 2 > write_end)
					return write_pos-buffer;

				*((s16 *) write_pos) = FixedToShort(Synth.pcm.samples[1][SynthPos]);
				write_pos += 2;
			}
			SynthPos++;
		}

		if(Stream.buffer == NULL || Stream.error == MAD_ERROR_BUFLEN)
		{
			u8 * ReadStart = ReadBuffer;
			int ReadSize = SoundBlockSize*SoundBlocks;
			int Remaining = 0;

			if(Stream.next_frame != NULL)
			{
				Remaining = Stream.bufend - Stream.next_frame;
				memmove(ReadBuffer, Stream.next_frame, Remaining);
				ReadStart += Remaining;
				ReadSize -= Remaining;
			}

			ReadSize = file_fd->read(ReadStart, ReadSize);
			if(ReadSize <= 0)
			{
				//! second EOF pass: the padding already in place yielded no
				//! new frame, so the stream is done - end it, don't spin
				if(GuardPtr)
					return -1;

				GuardPtr = ReadStart;
				memset(GuardPtr, 0, MAD_BUFFER_GUARD);
				ReadSize = MAD_BUFFER_GUARD;
				//! keep stream data plus its guard inside the allocation
				if(Remaining + ReadSize > SoundBlockSize*SoundBlocks)
					ReadSize = SoundBlockSize*SoundBlocks - Remaining;
			}

			CurPos += ReadSize;
			mad_stream_buffer(&Stream, ReadBuffer, Remaining+ReadSize);
			//! libmad may read MAD_BUFFER_GUARD bytes past the stream data
			memset(ReadBuffer + Remaining + ReadSize, 0, MAD_BUFFER_GUARD);
		}

		if(mad_frame_decode(&Frame,&Stream))
		{
			if(MAD_RECOVERABLE(Stream.error))
			{
				/*
				 * a lost sync inside the end of data padding cannot be
				 * recovered by refilling the buffer
				 */
				if(Stream.error == MAD_ERROR_LOSTSYNC && GuardPtr)
					return -1;

				Stream.error = MAD_ERROR_NONE;
				continue;
			}

			if(Stream.error != MAD_ERROR_BUFLEN)
				return -1;

			/*
			 * MAD_ERROR_BUFLEN needs more input, every other error is fatal.
			 * mad_synth_frame() must never run on a frame that failed to
			 * decode, its header and samples are only valid after success.
			 */
			if(GuardPtr)
				return -1;

			continue;
		}

		mad_timer_add(&Timer,Frame.header.duration);
		mad_synth_frame(&Synth,&Frame);
		SynthPos = 0;
	}
	return 0;
}
