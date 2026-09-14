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
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include "common/types.h"

std::string strfmt(const char * format, ...)
{
	std::string str;
	char * tmp = NULL;

	va_list va;
	va_start(va, format);
	if((vasprintf(&tmp, format, va) >= 0) && tmp)
	{
		str = tmp;
	}
	va_end(va);

	if(tmp)
		free(tmp);

	return str;
}

int strtokcmp(const char * string, const char * compare, const char * separator)
{
	if(!string || !compare)
		return -1;

	char TokCopy[512];
	strncpy(TokCopy, compare, sizeof(TokCopy));
	TokCopy[511] = '\0';

	char * strTok = strtok(TokCopy, separator);

	while (strTok != NULL)
	{
		if (strcasecmp(string, strTok) == 0)
		{
			return 0;
		}
		strTok = strtok(NULL,separator);
	}

	return -1;
}

