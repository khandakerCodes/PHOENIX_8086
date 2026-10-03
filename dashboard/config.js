// SPDX-License-Identifier: MIT
/**
 * Phoenix-8086 — Dashboard configuration
 *
 * emulator: run the kernel inside the page (v86) instead of connecting
 * to a telemetry bridge. The hosted demo site sets this to true; it
 * needs the files in emulator/, which tools/build_site.sh puts there.
 * "?emulator" or "?live" in the address overrides it.
 */
window.PhoenixConfig = {
    emulator: false,
};
