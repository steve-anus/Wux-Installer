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
#ifndef _CMUTEX_H_
#define _CMUTEX_H_

#include <malloc.h>
#include <coreinit/mutex.h>
#include "utils/logger.h"

class CMutex
{
public:
    CMutex() {
        pMutex = (OSMutex*) malloc(sizeof(OSMutex));
        if(!pMutex)
        {
            //! Fail-open: lock/unlock degrade to no-ops. Loud, because every
            //! caller assumes mutual exclusion.
            log_printf("CMutex: failed to allocate OSMutex, locking is inactive\n");
            return;
        }

        OSInitMutex(pMutex);
    }
    virtual ~CMutex() {
        if(pMutex)
            free(pMutex);
    }

    void lock(void) {
        if(pMutex)
            OSLockMutex(pMutex);
    }
    void unlock(void) {
        if(pMutex)
            OSUnlockMutex(pMutex);
    }
    bool tryLock(void) {
        if(!pMutex)
            return false;

        return (OSTryLockMutex(pMutex) != 0);
    }
private:
    OSMutex *pMutex;
};

#endif // _CMUTEX_H_
