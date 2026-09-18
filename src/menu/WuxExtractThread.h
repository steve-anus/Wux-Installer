/*
 * wuxinstaller - worker thread for the .wux extraction pipeline.
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
#ifndef _WUX_EXTRACT_THREAD_H
#define _WUX_EXTRACT_THREAD_H

#include <atomic>
#include <string>
#include "system/CThread.h"
#include "gui/MessageBox.h"
#include "wux/wux_installer.h"

// Runs the .wux extraction on a worker thread so the main loop keeps
// rendering while the files are written (InstallWindow's install progress
// works the same way: a CThread that updates its MessageBox from its own
// thread). This thread only calls simple setters on the progress
// box - it never touches the GUI element tree.
class WuxExtractThread : public CThread
{
public:
    WuxExtractThread(const std::string &wuxFile, const std::string &keyFile,
                     const std::string &commonKeyFile, const std::string &installRoot,
                     MessageBox *box);

    //! Asks the worker to stop at its next progress point (it checks the flag
    //! per written chunk, and the partial file is unlinked like a write
    //! error). Used only by the foreground-release quit path, so the writer
    //! is not abandoned at process exit.
    void requestCancel() { cancelRequested.store(true); }

    // Filled in by the worker when extract() returns. The main thread reads
    // them only after isThreadTerminated() polls true, which orders the
    // worker's writes before the reads; the render thread never touches
    // them.
    wux::ExtractResult result;
    wux::Error error;

private:
    void executeThread();

    // Progress callback (see extract's ProgressFn): only simple setters on
    // the progress box - the same cross-thread pattern InstallWindow uses
    // for its install progress. Updates are throttled:
    // the file name changes per content file, the bar per percent step.
    // Returns false once the worker was asked to cancel (ProgressFn contract).
    static bool onProgress(int cur, int total, U32 contentId,
                           U64 doneBytes, U64 totalBytes, void *user);

    // Cancel checkpoint (see extract's CancelFn): polled at every title
    // boundary and before the first write, so a quit request is seen even in
    // the read/decrypt phase. True = keep extracting.
    static bool onCancel(void *user);

    std::string wuxPath;
    std::string keyPath;
    std::string commonKeyPath;
    std::string outRoot;
    MessageBox *progressBox;
    int lastContent;   // last content index reported to the progress box
    int lastPercent;   // last percent reported (throttles text updates)
    std::atomic<bool> cancelRequested = { false };
};

#endif // _WUX_EXTRACT_THREAD_H
