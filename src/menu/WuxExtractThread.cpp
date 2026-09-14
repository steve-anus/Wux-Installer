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
#include "WuxExtractThread.h"
#include "utils/StringTools.h"

void WuxExtractThread::onProgress(int cur, int total, U32 contentId,
                                  U64 doneBytes, U64 totalBytes, void *user)
{
    WuxExtractThread *self = (WuxExtractThread *)user;
    if (!self || !self->progressBox)
        return;

    int percent = (totalBytes != 0) ? (int)((doneBytes * 100) / totalBytes) : 0;
    if (percent > 100)
        percent = 100;

    if (self->lastContent != cur)
    {
        self->lastContent = cur;
        self->progressBox->setMessage1(
            strfmt("Writing %08X.app (%d/%d)", contentId, cur, total));
    }

    if (percent != self->lastPercent)
    {
        self->lastPercent = percent;
        self->progressBox->setProgress((f32)percent);
        std::string info = strfmt("%0.1f / %0.1f MB (%i",
                                  doneBytes / (1024.0f * 1024.0f),
                                  totalBytes / (1024.0f * 1024.0f), percent);
        info += "%)";
        self->progressBox->setProgressBarInfo(info);
    }
}

WuxExtractThread::WuxExtractThread(const std::string &wuxFile,
                                   const std::string &keyFile,
                                   const std::string &commonKeyFile,
                                   const std::string &installRoot,
                                   MessageBox *box)
    // Worker stack is 192 KiB (the CThread default 32 KiB is not enough):
    // the extraction call chain (extract + I/O + stdio frames) needs the
    // headroom. Same core-0 pinning and priority as InstallWindow's thread.
    : CThread(CThread::eAttributeAffCore0 | CThread::eAttributePinnedAff,
              16, 0x30000)
    , result()
    , error(wux::Error::Ok)
    , wuxPath(wuxFile)
    , keyPath(keyFile)
    , commonKeyPath(commonKeyFile)
    , outRoot(installRoot)
    , progressBox(box)
    , lastContent(0)
    , lastPercent(-1)
{
}

void WuxExtractThread::executeThread()
{
    wux::WuxInstaller installer;
    error = installer.extract(wuxPath.c_str(), keyPath.c_str(),
                              commonKeyPath.c_str(), outRoot.c_str(),
                              result, onProgress, this);
}
