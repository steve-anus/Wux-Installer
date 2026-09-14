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
#include <string.h>
#include "WavDecoder.hpp"
#include "utils/utils.h"

WavDecoder::WavDecoder(const char * filepath)
	: SoundDecoder(filepath)
{
	SoundType = SOUND_WAV;
	SampleRate = 48000;
	Format = (u16)((u16)CHANNELS_STEREO | (u16)FORMAT_PCM_16_BIT);
	Sample16Bit = false;

	if(!file_fd)
		return;

	OpenFile();
}

WavDecoder::WavDecoder(const u8 * snd, int len)
	: SoundDecoder(snd, len)
{
	SoundType = SOUND_WAV;
	SampleRate = 48000;
	Format = (u16)((u16)CHANNELS_STEREO | (u16)FORMAT_PCM_16_BIT);
	Sample16Bit = false;

	if(!file_fd)
		return;

	OpenFile();
}

WavDecoder::~WavDecoder()
{
}


void WavDecoder::OpenFile()
{
	SWaveHdr Header;
	SWaveFmtChunk FmtChunk;
	memset(&Header, 0, sizeof(SWaveHdr));
	memset(&FmtChunk, 0, sizeof(SWaveFmtChunk));

	if(file_fd->read((u8 *) &Header, sizeof(SWaveHdr)) != (int) sizeof(SWaveHdr))
	{
		CloseFile();
		return;
	}

	if(file_fd->read((u8 *) &FmtChunk, sizeof(SWaveFmtChunk)) != (int) sizeof(SWaveFmtChunk))
	{
		CloseFile();
		return;
	}

	if (Header.magicRIFF != 0x52494646) // 'RIFF'
	{
		CloseFile();
		return;
	}
	else if(Header.magicWAVE != 0x57415645) // 'WAVE'
	{
		CloseFile();
		return;
	}
	else if(FmtChunk.magicFMT != 0x666d7420) // 'fmt '
	{
		CloseFile();
		return;
	}

	const u64 fileSize = file_fd->size();

	//! every offset below is computed wide so a hostile size cannot wrap
	u64 offset = (u64) sizeof(SWaveHdr) + (u64) le32(FmtChunk.size) + 8;
	if(offset + 8 > fileSize)
	{
		CloseFile();
		return;
	}

	DataOffset = (u32) offset;
	file_fd->seek(DataOffset, SEEK_SET);

	SWaveChunk DataChunk;
	memset(&DataChunk, 0, sizeof(SWaveChunk));
	if(file_fd->read((u8 *) &DataChunk, sizeof(SWaveChunk)) != (int) sizeof(SWaveChunk))
	{
		CloseFile();
		return;
	}

	int chunks = 0;
	while(DataChunk.magicDATA != 0x64617461) // 'data'
	{
		if(++chunks > 64)
		{
			CloseFile();
			return;
		}

		offset = (u64) DataOffset + 8 + (u64) le32(DataChunk.size);
		if(offset + 8 > fileSize)
		{
			CloseFile();
			return;
		}

		DataOffset = (u32) offset;
		file_fd->seek(DataOffset, SEEK_SET);

		memset(&DataChunk, 0, sizeof(SWaveChunk));
		int ret = file_fd->read((u8 *) &DataChunk, sizeof(SWaveChunk));
		if(ret <= 0)
		{
			CloseFile();
			return;
		}
	}

	DataOffset += 8;
	DataSize = le32(DataChunk.size);
	//! the data block may never reach past the end of the file
	if((u64) DataOffset + (u64) DataSize > fileSize)
		DataSize = (u32) (fileSize - DataOffset);

	Sample16Bit = (le16(FmtChunk.bps) == 16);
	SampleRate = le32(FmtChunk.freq);

	if (le16(FmtChunk.channels) == 1 && le16(FmtChunk.bps) == 8 && le16(FmtChunk.alignment) <= 1)
		Format = (u16)((u16)CHANNELS_MONO | (u16)FORMAT_PCM_8_BIT);
	else if (le16(FmtChunk.channels) == 1 && le16(FmtChunk.bps) == 16 && le16(FmtChunk.alignment) <= 2)
		Format = (u16)((u16)CHANNELS_MONO | (u16)FORMAT_PCM_16_BIT);
	else if (le16(FmtChunk.channels) == 2 && le16(FmtChunk.bps) == 8 && le16(FmtChunk.alignment) <= 2)
		Format = (u16)((u16)CHANNELS_STEREO | (u16)FORMAT_PCM_8_BIT);
	else if (le16(FmtChunk.channels) == 2 && le16(FmtChunk.bps) == 16 && le16(FmtChunk.alignment) <= 4)
		Format = (u16)((u16)CHANNELS_STEREO | (u16)FORMAT_PCM_16_BIT);
}

void WavDecoder::CloseFile()
{
	if(file_fd)
		delete file_fd;

	file_fd = NULL;
}

int WavDecoder::Read(u8 * buffer, int buffer_size, int pos)
{
	if(!file_fd)
		return -1;

	if(CurPos < 0 || (u32) CurPos >= DataSize)
		return 0;

	if(buffer_size < 0)
		return -1;

	file_fd->seek(DataOffset+CurPos, SEEK_SET);

	if((u32) buffer_size > DataSize - (u32) CurPos)
		buffer_size = (int) (DataSize - (u32) CurPos);

	int read = file_fd->read(buffer, buffer_size);
	if(read > 0)
	{
		if (Sample16Bit)
		{
			read &= ~0x0001;

			for (u32 i = 0; i < (u32) (read / sizeof (u16)); ++i)
				((u16 *) buffer)[i] = le16(((u16 *) buffer)[i]);
		}
		CurPos += read;
	}

	return read;
}
