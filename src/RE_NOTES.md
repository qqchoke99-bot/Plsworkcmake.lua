# Confirmed RE + raycast integration

## Hook target
- Function start: `0x110CB1D0`
- Channel field: `object + 0x78`
- Position data used by `set3DAttributes`: `object + 0x98`
- `playSound` call: `0x110CB390`

## Raycast
The source now has a real LeviLamina client-API path:

`ll::service::bedrock::getClientInstance()` -> `ClientInstance::getLocalPlayer()` -> `Player::canSee(sourcePos, ShapeType(0))`

This is compiled only when the matching client API headers are available. The public `preloader-android` SDK by itself does not provide those client headers.

## Effect
- clear line of sight: LowPassGain = `1.0`
- blocked line of sight: LowPassGain = `occluded_lowpass_gain` (default `0.25`)

This is single-ray occlusion, not multi-bounce reverb.
