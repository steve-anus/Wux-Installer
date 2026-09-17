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
#include "AsyncDeleter.h"
#include "utils/logger.h"

std::atomic<AsyncDeleter *> AsyncDeleter::deleterInstance(NULL);
std::atomic<bool> AsyncDeleter::instanceUnavailable(false);

//! Guards lazy creation of deleterInstance; pushForDelete and
//! triggerDeleteProcess may run on the main thread and worker threads alike.
static CMutex instanceMutex;

AsyncDeleter * AsyncDeleter::getDeleterInstance(void)
{
    // Acquire pairs with the release store below. A plain pointer here would
    // let a second thread see a non-NULL instance before the constructor's
    // stores (notably CMutex::pMutex) are visible on its core, and CMutex
    // fails open on a NULL pMutex: two threads would push/pop the same
    // std::queue with no lock at all.
    AsyncDeleter *inst = deleterInstance.load(std::memory_order_acquire);
    if(inst || instanceUnavailable.load(std::memory_order_acquire))
        return inst;

    instanceMutex.lock();
    //! Re-check under the mutex so only one thread ever builds the worker.
    inst = deleterInstance.load(std::memory_order_relaxed);
    if(!inst && !instanceUnavailable.load(std::memory_order_relaxed))
    {
        inst = new AsyncDeleter;
        if(inst && !inst->isCreated())
        {
            log_printf("AsyncDeleter: delete thread could not start\n");
            delete inst;
            inst = NULL;
        }
        if(!inst)
            instanceUnavailable.store(true, std::memory_order_release);
        deleterInstance.store(inst, std::memory_order_release);
    }
    instanceMutex.unlock();

    return deleterInstance.load(std::memory_order_acquire);
}

AsyncDeleter::AsyncDeleter()
	: CThread(CThread::eAttributeAffCore1 | CThread::eAttributePinnedAff)
	, exitApplication(false)
{
}

AsyncDeleter::~AsyncDeleter()
{
    exitApplication = true;

    //! The worker only runs when resumed, so it may never come back to drain
    //! what is left. Reclaim it here; test-and-pop runs under deleteMutex as
    //! well, so an in-flight worker steal cannot race this drain.
    deleteMutex.lock();
    while(!deleteElements.empty())
    {
        AsyncDeleter::Element *element = deleteElements.front();
        deleteElements.pop();
        delete element;
    }
    while(!realDeleteElements.empty())
    {
        AsyncDeleter::Element *element = realDeleteElements.front();
        realDeleteElements.pop();
        delete element;
    }
    deleteMutex.unlock();
}

void AsyncDeleter::pushForDelete(AsyncDeleter::Element *e)
{
    if(!e)
        return;

    AsyncDeleter *inst = getDeleterInstance();
    if(!inst)
    {
        // The delete worker is unavailable (out of memory, or already torn
        // down). Freeing the element here is not safe: the fade and OK
        // handlers - the progress-box fade, the result-box OK - run from inside the element's
        // own signal emission, so its member function is still on the call
        // stack and a free makes the emit return into destroyed memory. Other
        // callers push objects that are not emitting, but this fallback cannot
        // tell the two apart, so it never frees. Leaking is the correct loss:
        // the process is already out of memory, and a use after free is a crash
        // or worse where a leak only degrades. Skipped destructors also pin
        // Resources image/sound refcounts until the heaps are destroyed whole.
        // Atomic because pushForDelete is callable from worker threads too.
        static std::atomic<unsigned int> leakedCount(0);
        unsigned int count = ++leakedCount;
        // Log the first leak, then only at powers of two: a permanent failure
        // with the UI still animating would otherwise write a line per element
        // per frame.
        if(count == 1 || (count & (count - 1)) == 0)
            log_printf("AsyncDeleter: delete thread unavailable, leaking element (total %u)\n", count);
        return;
    }

    inst->deleteMutex.lock();
    // Identity guard: the same element may be queued only once. A double
    // emit from one button press (touch + A-proxy answering in the same
    // frame) used to queue the same element twice and free it twice.
    {
        std::queue<Element*> probe = inst->deleteElements;
        while(!probe.empty())
        {
            if(probe.front() == e)
            {
                inst->deleteMutex.unlock();
                log_printf("AsyncDeleter: element already pending in delete queue, skip\n");
                return;
            }
            probe.pop();
        }
        probe = inst->realDeleteElements;
        while(!probe.empty())
        {
            if(probe.front() == e)
            {
                inst->deleteMutex.unlock();
                log_printf("AsyncDeleter: element already handed to delete thread, skip\n");
                return;
            }
            probe.pop();
        }
    }
    inst->deleteElements.push(e);
    inst->deleteMutex.unlock();
}

bool AsyncDeleter::deleteQueueEmpty()
{
    // Same acquire read as getDeleterInstance(): this decides whether the
    // shutdown path may tear down the heaps the queued objects live in, so it
    // must not answer from a stale or torn view of the pointer.
    AsyncDeleter *inst = deleterInstance.load(std::memory_order_acquire);
    if(!inst)
        return true;

    inst->deleteMutex.lock();
    bool empty = inst->deleteElements.empty() &&
                 inst->realDeleteElements.empty();
    inst->deleteMutex.unlock();
    return empty;
}

void AsyncDeleter::triggerDeleteProcess(void)
{
    //! to trigger the event after GUI process is finished execution
    //! this function is used to swap elements from one to next array
    AsyncDeleter *inst = getDeleterInstance();
    if(!inst)
        return;

    bool moved = false;

    inst->deleteMutex.lock();
    while(!inst->deleteElements.empty())
    {
        inst->realDeleteElements.push(inst->deleteElements.front());
        inst->deleteElements.pop();
        moved = true;
    }
    inst->deleteMutex.unlock();

    if(moved)
        inst->resumeThread();
}

void AsyncDeleter::executeThread(void)
{
    while(!exitApplication)
    {
        suspendThread();

        //! delete elements that require post process deleting
        //! because otherwise they would block or do invalid access on GUI thread
        for(;;)
        {
            AsyncDeleter::Element *element = NULL;

            //! Test and pop in ONE critical section: reading empty()/front()
            //! outside the lock races the producers and the shutdown drain.
            deleteMutex.lock();
            if(!realDeleteElements.empty())
            {
                element = realDeleteElements.front();
                realDeleteElements.pop();
            }
            deleteMutex.unlock();

            if(!element)
                break;

            delete element;
        }
    }

}
