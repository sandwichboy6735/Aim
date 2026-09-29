# Lock-On Lab

A small browser game for testing anti-cheat. It has a built-in aim bot that locks onto
characters, plus an anti-cheat system that watches your aim and tries to catch it.

Open `index.html` in a browser. There's nothing to install.

## Controls

| Input | Action |
| --- | --- |
| Mouse | Aim |
| Click | Shoot |
| Right-click or `Q` (hold) | Lock on with the bot |
| `WASD` | Move |
| `B` | Cycle bot mode |
| `R` | Reset the session |

## Bot modes

- **Snap**: moves the crosshair onto the target instantly.
- **Smooth**: pulls the crosshair toward the target at a constant speed.
- **Humanized**: waits for a human-like reaction time, eases in and adds jitter.
- **Lock through walls (ESP)**: also targets characters hidden behind walls.

The bot only reads this game's own target list. It can't see or control other programs.

## Anti-cheat detectors

The anti-cheat sees only what a game server would see: the crosshair position each frame,
shots fired, and where the targets are. Each detection adds to a suspicion score that
decays over time.

| Detector | Trigger |
| --- | --- |
| Snap-to-target | Aim jumps 80+ px in one frame, lands within 3 px of center, then fires within 150 ms |
| Inhuman reaction | Aim is on a target less than 140 ms after it came into view from 60+ px away |
| Robotic tracking | Aim follows a moving target for 600 ms with under 1.5 px of wobble |
| Tracking through walls | Aim follows a hidden target for 450 ms |
| Impossible accuracy | 90%+ hit rate over the last 20 shots |

The Humanized mode is built to slip past most of these checks. Use it as the hard case
when you design better detectors, for example ones based on reaction-time spread or on
how the jitter is distributed.

## Unreal Engine version

[`unreal/`](unreal) has the same bot and anti-cheat as C++ components for an Unreal Engine 5 FPS. The anti-cheat runs on
the server, rewinds enemies by each player's ping, and comes with a test that runs its detection logic against simulated
players. See [`unreal/README.md`](unreal/README.md) for setup.
