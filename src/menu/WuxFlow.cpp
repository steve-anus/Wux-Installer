/*
 * wuxinstaller - state of the "install wux" flow.
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
 */
#include "WuxFlow.h"

#include "gui/MessageBox.h"
#include "utils/logger.h"
#include "WuxExtractThread.h"

WuxFlow::WuxFlow()
    : curState(State::Idle)
    , extractThread(NULL)
    , progressBoxPtr(NULL)
    , progressBoxClosing(false)
{
}

WuxFlow::~WuxFlow()
{
    shutdown();
}

const char * WuxFlow::stateName(State s)
{
    switch(s)
    {
        case State::Idle:           return "Idle";
        case State::WupInstall:     return "WupInstall";
        case State::WuxExtract:     return "WuxExtract";
        case State::WuxInstall:     return "WuxInstall";
        case State::WuxErrorBox:    return "WuxErrorBox";
    }

    return "Unknown";
}

bool WuxFlow::transition(State expected, State newState)
{
    if(curState != expected)
    {
        // The one diagnostic that explains why a button stopped responding.
        log_printf("WuxFlow: %s -> %s refused (expected %s)",
                   stateName(curState), stateName(newState), stateName(expected));
        return false;
    }

    curState = newState;
    return true;
}

void WuxFlow::force(State newState)
{
    curState = newState;
}

void WuxFlow::setThread(WuxExtractThread *thread)
{
    extractThread = thread;
}

bool WuxFlow::extractDone() const
{
    return extractThread != NULL && extractThread->isThreadTerminated();
}

void WuxFlow::joinThread()
{
    if(extractThread != NULL)
        extractThread->shutdownThread();
}

void WuxFlow::releaseThread()
{
    if(extractThread != NULL)
    {
        // ~CThread joins the worker, so nothing of it runs past this point.
        delete extractThread;
        extractThread = NULL;
    }
}

void WuxFlow::setProgressBox(MessageBox *box)
{
    if(progressBoxPtr != NULL && box != NULL)
    {
        // The fade-out handler must have discarded the previous box first; a
        // live one here means it never ran. Logged rather than deleted: the
        // deferred delete queue may already own it.
        log_printf("WuxFlow: progress box replaced while still live");
    }

    progressBoxPtr = box;
    progressBoxClosing = false;
}

void WuxFlow::armProgressFadeOut()
{
    progressBoxClosing = true;
}

void WuxFlow::finishProgressFadeOut()
{
    // The queue owns the box from the caller's pushForDelete; give up both the
    // claim and the flag together so shutdown() cannot delete it a second time.
    progressBoxPtr = NULL;
    progressBoxClosing = false;
}

void WuxFlow::setCleanupFiles(const std::string &wuxPath, const std::string &keyPath)
{
    cleanupFilesList.clear();
    cleanupFilesList.push_back(wuxPath);
    cleanupFilesList.push_back(keyPath);
}

void WuxFlow::releaseProgressBox()
{
    if(progressBoxPtr != NULL)
    {
        // Real delete, destructor path only. Precondition: the box is no
        // longer being animated. The fade handler hands boxes to the deferred
        // delete queue (finishProgressFadeOut) and a live emit there could not
        // tolerate a free, so reaching this with a box still on screen means an
        // update pass could touch freed memory. ~MainWindow clears the frames
        // around this point and nothing updates them afterwards.
        delete progressBoxPtr;
        progressBoxPtr = NULL;
    }
}

void WuxFlow::shutdown()
{
    // Order matters: the worker writes to the progress box until it is
    // joined, so the box must go last.
    releaseThread();
    releaseProgressBox();

    progressBoxClosing = false;
    cleanupFilesList.clear();
    finalNoteText.clear();
}
