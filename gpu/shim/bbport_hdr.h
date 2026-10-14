// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: the HDR decision, kept out of the settings class so tests can pin it.
#pragma once

/// Whether the port may use HDR output.
///
/// `BB_HDR` wins when it names a value, with the port's usual truthiness (a leading 1, y or t), so
/// `BB_HDR=0` forces HDR off even on a display that could show it. Unset or empty leaves it to the
/// display: an HDR-capable screen gets the game's own HDR setting enabled, a plain one keeps
/// today's SDR behaviour. Neither is something a player should have to know to set -- which is why
/// this is not simply `Flag("BB_HDR", false)` any more.
inline bool BbHdrAllowed(const char* bb_hdr, bool display_has_hdr) {
    if (bb_hdr != nullptr && bb_hdr[0] != '\0') {
        return bb_hdr[0] == '1' || bb_hdr[0] == 'y' || bb_hdr[0] == 't';
    }
    return display_has_hdr;
}
