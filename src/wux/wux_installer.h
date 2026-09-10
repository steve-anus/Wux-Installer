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

namespace wux {

// Result of a .wux extraction.
struct ExtractResult {
    bool ok = false;
    U64 titleID = 0;
    std::string outDir;     // absolute path to <outRoot>/<TITLEID>/
    int contentCount = 0;
    std::string error;      // human-readable reason on failure
};

// Progress callback, called once per content file before it is written:
// (currentIndex 1-based, total, contentId, user). May be null.
typedef void (*ProgressFn)(int cur, int total, U32 contentId, void* user);

class WuxInstaller {
public:
    // Extract the .wux at wuxPath (title key at keyPath) into outRoot/<TITLEID>/
    // using the JWUDTool layout: raw still-encrypted <id8>.app, <id8>.h3 for
    // hashed content, plus the decrypted title.tmd / title.tik / title.cert.
    // The completed folder is then installable by the fork's existing MCP flow.
    Error extract(const char* wuxPath, const char* keyPath, const char* outRoot,
                  ExtractResult& result,
                  ProgressFn progress = nullptr, void* progressUser = nullptr);
};

} // namespace wux

#endif // _WUX_INSTALLER_H
