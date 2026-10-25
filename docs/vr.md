# Desktop VR

VR uses OpenXR 1.1 through Vulkan: native on Windows and Linux, MoltenVK
(installed separately) on macOS. The runtime must support primary stereo, opaque
composition and `XR_KHR_vulkan_enable2`; `XR_RUNTIME_JSON`, when set, must name
a valid runtime manifest. The OpenXR loader ships next to the executable.
Programs that use no VR form keep the normal backend (Metal on macOS).

## BASIC forms

Available in both `dbc` and `dbpro`; they add no profile.

| Form | Contract |
| --- | --- |
| `SET VR ENABLED mode` | `0` off (default), `1` on if supported |
| `SET TRACKING SPACE mode` | `0` LOCAL (default), `1` LOCAL_FLOOR, `2` STAGE |
| `RECENTER VR CAMERA` | Cancel head yaw and horizontal offset |
| `GET VR CONTROLLER POSITION hand, vectorId` | Grip world position |
| `GET VR CONTROLLER ROTATION hand, vectorId` | Grip pitch/yaw/roll, degrees |
| `GET VR HEAD POSITION vectorId` | Head world position |
| `GET VR ROTATION vectorId` | Head pitch/yaw/roll, degrees |
| `IS VR CONTROLLER TRACKED(hand)` | `1` if the grip is tracked |
| `IS VR HEAD TRACKED()` | `1` if the head is tracked |
| `IS VR SUPPORTED()` | Loader, runtime, headset and Vulkan binding found at startup |
| `IS VR ENABLED()` | Session active |
| `SET VR PREVIEW MODE mode` | Window mirror: `0` both eyes (default), `1` left, `2` right |
| `SET VR CAMERA HEIGHT height` / `GET VR CAMERA HEIGHT()` | Extra height in metres (default `0`) |

Hands are `0` left and `1` right. `GET` forms create or replace a Vector3, read
with `X VECTOR3`, `Y VECTOR3`, `Z VECTOR3` (also available in Classic for this).

## Behaviour

- One unit is one metre; +X right, +Y up, +Z forward.
- The current camera moves the player: world pose = camera × height × recenter
  × tracked pose. Camera position and angle queries ignore tracking.
- Before the first sample, poses are zero. When tracking is lost, the last pose
  is kept and the tracked query returns `0`.
- Changing the tracking space resets the recenter. An unavailable STAGE keeps
  the previous space and logs a warning.
- Recentering needs a valid head pose. LOCAL also cancels height; LOCAL_FLOOR
  and STAGE keep it.
- If VR cannot start, the program runs flat and `IS VR ENABLED()` stays `0`.
- Screen capture returns the last mirror image (black before the first one).

## Example

```bash
opendbc run examples/open-dbc/vr_tracking.dba --dialect dbc
```

V toggles VR, R recenters, T cycles the tracking space, P the preview, H/J
change the height, arrows move the camera, Escape quits.

## Status

Automated tests use a simulated OpenXR runtime and fake swapchain images. A real
headset has not been qualified yet, nor have Windows and Linux. Deferred:
native Metal XR, standalone Quest, controller buttons and haptics, hand tracking,
passthrough.
