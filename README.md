# Sound Physics Lite v0.8-MVP

## Design (from your RE)

| Item | Value |
|------|--------|
| Hook function body | `0x110CB1D0` (NOT PLT) |
| playSound call site | `0x110CB390` → PLT `0x1298F5A0` |
| Channel* | `[this + 0x78]` after playSound |
| Levi API | `pl::memory::hook(target, detour, &orig, priority)` |

## MVP effect
After hooked function runs → read Channel* → `FMOD_Channel_SetLowPassGain` or CreateDSPByType lowpass + AddDSP.

## Build
1. Clone levilauncher-android-mod-template
2. Replace src with this folder
3. NDK build arm64-v8a → libSoundPhysicsLite.so
4. Zip: manifest.json + libSoundPhysicsLite.so + icon.png → .levipack

## Caution
- Hook signature is simplified (this-only). If game crashes, need full AAPCS args from `afi` / more asm.
- Prefer SetLowPassGain path first (no System*).
