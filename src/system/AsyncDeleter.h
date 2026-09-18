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
#ifndef _ASYNC_DELETER_H
#define _ASYNC_DELETER_H

#include <queue>
#include <atomic>
#include "CThread.h"
#include "CMutex.h"

class AsyncDeleter : public CThread
{
public:
    static void destroyInstance()
    {
        // Latch before releasing the object: ~AsyncDeleter drains its queues
        // and a destructor it runs can call pushForDelete again. With the flag
        // set first that nested call leaks (logged) instead of lazily building
        // a fresh worker at teardown that nobody would ever join.
        instanceUnavailable.store(true, std::memory_order_release);
        AsyncDeleter *inst = deleterInstance.load(std::memory_order_acquire);
        deleterInstance.store(NULL, std::memory_order_release);
        delete inst;
    }

    class Element
    {
    public:
        Element() {}
        virtual ~Element() {}
    };

    //!Queues an element for deferred deletion on the delete worker thread.
    //!May be called from any thread.
    static void pushForDelete(AsyncDeleter::Element *e);

    //!True when both pending queues are empty; callers use it to wait for
    //!all queued deletions before tearing down the heaps the objects live in.
    static bool deleteQueueEmpty();

    static void triggerDeleteProcess(void);

private:
    AsyncDeleter();
    virtual ~AsyncDeleter();

    //! Published with release/acquire rather than as a plain pointer: the
    //! fast path below reads it outside instanceMutex, and on weakly ordered
    //! POWER a non-NULL read must not arrive before the constructor's writes
    //! (especially CMutex::pMutex, which CMutex treats as a silent no-op).
    static std::atomic<AsyncDeleter *> deleterInstance;
    //! Latched when the worker could not be created, or once it is destroyed.
    //! Without this every push would retry new/memalign/delete on the caller's
    //! thread - often the GUI thread inside a signal emit - forever.
    static std::atomic<bool> instanceUnavailable;

    void executeThread(void);

    //! creates the singleton on first use; NULL when the thread could not start
    static AsyncDeleter * getDeleterInstance(void);

    //!Set from the main thread, polled by the delete worker loop.
    std::atomic<bool> exitApplication;
    std::queue<AsyncDeleter::Element *> deleteElements;
    std::queue<AsyncDeleter::Element *> realDeleteElements;
    //! Elements popped but not yet deleted. Counted under deleteMutex and
    //! required zero by deleteQueueEmpty(): the queues alone read empty while
    //! the LAST element is still between its pop and its destructor, and
    //! heap teardown waits on this answer.
    int deleteInFlight;
    CMutex deleteMutex;
};

#endif // _ASYNC_DELETER_H
