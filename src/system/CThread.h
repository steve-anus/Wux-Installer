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
#ifndef CTHREAD_H_
#define CTHREAD_H_

#include <malloc.h>
#include <unistd.h>
#include <cstring>
#include <coreinit/thread.h>
#include "common/types.h"
#include "utils/logger.h"

class CThread
{
public:
	typedef void (* Callback)(CThread *thread, void *arg);

	//! constructor
	CThread(int iAttr, int iPriority = 16, int iStackSize = 0x8000, CThread::Callback callback = NULL, void *callbackArg = NULL)
		: pThread(NULL)
		, pThreadStack(NULL)
		, pCallback(callback)
		, pCallbackArg(callbackArg)
		, threadCreated(false)
		, createRet(-1)
	{
	    //! save attribute assignment
	    iAttributes = iAttr;
		//! allocate the thread
		pThread = (OSThread*)memalign(8, sizeof(OSThread));
		//! allocate the stack
		pThreadStack = (u8 *) memalign(0x20, iStackSize);
        //! create the thread
		if(pThread && pThreadStack)
		{
			//! zero the block so a failed create leaves recognizable emptiness:
			//! coreinit writes the entry point only on success. (OSCreateThread
			//! is a coreinit import returning BOOL by Nintendo convention, but
			//! detection via fields does not depend on that convention.)
			std::memset(pThread, 0, sizeof(OSThread));
			int ret = OSCreateThread(pThread, &CThread::threadCallback, 1, (char*)this, pThreadStack+iStackSize, iStackSize, iPriority, iAttributes);
			threadCreated = (pThread->entryPoint ==
			                 (OSThreadEntryPointFn)&CThread::threadCallback);
			createRet = ret;
			log_printf("CThread: OSCreateThread ret=%d created=%d\n", ret, (int)threadCreated);
		}
	}

	//! destructor
	virtual ~CThread() { shutdownThread(); }

	static CThread *create(CThread::Callback callback, void *callbackArg, int iAttr = eAttributeNone, int iPriority = 16, int iStackSize = 0x8000)
	{
	    return ( new CThread(iAttr, iPriority, iStackSize, callback, callbackArg) );
	}

	//! Get thread ID
	virtual void* getThread() const { return pThread; }
	//! True when OSCreateThread() succeeded and the thread object is still owned
	bool isCreated() const { return threadCreated && pThread != NULL; }
	//! Thread entry function
	virtual void executeThread(void)
	{
	    if(pCallback)
            pCallback(this, pCallbackArg);
	}
	//! Suspend thread
	virtual void suspendThread(void) { if(isThreadSuspended()) return; if(pThread) OSSuspendThread(pThread); }
	//! Resume thread
	virtual void resumeThread(void) { if(!isThreadSuspended()) return; if(pThread) OSResumeThread(pThread); }
	//! Set thread priority
	virtual void setThreadPriority(int prio) { if(pThread) OSSetThreadPriority(pThread, prio); }
	//! Check if thread is suspended
	virtual bool isThreadSuspended(void) const { if(pThread) return OSIsThreadSuspended(pThread); return false; }
	//! Check if thread is terminated
	virtual bool isThreadTerminated(void) const { if(pThread) return OSIsThreadTerminated(pThread); return false; }
	//! Shutdown thread
	virtual void shutdownThread(void)
	{
		if(!pThread)
		{
			//! the stack was still allocated when only the thread block failed
			if(pThreadStack)
				free(pThreadStack);
			pThreadStack = NULL;
			return;
		}

		//! wait for the thread to finish, only when OSCreateThread() succeeded
		if(threadCreated && !(iAttributes & eAttributeDetach))
		{
		    if(isThreadSuspended())
                resumeThread();

			OSJoinThread(pThread, NULL);
		}
		// Fail-safe for the field-based create detection: a raw return of
		// 0 or 1 could have meant success under either plausible
		// convention, so "field says not created" is not trusted there and
		// the memory is kept (leaked) rather than freed under a possibly
		// live thread. Any other return (including -1: create never ran)
		// is an unambiguous failure and is freed normally.
		if(threadCreated || (createRet != 0 && createRet != 1))
		{
			//! free the thread stack buffer
			if(pThreadStack)
				free(pThreadStack);
			if(pThread)
				free(pThread);
		}
		else
			log_printf("CThread: create status ambiguous, thread memory kept\n");

		pThread = NULL;
		pThreadStack = NULL;
	}
    //! Thread attributes
	enum eCThreadAttributes
	{
	    eAttributeNone              = 0x07,
	    eAttributeAffCore0          = 0x01,
	    eAttributeAffCore1          = 0x02,
	    eAttributeAffCore2          = 0x04,
	    eAttributeDetach            = 0x08,
	    eAttributePinnedAff         = 0x10
	};
private:
	static int threadCallback(int argc, const char **argv)
	{
		//! After call to start() continue with the internal function
		((CThread *) argv)->executeThread();
		return 0;
	}
    int iAttributes;
	OSThread *pThread;
	u8 *pThreadStack;
	Callback pCallback;
	void *pCallbackArg;
	bool threadCreated;
	int createRet;
};

#endif
