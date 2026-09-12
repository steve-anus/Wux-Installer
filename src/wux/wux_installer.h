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
    // Extract every title found in the .wux at wuxPath (title key at keyPath) into outRoot/<TITLEID>/
    Error extract(const char* wuxPath, const char* keyPath, const char* outRoot,
                  ExtractResult& result,
                  ProgressFn progress = nullptr, void* progressUser = nullptr);
};

} // namespace wux

#endif // _WUX_INSTALLER_H
