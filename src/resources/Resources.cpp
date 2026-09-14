#include <malloc.h>
#include <string.h>
#include "Resources.h"
#include "filelist.h"
#include "system/AsyncDeleter.h"
#include "fs/fs_utils.h"
#include "gui/GuiImageData.h"
#include "gui/GuiSound.h"

Resources * Resources::instance = NULL;
CMutex Resources::resourceMutex;

void Resources::Clear()
{
	for(int i = 0; RecourceList[i].filename != NULL; ++i)
	{
		if(RecourceList[i].CustomFile)
		{
			free(RecourceList[i].CustomFile);
			RecourceList[i].CustomFile = NULL;
		}

		RecourceList[i].CustomFileSize = 0;
	}

	resourceMutex.lock();

	//! Destroy the cache directly: the delete worker may already be stopped
	//! at teardown, so queueing would leak the entries.
	if(instance)
	{
		for(auto itr = instance->imageDataMap.begin(); itr != instance->imageDataMap.end(); itr++)
			delete itr->second.second;
		instance->imageDataMap.clear();

		for(auto itr = instance->soundDataMap.begin(); itr != instance->soundDataMap.end(); itr++)
			delete itr->second.second;
		instance->soundDataMap.clear();

		delete instance;
	}
	instance = NULL;

	resourceMutex.unlock();
}

bool Resources::LoadFiles(const char * path)
{
	if(!path)
		return false;

	bool result = false;
	Clear();

	for(int i = 0; RecourceList[i].filename != NULL; ++i)
	{
        std::string fullpath(path);
        fullpath += "/";
        fullpath += RecourceList[i].filename;

        u8 * buffer = NULL;
        u32 filesize = 0;

        LoadFileToMem(fullpath.c_str(), &buffer, &filesize);

        RecourceList[i].CustomFile = buffer;
        RecourceList[i].CustomFileSize = (u32) filesize;
        result |= (buffer != 0);
	}

	return result;
}

const u8 * Resources::GetFile(const char * filename)
{
	for(int i = 0; RecourceList[i].filename != NULL; ++i)
	{
		if(strcasecmp(filename, RecourceList[i].filename) == 0)
		{
			return (RecourceList[i].CustomFile ? RecourceList[i].CustomFile : RecourceList[i].DefaultFile);
		}
	}

	return NULL;
}

u32 Resources::GetFileSize(const char * filename)
{
	for(int i = 0; RecourceList[i].filename != NULL; ++i)
	{
		if(strcasecmp(filename, RecourceList[i].filename) == 0)
		{
			return (RecourceList[i].CustomFile ? RecourceList[i].CustomFileSize : RecourceList[i].DefaultFileSize);
		}
	}
	return 0;
}

GuiImageData * Resources::GetImageData(const char * filename)
{
	if(!filename)
		return NULL;

	resourceMutex.lock();

	if(!instance)
		instance = new Resources;

	std::map<std::string, std::pair<unsigned int, GuiImageData *> >::iterator itr = instance->imageDataMap.find(std::string(filename));
	if(itr != instance->imageDataMap.end())
	{
		itr->second.first++;
		resourceMutex.unlock();
		return itr->second.second;
	}

	for(int i = 0; RecourceList[i].filename != NULL; ++i)
	{
		if(strcasecmp(filename, RecourceList[i].filename) == 0)
		{
			const u8 * buff = RecourceList[i].CustomFile ? RecourceList[i].CustomFile : RecourceList[i].DefaultFile;
			const u32 size = RecourceList[i].CustomFile ? RecourceList[i].CustomFileSize : RecourceList[i].DefaultFileSize;

			if(buff == NULL)
			{
				resourceMutex.unlock();
				return NULL;
			}

			GuiImageData * image = new GuiImageData(buff, size);
			instance->imageDataMap[std::string(filename)].first = 1;
			instance->imageDataMap[std::string(filename)].second = image;

			resourceMutex.unlock();
			return image;
		}
	}

	resourceMutex.unlock();
	return NULL;
}

void Resources::RemoveImageData(GuiImageData * image)
{
	if(!image)
		return;

	resourceMutex.lock();

	if(instance)
	{
		std::map<std::string, std::pair<unsigned int, GuiImageData *> >::iterator itr;

		for(itr = instance->imageDataMap.begin(); itr != instance->imageDataMap.end(); itr++)
		{
			if(itr->second.second == image)
			{
				itr->second.first--;

				if(itr->second.first == 0)
				{
					AsyncDeleter::pushForDelete( itr->second.second );
					instance->imageDataMap.erase(itr);
				}
				break;
			}
		}
	}

	resourceMutex.unlock();
}

GuiSound * Resources::GetSound(const char * filename)
{
	if(!filename)
		return NULL;

	resourceMutex.lock();

	if(!instance)
		instance = new Resources;

	std::map<std::string, std::pair<unsigned int, GuiSound *> >::iterator itr = instance->soundDataMap.find(std::string(filename));
	if(itr != instance->soundDataMap.end())
	{
		itr->second.first++;
		resourceMutex.unlock();
		return itr->second.second;
	}

	for(int i = 0; RecourceList[i].filename != NULL; ++i)
	{
		if(strcasecmp(filename, RecourceList[i].filename) == 0)
		{
			const u8 * buff = RecourceList[i].CustomFile ? RecourceList[i].CustomFile : RecourceList[i].DefaultFile;
			const u32 size = RecourceList[i].CustomFile ? RecourceList[i].CustomFileSize : RecourceList[i].DefaultFileSize;

			if(buff == NULL)
			{
				resourceMutex.unlock();
				return NULL;
			}

			GuiSound * sound = new GuiSound(buff, size);
			instance->soundDataMap[std::string(filename)].first = 1;
			instance->soundDataMap[std::string(filename)].second = sound;

			resourceMutex.unlock();
			return sound;
		}
	}

	resourceMutex.unlock();
	return NULL;
}

void Resources::RemoveSound(GuiSound * sound)
{
	if(!sound)
		return;

	resourceMutex.lock();

	if(instance)
	{
		std::map<std::string, std::pair<unsigned int, GuiSound *> >::iterator itr;

		for(itr = instance->soundDataMap.begin(); itr != instance->soundDataMap.end(); itr++)
		{
			if(itr->second.second == sound)
			{
				itr->second.first--;

				if(itr->second.first == 0)
				{
					AsyncDeleter::pushForDelete( itr->second.second );
					instance->soundDataMap.erase(itr);
				}
				break;
			}
		}
	}

	resourceMutex.unlock();
}
