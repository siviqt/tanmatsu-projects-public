# Host-authoritative LoRa protocol v2 / setup v4

Implemented 2026-10-08. Both devices require this version. The private-peer v1
protocol is incompatible. This contract supersedes the earlier 19-message
protocol and synchronized deployment/turn barriers.

## Ownership and gameplay

The application task owns every game transition. The host owns one `sb_match_t`
with both fleets and uses the existing deterministic Classic/Escalated resolver.
The guest owns only its local fleet, public knowledge and input staging. Only the
host accepts/resolves attacks; the guest installs authenticated projected states.
**The host device is trusted with both fleets and both locked salvos.** Neither
normal UI displays an unsunk enemy layout. Encryption protects radio traffic,
not against modified software on the authoritative host.

Deployment and targeting are untimed in both LoRa rulesets. Ready locks a valid
fleet. Classic alternates one shot, with no extra turn on hit or reply after the
final sinking. Escalated snapshots round-start allowances and resolves both locked
salvos together; partial/empty salvos and mutual destruction are supported.
Single Player also has no gameplay deadlines. All combat views are combined;
results wait for local F4/Enter acknowledgement while transport remains active.
No combat/deployment clock synchronization exists.

## Pairing, initial readiness and rematches

Cold connection retains the existing verified P-256 ECDH protocol: advertisement
(77 bytes) → guest commitment (68) → host reveal (109) → guest reveal (117).
Magic/version prefixes are `SBA\2`, `SBC\2`, `SBH\2`, `SBJ\2`. The 24-byte
specification is unchanged in layout, but version=4 and all three timer fields
are zero. Roles are host=0 and guest=1. Fixed board/fleet/rules fields are checked
exactly. Host name remains bounded ASCII, 1–16 characters. Changing rules creates
a fresh proposal. Both users compare the six-digit code and setup before Ready.

The transcript hashes the four complete flights in order. ECDH/HKDF-SHA256 uses
that transcript and `SpaceBattles/session/v1` info, retaining library-backed
primitive usage (PSA on device, OpenSSL in host tests). Material layout remains
host/guest AES-128 keys (32 bytes), directional nonce prefixes (8 bytes), and
verification material (32 bytes). HKDF info is a domain string, not the wire
version. Private ephemeral keys are wiped after derivation.

The host sends protected READY(transcript hash). A guest may confirm first in the
UI, but its READY is emitted as a response to host READY, piggybacking receipt.
This removes a predictable simultaneous-transmission collision. Mutual readiness
opens deployment immediately. The guest sends DEPLOY containing its encrypted
fleet. A locally ready host waits for guest deployment before publishing its first
state. A guest that deploys first receives a waiting snapshot; the next snapshot
starts play once the host is ready. No scheduled countdown or auto-deployment.

Mutual REMATCH carries one fresh random 16-byte nonce per role over the existing
authenticated channel. Derive a separate resumption root during initial ECDH:
`root_material = HKDF(shared, SHA256(transcript || "resume/v2"))`; retain its first
32 bytes in RAM. New transcript = SHA256(`"rematch/v2" || old_transcript ||
host_nonce || guest_nonce || new_spec`). Derive fresh traffic material from the
root and new transcript, with no repeat ECDH or human code comparison. Classic
starter alternates; Escalated starter remains 0. New protected READY confirms the
new keys. The old receive/ACK state is retained for one 30-second grace window to
re-ACK a lost final REMATCH receipt, then wiped. It also accepts authenticated
LEAVE during this handoff so a departure cannot strand the new session. Other
old-key game commands are not delivered.

Fast key reuse is limited to mutual rematches on the live authenticated pairing.
Leaving the match wipes pairing material; a later Host/Join or restarted device
uses fresh verified pairing. No persistent peer credentials or process-restart
match restoration are implemented. Never derive a key from the six-digit code.

## Compact protected envelope

Unsigned integers are big-endian; no native struct is serialized. Maximum radio
frame remains 253 bytes, with at most 207 payload bytes.

| Offset | Bytes | Field |
|---:|---:|---|
| 0 | 2 | ASCII SB |
| 2 | 1 | Protocol 2 |
| 3 | 1 | Message type |
| 4 | 8 | Game ID: first 8 transcript-hash bytes |
| 12 | 1 | Source role |
| 13 | 4 | Directional packet counter, starts at 1 |
| 17 | 4 | Reliable command ID, starts at 1; ACK uses 0 |
| 21 | 4 | Last semantically accepted peer command |
| 25 | 4 | Game turn/round; deployment=0; ACK=0 |
| 29 | 1 | Payload length |
| 30 | N | Ciphertext |
| 30+N | 16 | AES-GCM tag |

All 30 header bytes are AAD. Nonce = directional 4-byte prefix, four zero bytes,
and the 4-byte packet counter. Keys change on every match. Counter/command wrap
fails closed. Game ID truncation does not replace authentication: mismatched
match keys still reject a packet. The rules/specification are bound into the key
transcript rather than retransmitted as 32 bytes on every packet.

There is one retained outstanding command per direction and one queued successor
(two bounded command slots). New semantic replies piggyback receipt. ACK-only
frames are 46 bytes and never request ACKs. Exact retries retain all original
ciphertext/header/tag bytes. The replay window is 64 authenticated accepted
packets. Only the last accepted command's exact duplicate can be re-ACKed; it
cannot repeat damage or refresh liveness. Conflicting duplicates, command gaps,
invalid phases, malformed counts and bad authentication do not mutate game state.
An authenticated matching receipt is processed before the new message context.

## Message set

| ID | Type | Payload / handling |
|---:|---|---|
| 1 | ACK | Empty; cumulative receipt releases the matching outstanding command |
| 2 | READY | 32-byte transcript; verification only; host initiates |
| 3 | DEPLOY | Guest only; five `(anchor cell, vertical boolean)` pairs in ship-ID order; complete legal fleet required |
| 4 | ACT | Guest only; count plus sorted unknown cells, 1–6 bytes; Classic exactly one; Escalated 0–allowance |
| 5 | STATE | Host only; 104-byte bounded guest snapshot, described below |
| 6 | STATUS | Revision u32 + snapshot digest 16; receipt establishes health; stale guest revision requests a fresh host snapshot |
| 7 | SURRENDER | Empty; allowed before local lock; host determines terminal outcome |
| 8 | LEAVE | Empty; forfeit started match to remaining player; retain completed outcome |
| 9 | REMATCH | Fresh nonce 16; completed match only; mutual consent renews keys/spec starter |

No SYNC, TIME, START, START_OK, SHOT/RESULT pair, COMMIT, REVEAL, AGREE, ANIMATED,
PING/PONG pair, or generic RESUME state machine remains. Commands already in
flight when the host surrenders are consumed as cancelled, with no damage, so
they cannot block terminal receipts/rematch. Malformed cancelled commands are
still rejected.

Surrender immediately dismisses pending shot/round review and open overlays on
receipt of the terminal result, including when the local player has already
submitted a turn. No F4/Enter is needed to show the surrender result. The retained
previous combat record must not start another review. Normal combat results
continue to use explicit Continue.

### STATE payload

| Offset | Bytes | Field |
|---:|---:|---|
| 0 | 4 | Increasing authoritative revision |
| 4 | 4 | Game sequence (already next turn/round after nonterminal resolution) |
| 8 | 1 | Deployment=0, combat=1, terminal=2 |
| 9 | 1 | Outcome (ongoing/A/B/draw) |
| 10 | 1 | Actor |
| 11 | 1 | Two deployment-ready bits |
| 12 | 1 | Two salvo-locked bits |
| 13 | 2 | Round-start allowances |
| 15 | 10 | Guest fleet anchor/orientation pairs; unplaced=255/0 |
| 25 | 50 | Two public boards, 2 bits per cell, low bits first within each byte |
| 75 | 12 | Remaining-ship counts indexed 0..5 for each public board |
| 87 | 1 | End reason: none=0, combat=1, surrender A/B=2/3, departure A/B=4/5 (abort=6 is local only) |
| 88 | 16 | First 16 bytes of SHA256(payload bytes 0..87) |

A STATE is 150 bytes including envelope/tag, and always fits one radio frame.
Outcome/reason pairs are validated centrally by `sb_result_valid`; terminal
reasons cannot be changed by later snapshots. Setup v4 is required on both peers.
The digest identifies this exact guest projection, including its own fleet; it
is not a hash of the host's private match memory. Guest hits derive from incoming
public knowledge. Host layout is never serialized. Decode/validate into bounded
temporary state before publishing it. An older waiting snapshot must not unlock
a locally submitted guest fleet or salvo. Terminal outcome cannot be overwritten
by a later nonterminal or different-outcome snapshot.

This implementation deliberately uses full small snapshots instead of the design
report's optional event deltas. It simplifies resynchronization and avoids a
fragment/reorder/history-merge subsystem. A guest derives display effects from
new public cells; the host retains the resolver's record. The full snapshot costs
more airtime than a minimal Classic result delta, but far fewer frames than v1.

## Action, timing and failure flow

Guest Classic action: ACT → STATE (also receipts ACT) → ACK or next legal command.
Host Classic action: local resolve → STATE → guest ACK/next legal command. Local
selection/pending feedback is immediate; hit/miss comes only from the host.
Host will not accept another local action while its previous snapshot is awaiting
delivery. Target presentation does not create a network phase or fixed hold.

Escalated: host lock stays local; guest ACT is receipted if the host is still
thinking. Once both locks exist, the shared resolver resolves both directions,
advances the context or ends the match, and emits one STATE. Both players may
think indefinitely; no turn-start or selection deadline applies.

Radio worker alone owns airtime pacing across matches/regions. Protocol poll
reserves one immutable candidate. The application reports enqueue rejection or
copied worker TX completion; retry starts at actual completion, not at enqueue.
A queue rejection neither claims delivery nor consumes a retry interval. RX/TX
completion queues are bounded. Completion-queue overflow fails the worker rather
than silently losing transport ownership. Driver callbacks never mutate session
or framebuffer data. Shutdown retains callback resources as required by the
upstream driver, uses bounded worker join, and preserves SD-controller ownership.
Every deliberate secure exit requests reliable LEAVE, including verification,
results and rematch waiting. Existing commands drain in order; an unsent final
host snapshot precedes LEAVE to preserve the completed outcome. The worker stays
active until the departure is acknowledged or the 30-second grace deadline ends.
No new heartbeat or rematch is initiated during departure. Retries retain normal
radio pacing; transmission completion alone is not a delivery receipt.

Keep 10-second base retry, a guest-only maximum-frame-airtime+150ms retry offset,
and 0–100ms retry jitter. There is no fixed guest ACK delay. Host normally initiates
idle STATUS after 30 seconds, including while users compare verification codes; the guest only initiates recovery STATUS when needed.
Silence after 90 seconds enters a fixed 30-second recovery window. Missing delivery
also starts one fixed recovery incident. Classic guest actions additionally have
a fixed host-response deadline of six retries plus grace, unaffected by heartbeats.
No unrelated traffic may keep an outstanding command's recovery alive forever.
An authenticated LEAVE during a started match immediately awards the remaining
player a win with "Opponent has abandoned", even during result review, waiting,
or an overlay. Pairing cancellation does not award a win. A previously finished
result is preserved. No rematch is offered after the peer leaves. The application
dismisses any overlay/review and displays a departure notice before returning to
main automatically. The notice lasts at least 3 seconds, allowing pending receipts
to finish, with a limit of 3 seconds plus one base retry interval (13 seconds for
the regional profiles). Link failure follows the same notice/return path.
Pre-secure discovery remains governed by its existing pairing timeout; there is
no authenticated session to send a protected departure on yet.
Loss/disconnect without LEAVE abandons, never awards a timeout win. Terminal results survive
link loss. Host failure has no election/migration. In-memory snapshots repair stale
guest state; restarting either process starts a new match.

## Radio profiles and validation

The explicit region-confirmation update passed application hardware tests,
owner-confirmed on 2026-10-10. Historical acceptance is summarized below.
No reliable current-location source is available. The selected `region` must
match the persisted `manual_region` confirmation before the application opens
LoRa. First launch, older saves and profile changes require confirmation in
Settings > Radio. In-match region edits/reset are blocked.

Existing board/SX1262/v3.4.0 firmware compatibility checks, config
readback, idle RX recovery and SD/ESP-Hosted controller handoff are retained.
EU868: 869.525 MHz, BW125, SF7, CR4/5, 0 dBm. US915: 915 MHz, BW500, same other
settings. Both retain conservative SF7/BW125 accounting at 9%, including US915.
No regulatory or hardware qualification claim is added by this protocol change.
The public-source privacy update removes the two-device MAC allowlist. Radio
initialization no longer reads or compares a unit's MAC address. Other compatible
Tanmatsu units can initialize the radio when the existing board, chip, firmware
version/hash and confirmed regional settings match. This change does not alter
pairing, encryption or the wire protocol; it has not received new hardware testing.
A 150-byte STATE accounts for approximately 246ms airtime and 2.73s same-device
start spacing; the 253-byte driver ceiling accounts for 394.496ms / 4.384s.
Do not install standalone bootloaders/partition tables: use the AppFS application
binary through the launcher on both devices.

Run `tools/test-multiplayer.py`, `tools/test-multiplayer-runtime.py`,
`tools/investigate-lora-countdown.py`, `tools/test-radio.py`, and the retained
M2/M4/M8/M9/M10 suites. The runtime suite now uses real PAX/font rendering,
both roles/rulesets, full games, encrypted rematches, lost final rematch receipt,
private views, deliberate Leave, and SD lifecycle calls. Core tests use the
regional pacing model with real airtime and half-duplex collision accounting.
The project owner reported on 2026-10-09: “Tests have passed on both devices.”
This records two-device acceptance separately from the automated checks above.
Detailed device case coverage and hardware timing measurements were not reported;
physical RF qualification remains separate.

On 2026-10-10 the owner confirmed “HW checks are pass.” after the gameplay update
and immediate surrender-result fix. The final runtime suite additionally passed
ten no-input surrender cases across host/guest: deployment, submitted Escalated
salvos, Classic review, Help and Link. Historical acceptance is summarized below.
This records the hardware report without attributing the automated case matrix
or timing measurements to physical tests.

On 2026-10-12 the owner confirmed “HW checks passed” for Release #5, including
the delivered LoRa departure update. Historical binaries and test outputs are
not distributed; current regression test sources are included.
The initial Release #6 rebuild changed only release identification; the later
runtime hygiene update was accepted on 2026-10-13. See the
[release notes](../../../RELEASES.md). The Release #5 hardware report does not
supply per-case physical logs or new timing measurements.
