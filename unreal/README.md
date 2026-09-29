# Lock-On Lab for Unreal Engine

Two components for an Unreal Engine 5 FPS: a lock-on bot to test with, and the anti-cheat that tries to catch it.

| Component | Runs on | What it does |
| --- | --- | --- |
| **Lock On Test Bot** | The player's own machine, in development builds only | Locks your aim onto enemies (Snap or Smooth, optionally through walls) and can auto-fire. |
| **Aim Analyzer** | The server | Watches each player's aim, flags patterns that look like a bot, and keeps a suspicion score. |

The bot's logic is compiled out of Shipping builds, and its console commands don't exist there. It can't reach your players, and it only works inside a project you have the source for.

## Files

Everything in [`LockOn/`](LockOn) goes into your game's source folder.

| File | Contents |
| --- | --- |
| `LockOnTestBotComponent.h/.cpp` | The bot |
| `AimAnalyzerComponent.h/.cpp` | The Unreal side of the anti-cheat |
| `AimAnalyzerCore.h/.cpp` | The detection logic, in plain C++ so it can be tested without Unreal |
| `LockOnTargeting.h` | Rules both components share, so they agree on what a target is and where its head is |

## 1. Add the code to your project

1. **If your project is Blueprint-only, turn on C++ first.** In the editor, choose **Tools → New C++ Class**, pick **Actor Component**, name it `LockOnTestBotComponent`, and click **Create Class**. Unreal adds a `Source` folder and builds it. You need Visual Studio with the "Game development with C++" workload.
2. **Close the editor.** Copy the seven files from `LockOn/` into `Source/<YourProject>/`, the folder that holds `<YourProject>.Build.cs`. If the wizard made its own `LockOnTestBotComponent.h` and `.cpp` (sometimes inside `Public/` and `Private/`), delete them first so there's only one copy.
3. **Check `<YourProject>.Build.cs`.** `"InputCore"` must be in `PublicDependencyModuleNames`. New projects already have it.
4. **Build.** Right-click your `.uproject` file and choose **Generate Visual Studio project files**. Open the `.sln`, build **Development Editor**, then open the project.

## 2. Add the components to your character

Open your player character Blueprint (in the first-person template it's `BP_FirstPersonCharacter`). In the **Components** panel, click **Add** and add **Lock On Test Bot**, then add **Aim Analyzer**.

Then connect three things:

- **Auto-fire.** Select Lock On Test Bot. At the bottom of the Details panel, click **+** next to **On Wants To Fire**, and call the same fire function your shoot input calls.
- **Shots.** Wherever your game decides on the server whether a shot hit, call **Aim Analyzer → Report Shot**, with **Hit** set to whether it hit an enemy. For a line-trace weapon, do this right after the trace. For projectiles, do it when the projectile hits something or expires.
- **Enemies.** If your enemies use a different class from your player, set **Target Class** on both components. If dead bodies stay in the level, give them the actor tag `Dead` when they die (or destroy them), so neither component treats them as targets.

## 3. Test it

1. In **Play → Advanced Settings**, set **Number of Players** to 2 and **Net Mode** to **Play As Listen Server**. The analyzer runs on the server. With AI enemies, one player is enough.
2. Hold the **right mouse button** to lock on. Open the console with `~` to switch modes:

   | Command | Effect |
   | --- | --- |
   | `lockon.Mode 1` | Snap: jumps straight onto the target |
   | `lockon.Mode 2` | Smooth: turns toward the target at a steady speed |
   | `lockon.Mode 0` | Off |
   | `lockon.AlwaysOn 1` | Locks on without holding the button |
   | `lockon.ThroughWalls 1` | Also locks onto enemies behind walls |

3. Watch the top-left of the screen for `Aim Analyzer - <player>: suspicion N (Clean)`, plus a red line for each detection. Detections also appear in the Output Log under `LogAimAnalyzer`.

What to try:

| Do this | Expect |
| --- | --- |
| Play normally for a few minutes | Stays **Clean**. If it doesn't, see [Tuning](#tuning). |
| Snap mode on enemies | Snap-to-target and Inhuman reaction |
| Smooth mode | Inhuman reaction. Turn off **Auto Fire** and just follow enemies to get Robotic tracking. |
| Through walls, with enemies moving behind cover | Tracking through walls |
| Any bot mode for 15+ shots | Impossible accuracy |

**Test with lag, too.** In **Editor Preferences → Level Editor → Play → Multiplayer Options**, turn on **Enable Network Emulation**. The analyzer rewinds enemies by each player's ping, so it should still catch the bot while normal play stays Clean.

**On a dedicated server, raise the tick rate.** The analyzer samples aim once per server tick, and the snap check only runs at 40 samples a second or more. Dedicated servers default to 30. Add this to `Config/DefaultEngine.ini`:

```ini
[/Script/OnlineSubsystemUtils.IpNetDriver]
NetServerMaxTickRate=60
```

## What the checks look for

| Check | Flags when | Suspicion |
| --- | --- | --- |
| Snap-to-target | The aim is still, then turns 12°+ in one update, lands dead center on a head, and fires within 150 ms | +18 |
| Inhuman reaction | The aim reaches an enemy less than 140 ms after it appears 10°+ away | +12 |
| Robotic tracking | The aim follows a moving enemy for 0.6 s with almost no wobble | +15 |
| Tracking through walls | The aim follows a hidden, moving enemy for 0.8 s | +25 |
| Impossible accuracy | 90% or more of the last 20 shots hit | +20 |

The score runs from 0 to 100 and cools off by 4 points a second. Under 25 reads as **Clean**, 25 to 59 as **Suspicious**, and 60 or more as **Flagged**. Every check measures angles relative to how big the head looks at that distance, so the checks behave the same up close and far away.

## Using it in a real game

- **The analyzer only flags.** Bind **On Aim Detected** to decide what happens: write a log, save a replay, or mark the player for review. Don't ban on the score alone, because one detection can be a false positive.
- **Accuracy alone can't push anyone past Clean.** Good players on close targets do hit 90%. That check adds 20 points at most every 5 seconds, which the cool-off cancels, so it only adds weight to other signs.
- **To keep the score across respawns**, put the Aim Analyzer on your PlayerController instead of the character. It reads whatever pawn the controller is possessing. Report Shot then needs **Get Controller → Get Component by Class**.
- **Remove the bot component before release.** It does nothing in Shipping builds, but development builds you hand to testers would still have it.

### Tuning

The properties under **Aim Analyzer → Tuning** set each threshold. A good process:

1. Record a few real players on your maps.
2. Raise thresholds until all of them stay Clean.
3. Check that each bot mode still gets caught.

## Limits

- **The snap check turns off below 40 aim updates a second.** At lower rates a fast human flick fits between two updates and looks exactly like a snap. The reaction check still catches snap bots.
- **Ping rewind is approximate.** It uses the player's ping, and Unreal smooths other players' movement on each client. High ping makes the checks less sensitive, not more trigger-happy.
- **These are heuristics.** A player tracking someone through thin cover can occasionally trip the wall check.
- **The human baseline in the tests is simulated.** Real players vary more, so tune with real play.

## Test the detection logic without Unreal

The logic in `AimAnalyzerCore` is plain C++. [`tests/AimAnalyzerCoreTest.cpp`](tests/AimAnalyzerCoreTest.cpp) runs it against simulated players. From the repository root:

```sh
g++ -std=c++17 -O2 -I unreal/LockOn unreal/tests/AimAnalyzerCoreTest.cpp unreal/LockOn/AimAnalyzerCore.cpp -o aimtest && ./aimtest
```

On Windows, from a Developer Command Prompt:

```bat
cl /std:c++17 /EHsc /O2 /I unreal\LockOn unreal\tests\AimAnalyzerCoreTest.cpp unreal\LockOn\AimAnalyzerCore.cpp /Fe:aimtest.exe && aimtest
```

Output:

```
Player                  Time   Hz | Snap Reaction Robotic Wall Accuracy | Shots Hit% Peak sus. | Result
Human (steady)          600s   60 |    0        0       0    0        0 |   404  51%         0 | ok
Human (fast flicks)     600s   60 |    0        0       0    0        2 |   808  47%        20 | ok
Human (steady)          600s   20 |    0        0       0    0        0 |   420  50%         0 | ok
Human (fast flicks)     600s   20 |    0        0       0    0        0 |   795  46%         0 | ok
Bot: Snap                60s   60 |  120      120       0    0       10 |   120 100%       100 | ok
Bot: Snap                60s   20 |    0      120       0    0       10 |   120 100%       100 | ok
Bot: Smooth              60s   60 |    0       99       0    0       10 |    99 100%       100 | ok
Bot: Smooth, no fire     60s   60 |    0       18      24    0        0 |     0   0%       100 | ok
Bot: Smooth + walls      60s   60 |    0        0       0   31        0 |   278  12%       100 | ok
```

Humans must trip none of the first four checks and stay under 25. Each bot must trip the check it was built to trip. Pass a number to try another random seed, such as `./aimtest 42`. All seeds from 100 to 149 pass.

**Status:** The detection logic is tested as shown above. The Unreal components passed a C++ check against stand-in engine headers, but they haven't been compiled inside Unreal yet. If your engine version rejects something, the compiler error will point to the line.
