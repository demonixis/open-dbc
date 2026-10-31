# Changelog

## 0.5.0 - unreleased

First public release.

- Runs DarkBASIC Classic and DarkBASIC Professional programs through a typed
  frontend, verified bytecode and a cooperative VM, rendered with SDL3 GPU
  (Vulkan, Metal).
- One `opendbc` executable: the Studio (code editor, Scene view with snapping, a
  paint tool and the transforms of any program, snippets, project creation,
  Build window) and the command line. The Studio speaks English and French and
  has dark and light themes.
- New projects default to DBPro, and the Scene view edits DBPro projects only;
  levels written for Classic are BASIC and keep running.
- Exports to macOS (Apple silicon), Windows, Linux, Android and iOS with
  `opendbc-runner`.
- Classic multiplayer over ENet, native plugins through `LOAD DLL`, DBPro BSP
  worlds, Classic animations and desktop VR through OpenXR.
- Six demos and tutorials covering the 747 Classic help symbols, with CC0 assets.
- DBPro's `SET BUMP MAPPING ON`, `SET CUBE MAPPING ON` and `SET OBJECT SPECULAR` draw
  through a Blinn-Phong path, with Fresnel reflections and mipmapped cubes; the stage
  forms of `TEXTURE OBJECT` work.
- DBPro cameras draw to images and clip at a plane; objects can be excluded, masked
  per camera, and given their own field of view and depth bias.
- DBPro automatic collision for objects and cameras, fire and snow particles, and
  global shadow colour, shades and light.
