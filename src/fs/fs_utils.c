#include "fs_utils.h"
#include <malloc.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>
#include <coreinit/filesystem.h>
#include "utils/logger.h"


int LoadFileToMem(const char *filepath, u8 **inbuffer, u32 *size)
{
    //! always initialze input
	*inbuffer = NULL;
    if(size)
        *size = 0;

    int iFd = open(filepath, O_RDONLY);
	if (iFd < 0)
		return -1;

	off_t off = lseek(iFd, 0, SEEK_END);
	if(off < 0)
	{
		close(iFd);
		return -4;
	}
	if(off == 0)
	{
		//! Empty file: nothing to load, input stays NULL/0.
		close(iFd);
		return 0;
	}
	if(off > (off_t)32 * 1024 * 1024)
	{
		//! Only bundled content is loaded through here; a size this absurd
		//! indicates a corrupt file - refuse instead of trying to allocate.
		close(iFd);
		return -5;
	}

	u32 filesize = (u32)off;
    lseek(iFd, 0, SEEK_SET);

	u8 *buffer = (u8 *) malloc(filesize);
	if (buffer == NULL)
	{
        close(iFd);
		return -2;
	}

    u32 blocksize = 0x4000;
    u32 done = 0;
    int readBytes = 0;

	while(done < filesize)
    {
        if(done + blocksize > filesize) {
            blocksize = filesize - done;
        }
        readBytes = read(iFd, buffer + done, blocksize);
        if(readBytes <= 0)
            break;
        done += readBytes;
    }

    close(iFd);

	if (done != filesize)
	{
		free(buffer);
		return -3;
	}

	*inbuffer = buffer;

    //! sign is optional input
    if(size)
    {
        *size = filesize;
    }

	return filesize;
}

int RemoveDirectory(const char *path)
{
	DIR *d = opendir(path);
	size_t path_len = strlen(path);
	int r = -1;

	if (d)
	{
		struct dirent *p;
		r = 0;
		errno = 0;
		while (!r && (p = readdir(d)))
		{
			int r2 = -1;
			char *buf;
			size_t len;

			/* Skip the names "." and ".." as we don't want to recurse on them. */
			if (!strcmp(p->d_name, ".") || !strcmp(p->d_name, ".."))
				continue;

			len = path_len + strlen(p->d_name) + 2;
			buf = (char *) malloc(len);

			if (buf)
			{
				struct stat statbuf;
				snprintf(buf, len, "%s/%s", path, p->d_name);
				if (!stat(buf, &statbuf))
				{
					if (S_ISDIR(statbuf.st_mode))
						// We don't expect subdirectories in install folders. Let it fail if not empty.
						r2 = rmdir(buf);
					else
						r2 = unlink(buf);
				}
				else
					log_printf("RemoveDirectory: stat failed for %s", buf);
				free(buf);
			}
			r = r2;
			errno = 0;
		}

		if (errno != 0)
			r = -1;

		closedir(d);
	}

	if (!r)
		r = rmdir(path);

	return r;
}

void RemoveDirectoryAndEmptyParents(const char *path, const char *stopAt)
{
	if (RemoveDirectory(path) != 0)
		return;

	// Ancestors may only be walked with an explicit stop boundary.
	if (!stopAt)
		return;

	char parent[512];
	strncpy(parent, path, sizeof(parent));
	parent[sizeof(parent)-1] = '\0';

	while (1)
	{
		char *slash = strrchr(parent, '/');
		if (!slash) break;
		*slash = '\0';

		// Stop if we reached the limit or a root mount point
		if ((stopAt && strcmp(parent, stopAt) == 0) ||
			strlen(parent) < 20)
			break;

		// rmdir will intentionally fail and return non-zero if the directory is not empty
		if (rmdir(parent) != 0)
			break;
	}
}
