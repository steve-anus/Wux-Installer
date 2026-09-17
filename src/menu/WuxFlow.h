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
#ifndef _WUX_FLOW_H
#define _WUX_FLOW_H

#include <string>
#include <vector>

class MessageBox;
class WuxExtractThread;

/*
 * Single source of truth for the install flow.
 *
 * This replaced three independent booleans. The old invariant - which the
 * transition methods below now enforce by construction - was:
 *
 *   Idle            <=> !wuxBusy && !installWindowOpen
 *   WupInstall      <=> !wuxBusy &&  installWindowOpen (manual /install install)
 *   WuxExtract      <=>  wuxBusy &&  extraction worker running
 *   WuxInstall      <=>  wuxBusy &&  installWindowOpen (started by the wux flow)
 *   WuxErrorBox     <=>  wuxBusy &&  result/error box awaiting the OK click
 *
 * Every state but Idle blocks both entry points, so one test covers the
 * re-entrancy guard that used to be spread over the boolean pairs.
 *
 * The worker and progress-box pointers are private on purpose: releasing one
 * while the other is still live is the memory-safety bug this class exists to
 * prevent, so every change of either goes through a method that keeps the
 * pair consistent.
 *
 * Threading: owned by the GUI thread only. The extraction worker never
 * touches this object; MainWindow::update() polls the worker and moves the
 * state on the GUI thread.
 */
class WuxFlow
{
public:
    enum class State
    {
        Idle,             // start screen; both entry points live
        WupInstall,       // manual /install WUP install window open
        WuxExtract,       // extraction worker running, progress box shown
        WuxInstall,       // install window open, started by the wux flow
        WuxErrorBox       // result/error box awaiting the OK click
    };

    WuxFlow();
    ~WuxFlow();

    // Owning raw pointers: a copy would double-free both.
    WuxFlow(const WuxFlow &) = delete;
    WuxFlow & operator=(const WuxFlow &) = delete;

    State state() const { return curState; }

    //! Name used in the SD log, so a dead button can be diagnosed offline.
    static const char * stateName(State s);

    //! Moves to newState from `expected` only; otherwise logs and keeps the
    //! current state. A refused transition never leaves the UI without a way
    //! out, because the entry point that asked for it simply does nothing.
    bool transition(State expected, State newState);

    //! Moves to newState from any state. For the paths where the flow was
    //! already checked by the caller (the worker just reported in).
    void force(State newState);

    // ---- transitions ----------------------------------------------------
    bool beginWupInstall()   { return transition(State::Idle, State::WupInstall); }
    bool beginExtraction()   { return transition(State::Idle, State::WuxExtract); }
    void extractionFinished(bool installStarted)
    {
        force(installStarted ? State::WuxInstall : State::WuxErrorBox);
    }
    void enterErrorBox()       { force(State::WuxErrorBox); }
    void flowFinished()        { force(State::Idle); }

    // ---- extraction worker ------------------------------------------------
    //! Takes ownership of the worker (NULL accepts "no worker yet").
    void setThread(WuxExtractThread *thread);

    //! The live worker, or NULL. Reads of the worker's results must go
    //! through joinThread() first; see MainWindow::OnWuxExtractFinished.
    WuxExtractThread *thread() const { return extractThread; }

    //! True when a worker exists and has run to completion.
    bool extractDone() const;

    //! Blocks until the worker has finished. Gives the happens-before edge
    //! for reading its result fields; safe to call twice.
    void joinThread();

    //! Joins and releases the worker (NULL-safe).
    void releaseThread();

    // ---- progress box -----------------------------------------------------
    //! Takes ownership of the progress box. Warns if one is still live, which
    //! would mean a fade-out handler never ran.
    void setProgressBox(MessageBox *box);

    MessageBox *progressBox() const { return progressBoxPtr; }

    //! Arming the fade-out and setting the flag must move together: if the
    //! flag were set alone, a fade-IN completion would take the fade-out
    //! branch and delete a box the render pass is still using.
    void armProgressFadeOut();

    bool progressFadingOut() const { return progressBoxClosing; }

    //! The fade-out handler gave the box to the deferred delete queue: drop
    //! both the claim on it and the flag, in step.
    void finishProgressFadeOut();

    // ---- flow data --------------------------------------------------------
    void setCleanupFiles(const std::string &wuxPath, const std::string &keyPath);
    const std::vector<std::string> & cleanupFiles() const { return cleanupFilesList; }

    void setFinalNote(const std::string &note) { finalNoteText = note; }
    const std::string & finalNote() const { return finalNoteText; }

    //! Releases worker and progress box. Order is load bearing: the worker is
    //! joined first so it cannot write to the box afterwards.
    void shutdown();

private:
    State curState;

    //! Real delete of the progress box. Only shutdown() may use it: once a
    //! box has been handed to the deferred delete queue the queue owns it.
    void releaseProgressBox();

    WuxExtractThread *extractThread;
    //! Private names differ from the accessors above: a member and a member
    //! function of the same class cannot share a name.
    MessageBox *progressBoxPtr;
    //! True while the progress box is fading out and its effect handler is
    //! the one that must release it. Orthogonal to State: the fade is still
    //! running after the flow has moved on.
    bool progressBoxClosing;

    std::vector<std::string> cleanupFilesList;   // .wux + game.key paths
    std::string finalNoteText;                   // extraction accounting for the final box
};

#endif // _WUX_FLOW_H
