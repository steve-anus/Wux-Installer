/*
 * wuxinstaller - ticket / NUS content key derivation.
 *
 * The GM partition's FST is NUS content 0, encrypted with the title's CONTENT
 * key (not the title/disc key). The content key is recovered from the ticket
 * and the console common key:
 *
 *   contentKey = AES-CBC(commonKey, IV, encKey)   (one 16-byte block)
 *   encKey     = ticket[0x1BF .. 0x1BF+16]
 *   titleID    = ticket[0x1DC .. 0x1DC+8]  (u64 big-endian)
 *   IV         = [ titleID u64 BE (bytes 0-7), 0x00 (bytes 8-15) ]
 *
 * The content key and the disc key (game.key) are different values; game.key
 * only decrypts the disc structure (TOC, data FST, TIK/TMD/CERT). The common
 * key is console-specific and user-supplied (never distributed or hardcoded).
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
#ifndef _WUX_TICKET_H
#define _WUX_TICKET_H

#include "wux/wux_common.h"

namespace wux {

// Ticket (title.tik) field offsets.
const U32 kTicketKeyOffset   = 0x1BF;  // 16-byte encrypted content key
const U32 kTicketTitleIdOffset = 0x1DC;  // 8-byte title ID (u64 BE)

// Derive a title's NUS content key from its decrypted ticket (title.tik) and
// the console common key. On success contentKey holds 16 bytes.
Error deriveContentKey(const U8* commonKey, const U8* tik, size_t tikLen,
                       U8 contentKey[16]);

} // namespace wux

#endif // _WUX_TICKET_H
