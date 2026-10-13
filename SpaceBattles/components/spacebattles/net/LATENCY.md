# Host-authoritative replacement — 2026-10-08

The v2 implementation replaces the v1 latency update. There are no synchronized
starts, 45-second lead, 60-second LoRa deployment limit, selection timers, result
agreement exchanges or animation-ready barriers. The normal host game resolver
owns both fleets. Guest ACT produces one bounded STATE; receipt is piggybacked
where possible. First-time verified ECDH remains; mutual rematches derive fresh
keys without repeating it. Leaving/restarting requires fresh pairing.

The renderer regression is fixed at its source: the right-side multiplayer label
is now the bounded `Encrypted link` / `Recovering link`, with static lifetime.
All three font profiles are tested with the real tactical renderer, and the full
Versus runtime suite also renders actual menus/game screens. Escalated now displays
UNTIMED rather than a misleading 00:00 combat clock.

The first implementation favors a single 149-byte full guest snapshot over delta
updates. It contains guest-private/public state only, fits one frame, and provides
simple recovery. The 30-byte envelope replaces the prior 80-byte envelope. The
radio worker is the only airtime pacing owner and reports actual TX completion.
ACKs have no fixed 500ms guest delay. Host-led confirmation/health avoids routine
simultaneous traffic. Lost frames retain bounded retries with collision backoff.

Host-simulator results at the existing 9% conservative profile, instant player
input, and no real hardware/startup/code-comparison delay:

| Scenario | Game steps | Elapsed | RF frames |
|---|---:|---:|---:|
| Classic clean | 83 turns | 230.34 s | 213 |
| Escalated clean | 21 rounds, including an empty round | 60.46 s | 69 |
| Classic deterministic loss/duplicates | 83 turns | about 401 s | 236 |
| Escalated deterministic loss/duplicates | 21 rounds | about 107 s | 74 |

Clean results average roughly 2.8 seconds per complete turn/round under immediate
continuous input; this includes paced reliable delivery. This is not Fire-to-hit
latency, a human-game average, or a physical measurement. Retry jitter makes the
loss cases vary slightly. Human thinking may allow the airtime budget to recover.
Full first-time pairing and hardware initialization are outside this simulation.

On 2026-10-09 the project owner reported: “Tests have passed on both devices.”
This records two-device acceptance; no measured hardware latencies or itemized
case coverage accompanied the report. The simulator figures above remain
simulation results.

Detailed device-validation reference checklist: both roles, Classic/Escalated, all font profiles,
manual deployment, prolonged thinking, partial/empty salvos, terminal sinking/draw,
lost deployment/state/receipt, temporary interference, clean Leave, failed host,
repeated rematches, SD transitions and stack/heap high-water logging. Both devices
must install the same current wire v2/setup v4 build; older protocol versions
are intentionally rejected. The figures above describe the historical simulation,
not a new measurement of the current release.
