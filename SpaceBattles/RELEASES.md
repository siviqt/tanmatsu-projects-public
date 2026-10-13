# Release notes

The current application is **Release #6**, including the runtime hygiene update
accepted on hardware on 2026-10-13. The owner reported: “HW test passed. No traces
of the lag now.”

`components/spacebattles/release.h` supplies the release number to the About screen
and firmware metadata. Settings schema and LoRa protocol versions are independent.
Release numbers advance only with explicit owner approval; this source export
does not increment the release number.

## Release #6 — runtime hygiene update, 2026-10-13

- Original nonblocking input polling and one-tick idle yield retained.
- Reduced diagnostic logging and faster rendering for menu selection changes.
- Eleven host suites and the ESP-IDF production build passed before hardware
  acceptance. No measured power-saving claim is made.

The accepted application binary has SHA-256
`80f9e1be2befe12722f3450371c45535ceacfebeb102cee15024e37c0f887905`;
its matching ELF has SHA-256
`d8ebb180f22f030ef17f9d2a051b3e238dafa1b917b04047d443fb48093c80d2`.
These identify the historical hardware-tested build. This source distribution
contains no firmware, debug binaries or device logs; fresh builds may have
different hashes.

## Release #6 — initial rebuild, 2026-10-12

Updated the release identification after Release #5 acceptance. CMake tracks
`release.h` so incremental builds refresh firmware metadata when it changes.

## Release #5 — LoRa departure handling, 2026-10-12

Hardware accepted. Secure session exits retry departure until receipt or a
bounded deadline. Peer departure or connection loss dismisses overlays, displays
a notice and returns to the main menu automatically. Completed results survive
departure; authenticated departure is accepted during the rematch key grace period.

## Release #4 — GUI and play-test fixes, 2026-10-12

Hardware accepted. Added the GUI v3 presentation, skipped unchanged LoRa frames,
removed a redundant full-frame clear and visual clutter, and refreshed peer
rematch status without requiring local input. Player identity appears once above
each combat board. Three earlier releases were owner-reported; their numbering
is not reconstructed from historical build dates.
