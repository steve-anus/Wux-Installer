/*
 * wuxinstaller - .wux extraction pipeline.
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
#ifndef _WUX_INSTALLER_H
#define _WUX_INSTALLER_H

#include "wux/wux_common.h"
#include <string>
#include <vector>

namespace wux {

// Result of a .wux extraction. A disc can hold several titles (e.g. the game
// plus the 00050010-10060000 "rear.rpx" dummy); one folder is produced per
// title and all of them are installable.
struct ExtractResult {
    bool ok = false;
    U64 titleID = 0;                // first extracted title
    std::string outDir;             // first title's folder
    int contentCount = 0;           // content files across all titles
    std::string error;              // human-readable reason on failure
    std::vector<std::string> outDirs;   // all <outRoot>/<TITLEID> folders
    int titleCount = 0;
};

// Progress callback, called once per content file before it is written:
// (currentIndex 1-based, total, contentId, user). May be null.
typedef void (*ProgressFn)(int cur, int total, U32 contentId, void* user);

class WuxInstaller {
public:
    // Extract every title found in the .wux at wuxPath into outRoot/<TITLEID>/.
    //
    // keyPath is the title/disc key (game.key); it decrypts the disc structure
    // (TOC, data FST, TIK/TMD/CERT). commonKeyPath is the console common key
    // (common.key); each GM title's NUS content key is derived from its ticket
    // plus this key, and that content key decrypts the GM FST (NUS content 0).
    // Both files live next to the .wux
    Error extract(const char* wuxPath, const char* keyPath,
                  const char* commonKeyPath, const char* outRoot,
                  ExtractResult& result,
                  ProgressFn progress = nullptr, void* progressUser = nullptr);
};

} // namespace wux

#endif // _WUX_INSTALLER_H
