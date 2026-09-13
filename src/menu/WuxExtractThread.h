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

#include <string>
#include "system/CThread.h"
#include "gui/MessageBox.h"
#include "wux/wux_installer.h"

// Runs the .wux extraction on a worker thread so the main loop keeps
// rendering while the files are written (the fork's install progress works
// the same way: InstallWindow is a CThread that updates its MessageBox from
// its own thread). This thread only calls simple setters on the progress
// box - it never touches the GUI element tree.
class WuxExtractThread : public CThread
{
public:
    WuxExtractThread(const std::string &wuxPath, const std::string &keyPath,
                     const std::string &commonKeyPath, const std::string &outRoot,
                     MessageBox *progressBox);

    // Filled in by the worker when extract() returns. The main thread reads
    // them only after isThreadTerminated() (both threads run on core 0, so
    // the reads are serialized with the worker's writes).
    wux::ExtractResult result;
    wux::Error error;

private:
    void executeThread();

    // Progress callback (see extract's ProgressFn): only simple setters on
    // the progress box - the same cross-thread pattern the fork's
    // InstallWindow uses for its install progress. Updates are throttled:
    // the file name changes per content file, the bar per percent step.
    static void onProgress(int cur, int total, U32 contentId,
                           U64 doneBytes, U64 totalBytes, void *user);

    std::string wuxPath;
    std::string keyPath;
    std::string commonKeyPath;
    std::string outRoot;
    MessageBox *progressBox;
    int lastContent;   // last content index reported to the progress box
    int lastPercent;   // last percent reported (throttles text updates)
};

#endif // _WUX_EXTRACT_THREAD_H
