#!/usr/bin/env python3

import argparse
import hashlib
import json
import subprocess
import tempfile
from pathlib import Path

from basic_profiles import load_test_profiles


ROOT = Path(__file__).resolve().parents[1]

UNIT_WITNESSES = (
    ("dbc", "Command", "SET GAMMA", "set gamma 128, 255, 511\n",
     "pixel-capture", "test_engine_builtins.cpp::Classic gamma and mipmap modes drive final pixels and sampler state"),
    ("dbc", "Command", "SET MIPMAP MODE", "set mipmap mode 0\n",
     "sampler-state", "test_engine_builtins.cpp::Classic gamma and mipmap modes drive final pixels and sampler state"),
    ("dbc", "Command", "APPEND OBJECT ANIMATION", "append object animation \"model.x\", \"motion.dat\", 1, 0\n",
     "animation-file", "test_engine_builtins.cpp::Classic imported object animation saves every moving frame and appends losslessly"),
    ("dbc", "Command", "SAVE OBJECT ANIMATION", "save object animation \"motion.dat\", 1\n",
     "animation-file", "test_engine_builtins.cpp::Classic imported object animation saves every moving frame and appends losslessly"),
    ("dbc", "Command", "CLEAR ALL OBJECT KEYFRAMES", "clear all object keyframes 1\n",
     "animation-state", "test_engine_builtins.cpp::Classic authored object keyframes round-trip through the portable animation format"),
    ("dbc", "Command", "CLEAR OBJECT KEYFRAME", "clear object keyframe 1, 10\n",
     "animation-state", "test_engine_builtins.cpp::Classic authored object keyframes round-trip through the portable animation format"),
    ("dbc", "Command", "SET OBJECT KEYFRAME", "set object keyframe 1, 10\n",
     "animation-state", "test_engine_builtins.cpp::Classic authored object keyframes round-trip through the portable animation format"),
    ("dbc", "Command", "ATTACH OBJECT TO STATIC", "attach object to static 1\n",
     "static-world", "test_engine_builtins.cpp::Classic portable static world, line of sight, and global collision have observable state"),
    ("dbc", "Command", "DETACH OBJECT FROM STATIC", "detach object from static 1\n",
     "static-world", "test_engine_builtins.cpp::Classic portable static world, line of sight, and global collision have observable state"),
    ("dbc", "Command", "ENABLE STATIC OCCLUSION", "enable static occlusion\n",
     "static-frustum", "test_engine_builtins.cpp::Classic portable static world, line of sight, and global collision have observable state"),
    ("dbc", "Command", "DISABLE STATIC OCCLUSION", "disable static occlusion\n",
     "static-frustum", "test_engine_builtins.cpp::Classic portable static world, line of sight, and global collision have observable state"),
    ("dbc", "Command", "SET STATIC OBJECTS TEXTURE", "set static objects texture 2\n",
     "static-render-state", "test_engine_builtins.cpp::Classic portable static world, line of sight, and global collision have observable state"),
    ("dbc", "Command", "SET STATIC OBJECTS WIREFRAME ON", "set static objects wireframe on\n",
     "static-render-state", "test_engine_builtins.cpp::Classic portable static world, line of sight, and global collision have observable state"),
    ("dbc", "Command", "SET STATIC OBJECTS WIREFRAME OFF", "set static objects wireframe off\n",
     "static-render-state", "test_engine_builtins.cpp::Classic portable static world, line of sight, and global collision have observable state"),
    ("dbc", "Command", "SAVE STATIC OBJECTS", "save static objects \"world.dat\"\n",
     "static-file", "test_engine_builtins.cpp::Classic portable static world, line of sight, and global collision have observable state"),
    ("dbc", "Command", "LOAD STATIC OBJECTS", "load static objects \"world.dat\"\n",
     "static-file", "test_engine_builtins.cpp::Classic portable static world, line of sight, and global collision have observable state"),
    ("dbc", "Command", "SET GLOBAL COLLISION ON", "set global collision on\n",
     "collision-state", "test_engine_builtins.cpp::Classic portable static world, line of sight, and global collision have observable state"),
    ("dbc", "Command", "SET GLOBAL COLLISION OFF", "set global collision off\n",
     "collision-state", "test_engine_builtins.cpp::Classic portable static world, line of sight, and global collision have observable state"),
    ("dbc", "Function", "STATIC LINE OF SIGHT", "value = static line of sight(0, 0, 0, 1, 1, 1, 0, 1)\n",
     "static-collision", "test_engine_builtins.cpp::Classic portable static world, line of sight, and global collision have observable state"),
    ("dbc", "Function", "STATIC LINE OF SIGHT X", "value = static line of sight x()\n",
     "static-collision", "test_engine_builtins.cpp::Classic portable static world, line of sight, and global collision have observable state"),
    ("dbc", "Function", "STATIC LINE OF SIGHT Y", "value = static line of sight y()\n",
     "static-collision", "test_engine_builtins.cpp::Classic portable static world, line of sight, and global collision have observable state"),
    ("dbc", "Function", "STATIC LINE OF SIGHT Z", "value = static line of sight z()\n",
     "static-collision", "test_engine_builtins.cpp::Classic portable static world, line of sight, and global collision have observable state"),
    ("dbc", "Command", "FORCE SHOOT", "force shoot 100, 100\n",
     "input-haptic-no-device", "test_engine_builtins.cpp::SDL virtual joysticks expose the DBPro control device model"),
    ("dbpro", "Command", "FORCE SHOOT", "force shoot 100, 100\n",
     "input-haptic-no-device", "test_engine_builtins.cpp::SDL virtual joysticks expose the DBPro control device model"),
    ("dbpro", "Command", "SET BUMP MAPPING ON", "set bump mapping on 1, 2\n",
     "material-pixel", "test_engine_builtins.cpp::DBPro bump cartoon and rainbow effects reach the portable stock shader"),
    ("dbpro", "Command", "SET CARTOON SHADING ON", "set cartoon shading on 1, 2, 3\n",
     "material-pixel", "test_engine_builtins.cpp::DBPro bump cartoon and rainbow effects reach the portable stock shader"),
    ("dbpro", "Command", "SET RAINBOW SHADING ON", "set rainbow shading on 1, 2\n",
     "material-pixel", "test_engine_builtins.cpp::DBPro bump cartoon and rainbow effects reach the portable stock shader"),
    ("dbpro", "Command", "SET BLEND MAPPING ON", "set blend mapping on 1, 2, 10\n",
     "material-pixel", "test_engine_builtins.cpp::DBPro blend operations and cube faces reach the portable stock shader"),
    ("dbpro", "Command", "SET BLEND MAPPING ON", "set blend mapping on 1, 7, 3, 3, 2\n",
     "material-pixel", "test_engine_builtins.cpp::DBPro blend operations and cube faces reach the portable stock shader"),
    ("dbpro", "Command", "SET CUBE MAPPING ON", "set cube mapping on 2, 3, 3, 3, 3, 3, 3\n",
     "material-pixel", "test_engine_builtins.cpp::DBPro blend operations and cube faces reach the portable stock shader"),
    ("dbpro", "Command", "SET DETAIL MAPPING ON", "set detail mapping on 1, 2\n",
     "material-pixel", "test_engine_builtins.cpp::Portable normal normalization and DBPro auxiliary texture stages reach the stock shader"),
    ("dbpro", "Command", "SET LIGHT MAPPING ON", "set light mapping on 1, 2, 1\n",
     "material-pixel", "test_engine_builtins.cpp::Portable normal normalization and DBPro auxiliary texture stages reach the stock shader"),
    ("dbc", "Command", "SET NORMALIZATION OFF", "set normalization off\n",
     "render-state", "test_engine_builtins.cpp::Portable normal normalization and DBPro auxiliary texture stages reach the stock shader"),
    ("dbpro", "Command", "SET NORMALIZATION OFF", "set normalization off\n",
     "render-state", "test_engine_builtins.cpp::Portable normal normalization and DBPro auxiliary texture stages reach the stock shader"),
    ("dbc", "Command", "SET NORMALIZATION ON", "set normalization on\n",
     "render-state", "test_engine_builtins.cpp::Portable normal normalization and DBPro auxiliary texture stages reach the stock shader"),
    ("dbpro", "Command", "SET NORMALIZATION ON", "set normalization on\n",
     "render-state", "test_engine_builtins.cpp::Portable normal normalization and DBPro auxiliary texture stages reach the stock shader"),
    ("dbpro", "Command", "SET SPHERE MAPPING ON", "set sphere mapping on 1, 2\n",
     "material-pixel", "test_engine_builtins.cpp::Portable normal normalization and DBPro auxiliary texture stages reach the stock shader"),
    ("dbc", "Function", "OBJECT IN SCREEN", "value = object in screen(1)\n",
     "projection-state", "test_engine_builtins.cpp::Portable object visibility and GPU capability queries preserve DBPro semantics"),
    ("dbpro", "Function", "OBJECT IN SCREEN", "value = object in screen(1)\n",
     "projection-state", "test_engine_builtins.cpp::Portable object visibility and GPU capability queries preserve DBPro semantics"),
    ("dbc", "Function", "OBJECT SCREEN X", "value = object screen x(1)\n",
     "projection-state", "test_engine_builtins.cpp::Portable object visibility and GPU capability queries preserve DBPro semantics"),
    ("dbpro", "Function", "OBJECT SCREEN X", "value = object screen x(1)\n",
     "projection-state", "test_engine_builtins.cpp::Portable object visibility and GPU capability queries preserve DBPro semantics"),
    ("dbc", "Function", "OBJECT SCREEN Y", "value = object screen y(1)\n",
     "projection-state", "test_engine_builtins.cpp::Portable object visibility and GPU capability queries preserve DBPro semantics"),
    ("dbpro", "Function", "OBJECT SCREEN Y", "value = object screen y(1)\n",
     "projection-state", "test_engine_builtins.cpp::Portable object visibility and GPU capability queries preserve DBPro semantics"),
    ("dbc", "Function", "TNL AVAILABLE", "value = tnl available()\n",
     "gpu-capability", "test_engine_builtins.cpp::Portable object visibility and GPU capability queries preserve DBPro semantics"),
    ("dbpro", "Function", "TNL AVAILABLE", "value = tnl available()\n",
     "gpu-capability", "test_engine_builtins.cpp::Portable object visibility and GPU capability queries preserve DBPro semantics"),
    ("dbpro", "Function", "GET MAXIMUM PIXEL SHADER VERSION", "value = get maximum pixel shader version()\n",
     "gpu-capability", "test_engine_builtins.cpp::Portable object visibility and GPU capability queries preserve DBPro semantics"),
    ("dbpro", "Function", "GET MAXIMUM VERTEX SHADER VERSION", "value = get maximum vertex shader version()\n",
     "gpu-capability", "test_engine_builtins.cpp::Portable object visibility and GPU capability queries preserve DBPro semantics"),
    ("dbpro", "Function", "STATISTIC", "value = statistic(1)\n",
     "render-state", "test_engine_builtins.cpp::Portable object visibility and GPU capability queries preserve DBPro semantics"),
    ("dbc", "Command", "COLOR LIMB", "color limb 2, 1, rgb(18, 52, 86)\n",
     "render-state", "test_engine_builtins.cpp::Portable limb checklist colour and texture transforms preserve stage semantics"),
    ("dbpro", "Command", "COLOR LIMB", "color limb 2, 1, rgb(18, 52, 86)\n",
     "render-state", "test_engine_builtins.cpp::Portable limb checklist colour and texture transforms preserve stage semantics"),
    ("dbc", "Command", "PERFORM CHECKLIST FOR OBJECT LIMBS", "perform checklist for object limbs 2\n",
     "checklist-state", "test_engine_builtins.cpp::Portable limb checklist colour and texture transforms preserve stage semantics"),
    ("dbpro", "Command", "PERFORM CHECKLIST FOR OBJECT LIMBS", "perform checklist for object limbs 2\n",
     "checklist-state", "test_engine_builtins.cpp::Portable limb checklist colour and texture transforms preserve stage semantics"),
    ("dbc", "Command", "SCROLL LIMB TEXTURE", "scroll limb texture 2, 1, 0.25, 0.5\n",
     "render-state", "test_engine_builtins.cpp::Portable limb checklist colour and texture transforms preserve stage semantics"),
    ("dbpro", "Command", "SCROLL LIMB TEXTURE", "scroll limb texture 2, 1, 0.25, 0.5\n",
     "render-state", "test_engine_builtins.cpp::Portable limb checklist colour and texture transforms preserve stage semantics"),
    ("dbpro", "Command", "SCROLL LIMB TEXTURE", "scroll limb texture 2, 1, 2, 0.5, 0.25\n",
     "render-state", "test_engine_builtins.cpp::Portable limb checklist colour and texture transforms preserve stage semantics"),
    ("dbc", "Command", "SCALE LIMB TEXTURE", "scale limb texture 2, 1, 2.0, 3.0\n",
     "render-state", "test_engine_builtins.cpp::Portable limb checklist colour and texture transforms preserve stage semantics"),
    ("dbpro", "Command", "SCALE LIMB TEXTURE", "scale limb texture 2, 1, 2.0, 3.0\n",
     "render-state", "test_engine_builtins.cpp::Portable limb checklist colour and texture transforms preserve stage semantics"),
    ("dbpro", "Command", "SCALE LIMB TEXTURE", "scale limb texture 2, 1, 2, 2.0, 4.0\n",
     "render-state", "test_engine_builtins.cpp::Portable limb checklist colour and texture transforms preserve stage semantics"),
    ("dbpro", "Function", "TIMER", "value = timer()\n",
     "timing-state", "test_engine_builtins.cpp::Portable limb checklist colour and texture transforms preserve stage semantics"),
    ("dbc", "Command", "DRAW TO BACK", "draw to back\n",
     "pixel-capture", "test_engine_builtins.cpp::DRAW TO BACK and DRAW TO FRONT place every 2D batch around the 3D world"),
    ("dbc", "Command", "DRAW TO FRONT", "draw to front\n",
     "pixel-capture", "test_engine_builtins.cpp::DRAW TO BACK and DRAW TO FRONT place every 2D batch around the 3D world"),
    ("dbpro", "Command", "DRAW TO BACK", "draw to back\n",
     "pixel-capture", "test_engine_builtins.cpp::DRAW TO BACK and DRAW TO FRONT place every 2D batch around the 3D world"),
    ("dbpro", "Command", "DRAW TO FRONT", "draw to front\n",
     "pixel-capture", "test_engine_builtins.cpp::DRAW TO BACK and DRAW TO FRONT place every 2D batch around the 3D world"),
    ("dbc", "Command", "BACKDROP OFF", "backdrop off\n",
     "engine-state", "test_engine_builtins.cpp::Classic backdrop and fog commands update draw state"),
    ("dbc", "Command", "BACKDROP ON", "backdrop on\n",
     "engine-state", "test_engine_builtins.cpp::Classic backdrop and fog commands update draw state"),
    ("dbc", "Command", "COLOR BACKDROP", "color backdrop rgb(16, 32, 48)\n",
     "engine-state", "test_engine_builtins.cpp::Classic backdrop and fog commands update draw state"),
    ("dbc", "Command", "SCROLL BACKDROP", "scroll backdrop 25, -50\n",
     "engine-state", "test_engine_builtins.cpp::Classic backdrop and fog commands update draw state"),
    ("dbc", "Command", "TEXTURE BACKDROP", "texture backdrop 17\n",
     "engine-state", "test_engine_builtins.cpp::Classic backdrop and fog commands update draw state"),
    ("dbc", "Function", "ALPHABLENDING AVAILABLE", "value = alphablending available()\n",
     "gpu-capability", "test_engine_builtins.cpp::Classic backdrop and fog commands update draw state"),
    ("dbc", "Command", "FOG ON", "fog on\n",
     "engine-state", "test_engine_builtins.cpp::Classic backdrop and fog commands update draw state"),
    ("dbc", "Command", "FOG COLOR", "fog color rgb(64, 80, 96)\n",
     "engine-state", "test_engine_builtins.cpp::Classic backdrop and fog commands update draw state"),
    ("dbc", "Command", "FOG DISTANCE", "fog distance 750\n",
     "engine-state", "test_engine_builtins.cpp::Classic backdrop and fog commands update draw state"),
    ("dbc", "Command", "FOG OFF", "fog off\n",
     "engine-state", "test_engine_builtins.cpp::Classic backdrop and fog commands update draw state"),
    ("dbc", "Command", "INK", "ink rgb(17, 34, 51), 0\n",
     "engine-state", "test_engine_builtins.cpp::Classic primitive and text commands queue observable frame state"),
    ("dbc", "Command", "DOT", "dot 1, 2\n",
     "engine-state", "test_engine_builtins.cpp::Classic primitive and text commands queue observable frame state"),
    ("dbc", "Command", "LINE", "line 3, 4, 5, 6\n",
     "engine-state", "test_engine_builtins.cpp::Classic primitive and text commands queue observable frame state"),
    ("dbc", "Command", "CIRCLE", "circle 7, 8, 9\n",
     "engine-state", "test_engine_builtins.cpp::Classic primitive and text commands queue observable frame state"),
    ("dbc", "Command", "TEXT", "text 10, 11, \"left\"\n",
     "engine-state", "test_engine_builtins.cpp::Classic primitive and text commands queue observable frame state"),
    ("dbc", "Command", "CENTER TEXT", "center text 20, 21, \"center\"\n",
     "engine-state", "test_engine_builtins.cpp::Classic primitive and text commands queue observable frame state"),
    ("dbpro", "Command", "COLOR BACKDROP", "color backdrop 0, rgb(32, 48, 64)\n",
     "engine-state", "test_engine_builtins.cpp::DBPro backdrop and ink overloads preserve their profile-specific call shape"),
    ("dbpro", "Command", "INK", "ink rgb(80, 96, 112), 0\n",
     "engine-state", "test_engine_builtins.cpp::DBPro backdrop and ink overloads preserve their profile-specific call shape"),
    ("dbpro", "Command", "COLOR OBJECT", "color object 1, rgb(32, 64, 128)\n",
     "engine-state", "test_engine_builtins.cpp::Object material ghost and rotation commands update render state"),
    ("dbpro", "Command", "FADE OBJECT", "fade object 1, 50\n",
     "engine-state", "test_engine_builtins.cpp::Object material ghost and rotation commands update render state"),
    ("dbpro", "Command", "GHOST OBJECT ON", "ghost object on 1, 1\n",
     "engine-state", "test_engine_builtins.cpp::Object material ghost and rotation commands update render state"),
    ("dbpro", "Command", "GHOST OBJECT OFF", "ghost object off 1\n",
     "engine-state", "test_engine_builtins.cpp::Object material ghost and rotation commands update render state"),
    ("dbpro", "Command", "SET OBJECT ROTATION ZYX", "set object rotation zyx 1\n",
     "engine-state", "test_engine_builtins.cpp::Object material ghost and rotation commands update render state"),
    ("dbc", "Command", "ALWAYS ACTIVE OFF", "always active off\n",
     "vm-state", "test_engine_builtins.cpp::ALWAYS ACTIVE commands control execution while the host is suspended"),
    ("dbc", "Command", "SCALE OBJECT TEXTURE", "scale object texture 1, 1.0, 1.0\n",
     "engine-state", "test_engine_builtins.cpp::Object texture scale and SET OBJECT defaults match BASIC semantics"),
    ("dbc", "Command", "SCALE OBJECT", "scale object 1, 125, 75, 50\n",
     "engine-state", "test_engine_builtins.cpp::Object texture scale and SET OBJECT defaults match BASIC semantics"),
    ("dbc", "Command", "TEXTURE OBJECT", "texture object 1, 9\n",
     "engine-state", "test_engine_builtins.cpp::Object texture scale and SET OBJECT defaults match BASIC semantics"),
    ("dbc", "Command", "SET OBJECT", "set object 1, 0, 0, 1\n",
     "engine-state", "test_engine_builtins.cpp::Object texture scale and SET OBJECT defaults match BASIC semantics"),
    ("dbc", "Command", "SET OBJECT TEXTURE", "set object texture 1, 2, 1\n",
     "render-state", "test_engine_builtins.cpp::Object texture scale and SET OBJECT defaults match BASIC semantics"),
    ("dbpro", "Command", "SCALE OBJECT TEXTURE", "scale object texture 1, 2.0, 3.0\n",
     "render-state", "test_engine_builtins.cpp::DBPro object depth UV alpha and bounds controls drive portable rendering"),
    ("dbpro", "Command", "SCALE OBJECT TEXTURE", "scale object texture 1, 1, 4.0, 5.0\n",
     "render-state", "test_engine_builtins.cpp::DBPro object depth UV alpha and bounds controls drive portable rendering"),
    ("dbpro", "Command", "DISABLE OBJECT ZREAD", "disable object zread 1\n",
     "render-state", "test_engine_builtins.cpp::DBPro object depth UV alpha and bounds controls drive portable rendering"),
    ("dbpro", "Command", "ENABLE OBJECT ZREAD", "enable object zread 1\n",
     "render-state", "test_engine_builtins.cpp::DBPro object depth UV alpha and bounds controls drive portable rendering"),
    ("dbpro", "Command", "DISABLE OBJECT ZWRITE", "disable object zwrite 1\n",
     "render-state", "test_engine_builtins.cpp::DBPro object depth UV alpha and bounds controls drive portable rendering"),
    ("dbpro", "Command", "ENABLE OBJECT ZWRITE", "enable object zwrite 1\n",
     "render-state", "test_engine_builtins.cpp::DBPro object depth UV alpha and bounds controls drive portable rendering"),
    ("dbpro", "Command", "SET ALPHA MAPPING ON", "set alpha mapping on 1, 37.5\n",
     "pixel-capture", "test_engine_builtins.cpp::DBPro object depth UV alpha and bounds controls drive portable rendering"),
    ("dbpro", "Command", "SHOW OBJECT BOUNDS", "show object bounds 1\n",
     "render-state", "test_engine_builtins.cpp::DBPro object depth UV alpha and bounds controls drive portable rendering"),
    ("dbpro", "Command", "SHOW OBJECT BOUNDS", "show object bounds 1, 1\n",
     "render-state", "test_engine_builtins.cpp::DBPro object depth UV alpha and bounds controls drive portable rendering"),
    ("dbpro", "Command", "HIDE OBJECT BOUNDS", "hide object bounds 1\n",
     "render-state", "test_engine_builtins.cpp::DBPro object depth UV alpha and bounds controls drive portable rendering"),
    ("dbpro", "Command", "SET OBJECT", "set object 1, 0, 3, 1, 0, 0, 0, 0\n",
     "render-state", "test_engine_builtins.cpp::DBPro transparency modes preserve render phases depth writes and alpha testing"),
    ("dbpro", "Command", "SET OBJECT TRANSPARENCY", "set object transparency 1, 6\n",
     "pixel-capture", "test_engine_builtins.cpp::DBPro transparency modes preserve render phases depth writes and alpha testing"),
    ("dbc", "Command", "LOAD IMAGE", "load image \"fixture.png\", 1\n",
     "runtime-error", "test_engine_builtins.cpp::Asset load failures become runtime errors and leave no resource slot"),
    ("dbc", "Command", "LOAD BITMAP", "load bitmap \"fixture.png\", 1\n",
     "runtime-error", "test_engine_builtins.cpp::Asset load failures become runtime errors and leave no resource slot"),
    ("dbc", "Command", "COLOR LIGHT", "color light 7, rgb(16, 32, 48)\n",
     "engine-state", "test_engine_builtins.cpp::Both Classic COLOR LIGHT overloads update the renderer light state"),
    ("dbc", "Command", "COLOR LIGHT", "color light 7, 12, 34, 56\n",
     "engine-state", "test_engine_builtins.cpp::Both Classic COLOR LIGHT overloads update the renderer light state"),
    ("dbc", "Command", "COLOR AMBIENT LIGHT", "color ambient light rgb(17, 34, 51)\n",
     "engine-state", "test_engine_builtins.cpp::Classic light commands update visibility direction position and ambient state"),
    ("dbc", "Command", "SET DIRECTIONAL LIGHT", "set directional light 4, 1, -2, 3\n",
     "engine-state", "test_engine_builtins.cpp::Classic light commands update visibility direction position and ambient state"),
    ("dbc", "Command", "POSITION LIGHT", "position light 4, 10, 20, 30\n",
     "engine-state", "test_engine_builtins.cpp::Classic light commands update visibility direction position and ambient state"),
    ("dbc", "Command", "HIDE LIGHT", "hide light 4\n",
     "engine-state", "test_engine_builtins.cpp::Classic light commands update visibility direction position and ambient state"),
    ("dbc", "Command", "SHOW LIGHT", "show light 4\n",
     "engine-state", "test_engine_builtins.cpp::Classic light commands update visibility direction position and ambient state"),
    ("dbpro", "Command", "SET SPRITE PRIORITY", "set sprite priority 1, 5\n",
     "render-order", "test_engine_builtins.cpp::Sprite priority determines the renderer traversal order"),
    ("dbc", "Command", "SET SPRITE", "set sprite 1, 0, 0\n",
     "render-state", "test_engine_builtins.cpp::Sprite priority determines the renderer traversal order"),
    ("dbc", "Command", "OFFSET SPRITE", "offset sprite 1, 7, 8\n",
     "render-state", "test_engine_builtins.cpp::Sprite priority determines the renderer traversal order"),
    ("dbc", "Command", "SIZE SPRITE", "size sprite 1, 21, 22\n",
     "render-state", "test_engine_builtins.cpp::Sprite priority determines the renderer traversal order"),
    ("dbc", "Command", "PASTE SPRITE", "paste sprite 1, 31, 32\n",
     "render-state", "test_engine_builtins.cpp::Sprite priority determines the renderer traversal order"),
    ("dbc", "Function", "SPRITE IMAGE", "value = sprite image(1)\n",
     "engine-state", "test_engine_builtins.cpp::Sprite priority determines the renderer traversal order"),
    ("dbc", "Function", "SCANCODE", "value = scancode()\n",
     "input-map", "test_engine_builtins.cpp::DarkBASIC scan codes retain the complete DirectInput key layout"),
    ("dbpro", "Function", "SCANCODE", "value = scancode()\n",
     "input-map", "test_engine_builtins.cpp::DarkBASIC scan codes retain the complete DirectInput key layout"),
    ("dbpro", "Command", "DATA", "data 1, \"value\"\n",
     "vm-state", "test_vm.cpp::DATA READ and RESTORE use the typed module pool"),
    ("dbpro", "Command", "READ", "read value\n",
     "vm-state", "test_vm.cpp::DATA READ and RESTORE use the typed module pool"),
    ("dbpro", "Command", "RESTORE", "again:\ndata 1\nrestore again\n",
     "vm-state", "test_vm.cpp::DATA READ and RESTORE use the typed module pool"),
    ("dbpro", "Command", "READ LONG", "read long 1, value\n",
     "vm-state", "test_vm.cpp::Output commands store typed results and whole-array calls are explicit"),
    ("dbpro", "Command", "SAVE ARRAY", "dim values(1) as integer\nsave array \"values.bin\", values(0)\n",
     "file", "test_vm.cpp::Output commands store typed results and whole-array calls are explicit"),
    ("dbpro", "Command", "LOAD ARRAY", "dim values(1) as integer\nload array \"values.bin\", values(0)\n",
     "vm-state", "test_vm.cpp::Output commands store typed results and whole-array calls are explicit"),
    ("dbpro", "Command", "UNDIM", "dim values(1) as integer\nundim values()\n",
     "vm-state", "test_vm.cpp::Output commands store typed results and whole-array calls are explicit"),

    ("dbc", "Command", "ALWAYS ACTIVE ON", "always active on\n",
     "vm-state", "test_engine_builtins.cpp::ALWAYS ACTIVE commands control execution while the host is suspended"),
    ("dbc", "Command", "SLEEP", "sleep 0\n",
     "vm-state", "test_engine_builtins.cpp::SLEEP and key waits suspend resume and cancel cooperatively"),
    ("dbpro", "Command", "SLEEP", "sleep 0\n",
     "vm-state", "test_engine_builtins.cpp::SLEEP and key waits suspend resume and cancel cooperatively"),
    ("dbc", "Command", "SUSPEND FOR KEY", "suspend for key\n",
     "vm-state", "test_engine_builtins.cpp::SLEEP and key waits suspend resume and cancel cooperatively"),
    ("dbpro", "Command", "SUSPEND FOR KEY", "suspend for key\n",
     "vm-state", "test_engine_builtins.cpp::SLEEP and key waits suspend resume and cancel cooperatively"),
    ("dbpro", "Command", "WAIT KEY", "wait key\n",
     "vm-state", "test_engine_builtins.cpp::SLEEP and key waits suspend resume and cancel cooperatively"),
    ("dbc", "Command", "SYNC OFF", "sync off\n",
     "engine-state", "test_engine_builtins.cpp::Text compatibility commands and functions expose observable state in both profiles"),
    ("dbpro", "Command", "SYNC OFF", "sync off\n",
     "engine-state", "test_engine_builtins.cpp::Text compatibility commands and functions expose observable state in both profiles"),

    ("dbc", "Function", "MOUSECLICK", "value = mouseclick()\n",
     "input-state", "test_engine_builtins.cpp::Classic input functions preserve held keys absolute mouse state and button masks"),
    ("dbpro", "Function", "MOUSECLICK", "value = mouseclick()\n",
     "input-state", "test_engine_builtins.cpp::Classic input functions preserve held keys absolute mouse state and button masks"),
    ("dbc", "Function", "MOUSEX", "value = mousex()\n",
     "input-state", "test_engine_builtins.cpp::Classic input functions preserve held keys absolute mouse state and button masks"),
    ("dbpro", "Function", "MOUSEX", "value = mousex()\n",
     "input-state", "test_engine_builtins.cpp::Classic input functions preserve held keys absolute mouse state and button masks"),
    ("dbc", "Function", "MOUSEY", "value = mousey()\n",
     "input-state", "test_engine_builtins.cpp::Classic input functions preserve held keys absolute mouse state and button masks"),
    ("dbpro", "Function", "MOUSEY", "value = mousey()\n",
     "input-state", "test_engine_builtins.cpp::Classic input functions preserve held keys absolute mouse state and button masks"),
    ("dbc", "Function", "UPKEY", "value = upkey()\n",
     "input-state", "test_engine_builtins.cpp::Classic input functions preserve held keys absolute mouse state and button masks"),
    ("dbpro", "Function", "UPKEY", "value = upkey()\n",
     "input-state", "test_engine_builtins.cpp::Classic input functions preserve held keys absolute mouse state and button masks"),
    ("dbc", "Function", "DOWNKEY", "value = downkey()\n",
     "input-state", "test_engine_builtins.cpp::Classic input functions preserve held keys absolute mouse state and button masks"),
    ("dbpro", "Function", "DOWNKEY", "value = downkey()\n",
     "input-state", "test_engine_builtins.cpp::Classic input functions preserve held keys absolute mouse state and button masks"),
    ("dbc", "Function", "LEFTKEY", "value = leftkey()\n",
     "input-state", "test_engine_builtins.cpp::Classic input functions preserve held keys absolute mouse state and button masks"),
    ("dbpro", "Function", "LEFTKEY", "value = leftkey()\n",
     "input-state", "test_engine_builtins.cpp::Classic input functions preserve held keys absolute mouse state and button masks"),
    ("dbc", "Function", "RIGHTKEY", "value = rightkey()\n",
     "input-state", "test_engine_builtins.cpp::Classic input functions preserve held keys absolute mouse state and button masks"),
    ("dbpro", "Function", "RIGHTKEY", "value = rightkey()\n",
     "input-state", "test_engine_builtins.cpp::Classic input functions preserve held keys absolute mouse state and button masks"),
    ("dbc", "Function", "SPACEKEY", "value = spacekey()\n",
     "input-state", "test_engine_builtins.cpp::Classic input functions preserve held keys absolute mouse state and button masks"),
    ("dbpro", "Function", "SPACEKEY", "value = spacekey()\n",
     "input-state", "test_engine_builtins.cpp::Classic input functions preserve held keys absolute mouse state and button masks"),
    ("dbc", "Function", "RETURNKEY", "value = returnkey()\n",
     "input-state", "test_engine_builtins.cpp::Classic input functions preserve held keys absolute mouse state and button masks"),
    ("dbpro", "Function", "RETURNKEY", "value = returnkey()\n",
     "input-state", "test_engine_builtins.cpp::Classic input functions preserve held keys absolute mouse state and button masks"),
    ("dbc", "Function", "INKEY$", "value$ = inkey$()\n",
     "input-state", "test_engine_builtins.cpp::Classic input functions preserve held keys absolute mouse state and button masks"),
    ("dbpro", "Function", "INKEY$", "value$ = inkey$()\n",
     "input-state", "test_engine_builtins.cpp::Classic input functions preserve held keys absolute mouse state and button masks"),
    ("dbc", "Command", "HIDE MOUSE", "hide mouse\n",
     "cursor-state", "test_engine_builtins.cpp::Text compatibility commands and functions expose observable state in both profiles"),
    ("dbpro", "Command", "HIDE MOUSE", "hide mouse\n",
     "cursor-state", "test_engine_builtins.cpp::Text compatibility commands and functions expose observable state in both profiles"),
    ("dbc", "Command", "SHOW MOUSE", "show mouse\n",
     "cursor-state", "test_engine_builtins.cpp::Text compatibility commands and functions expose observable state in both profiles"),
    ("dbpro", "Command", "SHOW MOUSE", "show mouse\n",
     "cursor-state", "test_engine_builtins.cpp::Text compatibility commands and functions expose observable state in both profiles"),

    ("dbpro", "Command", "TEXT", "text 1, 2, \"value\"\n",
     "render-state", "test_engine_builtins.cpp::Text compatibility commands and functions expose observable state in both profiles"),
    ("dbpro", "Command", "CENTER TEXT", "center text 1, 2, \"value\"\n",
     "render-state", "test_engine_builtins.cpp::Text compatibility commands and functions expose observable state in both profiles"),
    ("dbpro", "Command", "SET TEXT FONT", "set text font \"Arial\"\n",
     "text-state", "test_engine_builtins.cpp::Text compatibility commands and functions expose observable state in both profiles"),
    ("dbpro", "Command", "SET TEXT SIZE", "set text size 22\n",
     "text-state", "test_engine_builtins.cpp::Text compatibility commands and functions expose observable state in both profiles"),
    ("dbpro", "Command", "SET TEXT TO BOLD", "set text to bold\n",
     "text-state", "test_engine_builtins.cpp::Text compatibility commands and functions expose observable state in both profiles"),
    ("dbpro", "Function", "TEXT WIDTH", "value = text width(\"abc\")\n",
     "render-state", "test_engine_builtins.cpp::Text compatibility commands and functions expose observable state in both profiles"),
    ("dbpro", "Function", "STR$", "value$ = str$(3.5, 2)\n",
     "value", "test_engine_builtins.cpp::Text compatibility commands and functions expose observable state in both profiles"),
    ("dbpro", "Function", "LEN", "value = len(\"abc\")\n",
     "value", "test_engine_builtins.cpp::Text compatibility commands and functions expose observable state in both profiles"),
    ("dbpro", "Function", "MID$", "value$ = mid$(\"abc\", 2)\n",
     "value", "test_engine_builtins.cpp::Text compatibility commands and functions expose observable state in both profiles"),
    ("dbpro", "Function", "UPPER$", "value$ = upper$(\"abc\")\n",
     "value", "test_engine_builtins.cpp::Text compatibility commands and functions expose observable state in both profiles"),
    ("dbpro", "Function", "SPACE$", "value$ = space$(3)\n",
     "value", "test_engine_builtins.cpp::Text compatibility commands and functions expose observable state in both profiles"),
    ("dbpro", "Function", "ASC", "value = asc(\"A\")\n",
     "value", "test_engine_builtins.cpp::Text compatibility commands and functions expose observable state in both profiles"),
    ("dbc", "Command", "SET TEXT TRANSPARENT", "set text transparent\n",
     "text-state", "test_engine_builtins.cpp::Text compatibility commands and functions expose observable state in both profiles"),
    ("dbpro", "Command", "SET TEXT TRANSPARENT", "set text transparent\n",
     "text-state", "test_engine_builtins.cpp::Text compatibility commands and functions expose observable state in both profiles"),
    ("dbc", "Command", "SET CURSOR", "set cursor 1, 2\n",
     "text-state", "test_engine_builtins.cpp::Text compatibility commands and functions expose observable state in both profiles"),
    ("dbpro", "Command", "SET CURSOR", "set cursor 1, 2\n",
     "text-state", "test_engine_builtins.cpp::Text compatibility commands and functions expose observable state in both profiles"),

    ("dbc", "Command", "COPY FILE", "copy file \"a\", \"b\"\n",
     "file", "test_engine_builtins.cpp::COPY FILE never overwrites and reports source and destination failures"),
    ("dbpro", "Command", "COPY FILE", "copy file \"a\", \"b\"\n",
     "file", "test_engine_builtins.cpp::COPY FILE never overwrites and reports source and destination failures"),
    ("dbpro", "Command", "RANDOMIZE", "randomize 123\n",
     "rng-state", "test_engine_builtins.cpp::DBPro math and screen compatibility functions retain deterministic values"),
    ("dbpro", "Function", "RND", "value = rnd(10)\n",
     "value", "test_engine_builtins.cpp::DBPro math and screen compatibility functions retain deterministic values"),
    ("dbpro", "Function", "WRAPVALUE", "value = wrapvalue(370)\n",
     "value", "test_engine_builtins.cpp::DBPro math and screen compatibility functions retain deterministic values"),
    ("dbpro", "Function", "COS", "value = cos(60)\n",
     "value", "test_engine_builtins.cpp::DBPro math and screen compatibility functions retain deterministic values"),
    ("dbpro", "Function", "SIN", "value = sin(30)\n",
     "value", "test_engine_builtins.cpp::DBPro math and screen compatibility functions retain deterministic values"),
    ("dbpro", "Function", "SCREEN WIDTH", "value = screen width()\n",
     "window-state", "test_engine_builtins.cpp::DBPro math and screen compatibility functions retain deterministic values"),
    ("dbpro", "Function", "SCREEN HEIGHT", "value = screen height()\n",
     "window-state", "test_engine_builtins.cpp::DBPro math and screen compatibility functions retain deterministic values"),
    ("dbc", "Function", "SCREEN FPS", "value = screen fps()\n",
     "timing-state", "test_engine_builtins.cpp::Text compatibility commands and functions expose observable state in both profiles"),
    ("dbpro", "Function", "SCREEN FPS", "value = screen fps()\n",
     "timing-state", "test_engine_builtins.cpp::DBPro math and screen compatibility functions retain deterministic values"),

    ("dbc", "Command", "HIDE WINDOW", "hide window\n",
     "window-state", "test_engine_builtins.cpp::Portable display and window commands update SDL state and reject unavailable modes"),
    ("dbpro", "Command", "HIDE WINDOW", "hide window\n",
     "window-state", "test_engine_builtins.cpp::Portable display and window commands update SDL state and reject unavailable modes"),
    ("dbc", "Command", "SHOW WINDOW", "show window\n",
     "window-state", "test_engine_builtins.cpp::Portable display and window commands update SDL state and reject unavailable modes"),
    ("dbpro", "Command", "SHOW WINDOW", "show window\n",
     "window-state", "test_engine_builtins.cpp::Portable display and window commands update SDL state and reject unavailable modes"),
    ("dbc", "Command", "MAXIMIZE WINDOW", "maximize window\n",
     "window-state", "test_engine_builtins.cpp::Portable display and window commands update SDL state and reject unavailable modes"),
    ("dbpro", "Command", "MAXIMIZE WINDOW", "maximize window\n",
     "window-state", "test_engine_builtins.cpp::Portable display and window commands update SDL state and reject unavailable modes"),
    ("dbc", "Command", "MINIMIZE WINDOW", "minimize window\n",
     "window-state", "test_engine_builtins.cpp::Portable display and window commands update SDL state and reject unavailable modes"),
    ("dbpro", "Command", "MINIMIZE WINDOW", "minimize window\n",
     "window-state", "test_engine_builtins.cpp::Portable display and window commands update SDL state and reject unavailable modes"),
    ("dbc", "Command", "RESTORE WINDOW", "restore window\n",
     "window-state", "test_engine_builtins.cpp::Portable display and window commands update SDL state and reject unavailable modes"),
    ("dbpro", "Command", "RESTORE WINDOW", "restore window\n",
     "window-state", "test_engine_builtins.cpp::Portable display and window commands update SDL state and reject unavailable modes"),
    ("dbc", "Command", "SET WINDOW TITLE", "set window title \"OpenDBC Window Test\"\n",
     "window-state", "test_engine_builtins.cpp::Portable display and window commands update SDL state and reject unavailable modes"),
    ("dbpro", "Command", "SET WINDOW TITLE", "set window title \"OpenDBC Window Test\"\n",
     "window-state", "test_engine_builtins.cpp::Portable display and window commands update SDL state and reject unavailable modes"),
    ("dbc", "Command", "SET WINDOW POSITION", "set window position 24, 32\n",
     "window-state", "test_engine_builtins.cpp::Portable display and window commands update SDL state and reject unavailable modes"),
    ("dbpro", "Command", "SET WINDOW POSITION", "set window position 24, 32\n",
     "window-state", "test_engine_builtins.cpp::Portable display and window commands update SDL state and reject unavailable modes"),
    ("dbc", "Command", "SET WINDOW SIZE", "set window size 80, 60\n",
     "window-state", "test_engine_builtins.cpp::Portable display and window commands update SDL state and reject unavailable modes"),
    ("dbpro", "Command", "SET WINDOW SIZE", "set window size 80, 60\n",
     "window-state", "test_engine_builtins.cpp::Portable display and window commands update SDL state and reject unavailable modes"),
    ("dbc", "Command", "SET WINDOW LAYOUT", "set window layout 1, 1, 1\n",
     "window-state", "test_engine_builtins.cpp::Portable display and window commands update SDL state and reject unavailable modes"),
    ("dbpro", "Command", "SET WINDOW LAYOUT", "set window layout 1, 1, 1\n",
     "window-state", "test_engine_builtins.cpp::Portable display and window commands update SDL state and reject unavailable modes"),
    ("dbc", "Command", "SET WINDOW OFF", "set window off\n",
     "window-state", "test_engine_builtins.cpp::Portable display and window commands update SDL state and reject unavailable modes"),
    ("dbpro", "Command", "SET WINDOW OFF", "set window off\n",
     "window-state", "test_engine_builtins.cpp::Portable display and window commands update SDL state and reject unavailable modes"),
    ("dbc", "Command", "SET WINDOW ON", "set window on\n",
     "window-state", "test_engine_builtins.cpp::Portable display and window commands update SDL state and reject unavailable modes"),
    ("dbpro", "Command", "SET WINDOW ON", "set window on\n",
     "window-state", "test_engine_builtins.cpp::Portable display and window commands update SDL state and reject unavailable modes"),
    ("dbpro", "Command", "WINDOW TO FRONT", "window to front\n",
     "window-state", "test_engine_builtins.cpp::Portable display and window commands update SDL state and reject unavailable modes"),
)

INPUT_TEST = "test_engine_builtins.cpp::DBPro text keyboard mouse and escape controls preserve their historical state"
JOYSTICK_TEST = "test_engine_builtins.cpp::SDL virtual joysticks expose the DBPro control device model"
SPRITE_TEST = "test_engine_builtins.cpp::DBPro sprites preserve transient paste atlas frame and render ordering state"
WAIT_TEST = "test_engine_builtins.cpp::SLEEP and key waits suspend resume and cancel cooperatively"

LOT17_WITNESS_GROUPS = (
    (("dbc", "dbpro"), "Command", "CLEAR ENTRY BUFFER", "clear entry buffer\n", "input-state", INPUT_TEST),
    (("dbc", "dbpro"), "Command", "DISABLE ESCAPEKEY", "disable escapekey\n", "input-state", INPUT_TEST),
    (("dbc", "dbpro"), "Command", "ENABLE ESCAPEKEY", "enable escapekey\n", "input-state", INPUT_TEST),
    (("dbc", "dbpro"), "Command", "FLIP SPRITE", "flip sprite 1\n", "render-state", SPRITE_TEST),
    (("dbc", "dbpro"), "Command", "HIDE ALL SPRITES", "hide all sprites\n", "render-state", SPRITE_TEST),
    (("dbc", "dbpro"), "Command", "MIRROR SPRITE", "mirror sprite 1\n", "render-state", SPRITE_TEST),
    (("dbc", "dbpro"), "Command", "PERFORM CHECKLIST FOR CONTROL DEVICES",
     "perform checklist for control devices\n", "input-device", JOYSTICK_TEST),
    (("dbc", "dbpro"), "Command", "POSITION MOUSE", "position mouse 7, 8\n", "input-state", INPUT_TEST),
    (("dbc", "dbpro"), "Command", "SCALE SPRITE", "scale sprite 1, 125\n", "render-state", SPRITE_TEST),
    (("dbc",), "Command", "SET CONTROL DEVICE", "set control device \"controller\"\n",
     "input-device", JOYSTICK_TEST),
    (("dbpro",), "Command", "SET CONTROL DEVICE", "set control device \"controller\", 0\n",
     "input-device", JOYSTICK_TEST),
    (("dbc", "dbpro"), "Command", "SHOW ALL SPRITES", "show all sprites\n", "render-state", SPRITE_TEST),
    (("dbc", "dbpro"), "Command", "STRETCH SPRITE", "stretch sprite 1, 125, 75\n",
     "render-state", SPRITE_TEST),
    (("dbpro",), "Command", "CHANGE MOUSE", "change mouse 0, 1\n", "input-state", INPUT_TEST),
    (("dbpro",), "Command", "CLONE SPRITE", "clone sprite 1, 2\n", "render-state", SPRITE_TEST),
    (("dbpro",), "Command", "CREATE ANIMATED SPRITE",
     "create animated sprite 1, \"atlas.bmp\", 2, 2, 1\n", "render-state", SPRITE_TEST),
    (("dbpro",), "Command", "HIDE SPRITE", "hide sprite 1\n", "render-state", SPRITE_TEST),
    (("dbpro",), "Command", "MOVE SPRITE", "move sprite 1, 4.0\n", "render-state", SPRITE_TEST),
    (("dbpro",), "Command", "OFFSET SPRITE", "offset sprite 1, 3, 4\n", "render-state", SPRITE_TEST),
    (("dbpro",), "Command", "PASTE SPRITE", "paste sprite 1, 10, 20\n", "render-state", SPRITE_TEST),
    (("dbpro",), "Command", "PLAY SPRITE", "play sprite 1, 1, 4, 10\n", "render-state", SPRITE_TEST),
    (("dbpro",), "Command", "ROTATE SPRITE", "rotate sprite 1, 45.0\n", "render-state", SPRITE_TEST),
    (("dbpro",), "Command", "SET SPRITE", "set sprite 1, 0, 2\n", "render-state", SPRITE_TEST),
    (("dbpro",), "Command", "SET SPRITE FRAME", "set sprite frame 1, 2\n", "render-state", SPRITE_TEST),
    (("dbpro",), "Command", "SET SPRITE TEXTURE COORD",
     "set sprite texture coord 1, 0, 0.25, 0.5\n", "render-state", SPRITE_TEST),
    (("dbpro",), "Command", "SHOW SPRITE", "show sprite 1\n", "render-state", SPRITE_TEST),
    (("dbpro",), "Command", "SIZE SPRITE", "size sprite 1, 16, 8\n", "render-state", SPRITE_TEST),
    (("dbpro",), "Command", "SUSPEND FOR MOUSE", "suspend for mouse\n", "vm-state", WAIT_TEST),
    (("dbpro",), "Command", "DRAW SPRITES FIRST", "draw sprites first\n", "render-order", SPRITE_TEST),
    (("dbpro",), "Command", "DRAW SPRITES LAST", "draw sprites last\n", "render-order", SPRITE_TEST),
    (("dbpro",), "Command", "SET CONTROL DEVICE INDEX", "set control device index 0\n",
     "input-device", JOYSTICK_TEST),
    (("dbpro",), "Command", "SET JOYSTICK DEADZONE", "set joystick deadzone 50\n",
     "input-device", JOYSTICK_TEST),
    (("dbpro",), "Command", "SET KEY STATE TOGGLE", "set key state toggle 20, 1\n",
     "input-state", INPUT_TEST),
    (("dbpro",), "Command", "SET SPRITE IMAGE", "set sprite image 1, 2\n", "render-state", SPRITE_TEST),
    (("dbpro",), "Command", "SET SPRITE RESIZE", "set sprite resize 1\n", "render-state", SPRITE_TEST),
    (("dbpro",), "Command", "SET SPRITE RESIZE", "set sprite resize 1, 2.0, 1.5\n",
     "render-state", SPRITE_TEST),
    (("dbpro",), "Command", "WAIT MOUSE", "wait mouse\n", "vm-state", WAIT_TEST),

    (("dbc", "dbpro"), "Function", "CONTROL DEVICE NAME$", "value$ = control device name$()\n",
     "input-device", JOYSTICK_TEST),
    (("dbc", "dbpro"), "Function", "CONTROL DEVICE X", "value = control device x()\n",
     "input-device", JOYSTICK_TEST),
    (("dbc", "dbpro"), "Function", "CONTROL DEVICE Y", "value = control device y()\n",
     "input-device", JOYSTICK_TEST),
    (("dbc", "dbpro"), "Function", "CONTROL DEVICE Z", "value = control device z()\n",
     "input-device", JOYSTICK_TEST),
    (("dbc", "dbpro"), "Function", "CONTROLKEY", "value = controlkey()\n", "input-state", INPUT_TEST),
    (("dbc", "dbpro"), "Function", "ENTRY$", "value$ = entry$()\n", "input-state", INPUT_TEST),
    (("dbc", "dbpro"), "Function", "ESCAPEKEY", "value = escapekey()\n", "input-state", INPUT_TEST),
    (("dbc", "dbpro"), "Function", "JOYSTICK DOWN", "value = joystick down()\n", "input-device", JOYSTICK_TEST),
    (("dbc", "dbpro"), "Function", "JOYSTICK FIRE A", "value = joystick fire a()\n", "input-device", JOYSTICK_TEST),
    (("dbc", "dbpro"), "Function", "JOYSTICK FIRE B", "value = joystick fire b()\n", "input-device", JOYSTICK_TEST),
    (("dbc", "dbpro"), "Function", "JOYSTICK FIRE C", "value = joystick fire c()\n", "input-device", JOYSTICK_TEST),
    (("dbc", "dbpro"), "Function", "JOYSTICK FIRE D", "value = joystick fire d()\n", "input-device", JOYSTICK_TEST),
    (("dbc", "dbpro"), "Function", "JOYSTICK FIRE X", "value = joystick fire x(7)\n", "input-device", JOYSTICK_TEST),
    (("dbc", "dbpro"), "Function", "JOYSTICK HAT ANGLE", "value = joystick hat angle(0)\n", "input-device", JOYSTICK_TEST),
    (("dbc", "dbpro"), "Function", "JOYSTICK LEFT", "value = joystick left()\n", "input-device", JOYSTICK_TEST),
    (("dbc", "dbpro"), "Function", "JOYSTICK RIGHT", "value = joystick right()\n", "input-device", JOYSTICK_TEST),
    (("dbc", "dbpro"), "Function", "JOYSTICK SLIDER A", "value = joystick slider a()\n", "input-device", JOYSTICK_TEST),
    (("dbc", "dbpro"), "Function", "JOYSTICK SLIDER B", "value = joystick slider b()\n", "input-device", JOYSTICK_TEST),
    (("dbc", "dbpro"), "Function", "JOYSTICK SLIDER C", "value = joystick slider c()\n", "input-device", JOYSTICK_TEST),
    (("dbc", "dbpro"), "Function", "JOYSTICK SLIDER D", "value = joystick slider d()\n", "input-device", JOYSTICK_TEST),
    (("dbc", "dbpro"), "Function", "JOYSTICK TWIST X", "value = joystick twist x()\n", "input-device", JOYSTICK_TEST),
    (("dbc", "dbpro"), "Function", "JOYSTICK TWIST Y", "value = joystick twist y()\n", "input-device", JOYSTICK_TEST),
    (("dbc", "dbpro"), "Function", "JOYSTICK TWIST Z", "value = joystick twist z()\n", "input-device", JOYSTICK_TEST),
    (("dbc", "dbpro"), "Function", "JOYSTICK UP", "value = joystick up()\n", "input-device", JOYSTICK_TEST),
    (("dbc", "dbpro"), "Function", "JOYSTICK X", "value = joystick x()\n", "input-device", JOYSTICK_TEST),
    (("dbc", "dbpro"), "Function", "JOYSTICK Y", "value = joystick y()\n", "input-device", JOYSTICK_TEST),
    (("dbc", "dbpro"), "Function", "JOYSTICK Z", "value = joystick z()\n", "input-device", JOYSTICK_TEST),
    (("dbc", "dbpro"), "Function", "KEYSTATE", "value = keystate(30)\n", "input-state", INPUT_TEST),
    (("dbc", "dbpro"), "Function", "MOUSEMOVEX", "value = mousemovex()\n", "input-state", INPUT_TEST),
    (("dbc", "dbpro"), "Function", "MOUSEMOVEY", "value = mousemovey()\n", "input-state", INPUT_TEST),
    (("dbc", "dbpro"), "Function", "MOUSEMOVEZ", "value = mousemovez()\n", "input-state", INPUT_TEST),
    (("dbc", "dbpro"), "Function", "MOUSEZ", "value = mousez()\n", "input-state", INPUT_TEST),
    (("dbc", "dbpro"), "Function", "SHIFTKEY", "value = shiftkey()\n", "input-state", INPUT_TEST),
    (("dbc", "dbpro"), "Function", "SPRITE COLLISION", "value = sprite collision(1, 2)\n",
     "render-state", SPRITE_TEST),
    (("dbc", "dbpro"), "Function", "SPRITE FLIPPED", "value = sprite flipped(1)\n", "render-state", SPRITE_TEST),
    (("dbc", "dbpro"), "Function", "SPRITE HEIGHT", "value = sprite height(1)\n", "render-state", SPRITE_TEST),
    (("dbc", "dbpro"), "Function", "SPRITE HIT", "value = sprite hit(1, 2)\n", "render-state", SPRITE_TEST),
    (("dbc", "dbpro"), "Function", "SPRITE MIRRORED", "value = sprite mirrored(1)\n", "render-state", SPRITE_TEST),
    (("dbc", "dbpro"), "Function", "SPRITE OFFSET X", "value = sprite offset x(1)\n", "render-state", SPRITE_TEST),
    (("dbc", "dbpro"), "Function", "SPRITE OFFSET Y", "value = sprite offset y(1)\n", "render-state", SPRITE_TEST),
    (("dbc", "dbpro"), "Function", "SPRITE SCALE X", "value = sprite scale x(1)\n", "render-state", SPRITE_TEST),
    (("dbc", "dbpro"), "Function", "SPRITE SCALE Y", "value = sprite scale y(1)\n", "render-state", SPRITE_TEST),
    (("dbc", "dbpro"), "Function", "SPRITE WIDTH", "value = sprite width(1)\n", "render-state", SPRITE_TEST),
    (("dbpro",), "Function", "SPRITE ANGLE", "value# = sprite angle(1)\n", "render-state", SPRITE_TEST),
    (("dbpro",), "Function", "SPRITE EXIST", "value = sprite exist(1)\n", "render-state", SPRITE_TEST),
    (("dbpro",), "Function", "SPRITE IMAGE", "value = sprite image(1)\n", "render-state", SPRITE_TEST),
    (("dbpro",), "Function", "SPRITE X", "value = sprite x(1)\n", "render-state", SPRITE_TEST),
    (("dbpro",), "Function", "SPRITE Y", "value = sprite y(1)\n", "render-state", SPRITE_TEST),
    (("dbpro",), "Function", "CONTROL DEVICE INDEX", "value = control device index()\n",
     "input-device", JOYSTICK_TEST),
    (("dbpro",), "Function", "GET KEY STATE", "value = get key state(20)\n", "input-state", INPUT_TEST),
    (("dbpro",), "Function", "JOYSTICK INFORMATION", "value = joystick information(0)\n",
     "input-device", JOYSTICK_TEST),
    (("dbpro",), "Function", "SPRITE FRAME", "value = sprite frame(1)\n", "render-state", SPRITE_TEST),
    (("dbpro",), "Function", "SPRITE PRIORITY", "value = sprite priority(1)\n", "render-state", SPRITE_TEST),
    (("dbpro",), "Function", "SPRITE VISIBLE", "value = sprite visible(1)\n", "render-state", SPRITE_TEST),
)

UNIT_WITNESSES += tuple(
    (Profile, Kind, Name, Code, Oracle, Test)
    for Profiles, Kind, Name, Code, Oracle, Test in LOT17_WITNESS_GROUPS
    for Profile in Profiles
)

AUDIO_TEST = "test_engine_builtins.cpp::Portable sound music 3D audio and memblocks preserve DBPro state"

LOT18_WITNESS_GROUPS = (
    (("dbc", "dbpro"), "Command", "CLONE SOUND", "clone sound 2, 1\n"),
    (("dbc", "dbpro"), "Command", "DELETE MUSIC", "delete music 1\n"),
    (("dbc", "dbpro"), "Command", "DELETE SOUND", "delete sound 1\n"),
    (("dbc", "dbpro"), "Command", "LOAD 3DSOUND", "load 3dsound \"sound.wav\", 1\n"),
    (("dbc", "dbpro"), "Command", "LOAD MUSIC", "load music \"music.mp3\", 1\n"),
    (("dbc",), "Command", "LOAD SOUND", "load sound \"sound.wav\", 1\n"),
    (("dbpro",), "Command", "LOAD SOUND", "load sound \"sound.wav\", 1\n"),
    (("dbc", "dbpro"), "Command", "LOOP MUSIC", "loop music 1\n"),
    (("dbc", "dbpro"), "Command", "LOOP SOUND", "loop sound 1\n"),
    (("dbc", "dbpro"), "Command", "MAKE MEMBLOCK FROM SOUND", "make memblock from sound 1, 1\n"),
    (("dbc", "dbpro"), "Command", "MAKE SOUND FROM MEMBLOCK", "make sound from memblock 1, 1\n"),
    (("dbc", "dbpro"), "Command", "PAUSE MUSIC", "pause music 1\n"),
    (("dbc", "dbpro"), "Command", "PAUSE SOUND", "pause sound 1\n"),
    (("dbc", "dbpro"), "Command", "PLAY MUSIC", "play music 1\n"),
    (("dbc", "dbpro"), "Command", "PLAY SOUND", "play sound 1\n"),
    (("dbc", "dbpro"), "Command", "POSITION LISTENER", "position listener 1.0, 2.0, 3.0\n"),
    (("dbc", "dbpro"), "Command", "POSITION SOUND", "position sound 1, 1.0, 2.0, 3.0\n"),
    (("dbc", "dbpro"), "Command", "RESUME MUSIC", "resume music 1\n"),
    (("dbc", "dbpro"), "Command", "RESUME SOUND", "resume sound 1\n"),
    (("dbc", "dbpro"), "Command", "ROTATE LISTENER", "rotate listener 1.0, 2.0, 3.0\n"),
    (("dbpro",), "Command", "SCALE LISTENER", "scale listener 0.5\n"),
    (("dbpro",), "Command", "SET MUSIC SPEED", "set music speed 1, 75\n"),
    (("dbpro",), "Command", "SET MUSIC VOLUME", "set music volume 1, 50\n"),
    (("dbc", "dbpro"), "Command", "SET SOUND PAN", "set sound pan 1, -2500\n"),
    (("dbc", "dbpro"), "Command", "SET SOUND SPEED", "set sound speed 1, 22050\n"),
    (("dbc", "dbpro"), "Command", "SET SOUND VOLUME", "set sound volume 1, 50\n"),
    (("dbc", "dbpro"), "Command", "STOP MUSIC", "stop music 1\n"),
    (("dbc", "dbpro"), "Command", "STOP SOUND", "stop sound 1\n"),

    (("dbc",), "Function", "LISTENER ANGLE X", "value# = listener angle x()\n"),
    (("dbpro",), "Function", "LISTENER ANGLE X", "value# = listener angle x()\n"),
    (("dbc",), "Function", "LISTENER ANGLE Y", "value# = listener angle y()\n"),
    (("dbpro",), "Function", "LISTENER ANGLE Y", "value# = listener angle y()\n"),
    (("dbc",), "Function", "LISTENER ANGLE Z", "value# = listener angle z()\n"),
    (("dbpro",), "Function", "LISTENER ANGLE Z", "value# = listener angle z()\n"),
    (("dbc",), "Function", "LISTENER POSITION X", "value# = listener position x()\n"),
    (("dbpro",), "Function", "LISTENER POSITION X", "value# = listener position x()\n"),
    (("dbc",), "Function", "LISTENER POSITION Y", "value# = listener position y()\n"),
    (("dbpro",), "Function", "LISTENER POSITION Y", "value# = listener position y()\n"),
    (("dbc",), "Function", "LISTENER POSITION Z", "value# = listener position z()\n"),
    (("dbpro",), "Function", "LISTENER POSITION Z", "value# = listener position z()\n"),
    (("dbc", "dbpro"), "Function", "MUSIC EXIST", "value = music exist(1)\n"),
    (("dbc", "dbpro"), "Function", "MUSIC LOOPING", "value = music looping(1)\n"),
    (("dbc", "dbpro"), "Function", "MUSIC PAUSED", "value = music paused(1)\n"),
    (("dbc", "dbpro"), "Function", "MUSIC PLAYING", "value = music playing(1)\n"),
    (("dbpro",), "Function", "MUSIC SPEED", "value = music speed(1)\n"),
    (("dbpro",), "Function", "MUSIC VOLUME", "value = music volume(1)\n"),
    (("dbc", "dbpro"), "Function", "SOUND EXIST", "value = sound exist(1)\n"),
    (("dbc", "dbpro"), "Function", "SOUND LOOPING", "value = sound looping(1)\n"),
    (("dbc", "dbpro"), "Function", "SOUND PAUSED", "value = sound paused(1)\n"),
    (("dbc", "dbpro"), "Function", "SOUND PLAYING", "value = sound playing(1)\n"),
    (("dbc", "dbpro"), "Function", "SOUND POSITION X", "value# = sound position x(1)\n"),
    (("dbc", "dbpro"), "Function", "SOUND POSITION Y", "value# = sound position y(1)\n"),
    (("dbc", "dbpro"), "Function", "SOUND POSITION Z", "value# = sound position z(1)\n"),
    (("dbc", "dbpro"), "Function", "SOUND TYPE", "value = sound type(1)\n"),
    (("dbpro",), "Function", "SOUND PAN", "value = sound pan(1)\n"),
    (("dbpro",), "Function", "SOUND SPEED", "value = sound speed(1)\n"),
    (("dbpro",), "Function", "SOUND VOLUME", "value = sound volume(1)\n"),
)

UNIT_WITNESSES += tuple(
    (Profile, Kind, Name, Code, "audio-state", AUDIO_TEST)
    for Profiles, Kind, Name, Code in LOT18_WITNESS_GROUPS
    for Profile in Profiles
)

OBJECT_TEST = (
    "test_engine_builtins.cpp::DBPro object loading primitives transforms and size queries preserve historical behavior"
)

LOT19_WITNESS_GROUPS = (
    ("Command", "DELETE OBJECTS", "delete objects 1, 4\n"),
    ("Command", "LOAD OBJECT", "load object \"model.x\", 1, 4, 2\n"),
    ("Command", "MAKE OBJECT PLAIN", "make object plain 1, 20, 10, 1\n"),
    ("Command", "MAKE OBJECT SPHERE", "make object sphere 1, 10\n"),
    ("Command", "MAKE OBJECT SPHERE", "make object sphere 1, 10, 8, 12\n"),
    ("Command", "MAKE OBJECT TRIANGLE", "make object triangle 1, 0, 0, 0, 2, 0, 0, 0, 3, 0\n"),
    ("Command", "MOVE OBJECT LEFT", "move object left 1, 4\n"),
    ("Command", "MOVE OBJECT RIGHT", "move object right 1, 1\n"),
    ("Command", "SCALE OBJECT", "scale object 1, 200, 50, 300\n"),
    ("Command", "XROTATE OBJECT", "xrotate object 1, 25\n"),
    ("Command", "ZROTATE OBJECT", "zrotate object 1, 35\n"),
    ("Function", "OBJECT SIZE", "value# = object size(1, 1)\n"),
    ("Command", "MAKE OBJECT PLANE", "make object plane 1, 20, 10, 1\n"),
)

UNIT_WITNESSES += tuple(
    ("dbpro", Kind, Name, Code, "engine-state", OBJECT_TEST)
    for Kind, Name, Code in LOT19_WITNESS_GROUPS
)

DBC_RESTORED_OBJECT_TEST = (
    "test_engine_builtins.cpp::Classic restored object commands preserve typed state and historical paths"
)

DBC_RESTORED_OBJECT_WITNESSES = (
    ("Command", "APPEND OBJECT", "append object \"walk.x\", 1, 100\n"),
    ("Command", "LOAD OBJECT", "load object \"model.x\", 1\n"),
    ("Command", "MAKE OBJECT CONE", "make object cone 2, 20\n"),
    ("Command", "MAKE OBJECT CYLINDER", "make object cylinder 3, 12\n"),
    ("Command", "MAKE OBJECT TRIANGLE", "make object triangle 4, 0, 0, 0, 2, 0, 0, 0, 3, 0\n"),
    ("Command", "MOVE OBJECT", "move object 2, 5\n"),
    ("Command", "POINT OBJECT", "point object 3, 1, 2, 3\n"),
    ("Command", "SCROLL OBJECT TEXTURE", "scroll object texture 2, 0.25, -0.5\n"),
    ("Command", "XROTATE OBJECT", "xrotate object 2, 10\n"),
    ("Command", "YROTATE OBJECT", "yrotate object 2, 20\n"),
    ("Command", "ZROTATE OBJECT", "zrotate object 2, 30\n"),
    ("Function", "CHECKLIST VALUE A", "value = checklist value a(1)\n"),
    ("Function", "CHECKLIST VALUE B", "value = checklist value b(1)\n"),
    ("Function", "CHECKLIST VALUE C", "value = checklist value c(1)\n"),
    ("Function", "CHECKLIST VALUE D", "value = checklist value d(1)\n"),
)

UNIT_WITNESSES += tuple(
    ("dbc", Kind, Name, Code, "engine-state", DBC_RESTORED_OBJECT_TEST)
    for Kind, Name, Code in DBC_RESTORED_OBJECT_WITNESSES
)

DBC_BOB_TEST = (
    "test_engine_builtins.cpp::Classic BOBs retain independent render collision and transform state"
)

DBC_BOB_WITNESSES = (
    ("Command", "BOB", "bob 1, 4, 5, 21\n"),
    ("Command", "DELETE BOB", "delete bob 1\n"),
    ("Command", "FLIP BOB", "flip bob 1\n"),
    ("Command", "MIRROR BOB", "mirror bob 1\n"),
    ("Command", "OFFSET BOB", "offset bob 1, 2, 3\n"),
    ("Command", "PASTE BOB", "paste bob 1, 19, 20\n"),
    ("Command", "SCALE BOB", "scale bob 1, 200\n"),
    ("Command", "SET BOB", "set bob 1, 0, 1\n"),
    ("Command", "STRETCH BOB", "stretch bob 1, 150, 50\n"),
    ("Function", "BOB COLLISION", "value# = bob collision(1, 2)\n"),
    ("Function", "BOB EXIST", "value# = bob exist(1)\n"),
    ("Function", "BOB FLIPPED", "value# = bob flipped(1)\n"),
    ("Function", "BOB HEIGHT", "value# = bob height(1)\n"),
    ("Function", "BOB HIT", "value# = bob hit(1, 2)\n"),
    ("Function", "BOB IMAGE", "value# = bob image(1)\n"),
    ("Function", "BOB MIRRORED", "value# = bob mirrored(1)\n"),
    ("Function", "BOB OFFSET X", "value# = bob offset x(1)\n"),
    ("Function", "BOB OFFSET Y", "value# = bob offset y(1)\n"),
    ("Function", "BOB SCALE X", "value# = bob scale x(1)\n"),
    ("Function", "BOB SCALE Y", "value# = bob scale y(1)\n"),
    ("Function", "BOB WIDTH", "value# = bob width(1)\n"),
    ("Function", "BOB X", "value# = bob x(1)\n"),
    ("Function", "BOB Y", "value# = bob y(1)\n"),
)

UNIT_WITNESSES += tuple(
    ("dbc", Kind, Name, Code, "bob-render-collision-state", DBC_BOB_TEST)
    for Kind, Name, Code in DBC_BOB_WITNESSES
)

DBC_FILE_TEST = (
    "test_engine_builtins.cpp::Classic portable files memblocks and DIRBLOCKs round-trip inside the asset root"
)

DBC_FILE_WITNESSES = (
    ("Command", "CD", "cd \".\"\n"),
    ("Command", "COPY MEMBLOCK", "copy memblock 1, 2, 0, 0, 4\n"),
    ("Command", "DELETE DIRECTORY", "delete directory \"created\"\n"),
    ("Command", "DIR", "dir\n"),
    ("Command", "DRIVELIST", "drivelist\n"),
    ("Command", "EMPTY CHECKLIST", "empty checklist\n"),
    ("Command", "FIND FIRST", "find first\n"),
    ("Command", "FIND NEXT", "find next\n"),
    ("Command", "MAKE DIRECTORY", "make directory \"created\"\n"),
    ("Command", "MAKE FILE", "make file \"created/empty.bin\"\n"),
    ("Command", "MAKE MEMBLOCK FROM FILE", "make memblock from file 4, 2\n"),
    ("Command", "MOVE FILE", "move file \"source.bin\", \"moved.bin\"\n"),
    ("Command", "PERFORM CHECKLIST FOR DRIVES", "perform checklist for drives\n"),
    ("Command", "READ DIRBLOCK", "read dirblock 3, \"restored\"\n"),
    ("Command", "READ MEMBLOCK", "read memblock 1, 3\n"),
    ("Command", "RENAME FILE", "rename file \"source.bin\", \"renamed.bin\"\n"),
    ("Command", "SET DIR", "set dir \"source\\nested\"\n"),
    ("Command", "WRITE DIRBLOCK", "write dirblock 3, \"source\"\n"),
    ("Command", "WRITE MEMBLOCK", "write memblock 1, 2\n"),
    ("Function", "FILE END", "value = file end(1)\n"),
    ("Function", "FILE OPEN", "value = file open(1)\n"),
    ("Function", "FILE SIZE", "value = file size(\"source/nested/data.bin\")\n"),
    ("Function", "GET DIR$", "value$ = get dir$()\n"),
    ("Function", "GET FILE DATE$", "value$ = get file date$()\n"),
    ("Function", "GET FILE NAME$", "value$ = get file name$()\n"),
    ("Function", "GET FILE TYPE", "value = get file type()\n"),
    ("Function", "PATH EXIST", "value = path exist(\"source\\nested\")\n"),
)

UNIT_WITNESSES += tuple(
    ("dbc", Kind, Name, Code, "confined-file-dirblock-state", DBC_FILE_TEST)
    for Kind, Name, Code in DBC_FILE_WITNESSES
)

DBC_HOST_CORE_TEST = (
    "test_engine_builtins.cpp::Classic host core clipboard and measurable system queries expose observable state"
)

DBC_HOST_CORE_WITNESSES = (
    ("Command", "EXECUTE FILE", "execute file \"document.txt\", \"--page 2\", \"working\", 1\n"),
    ("Command", "EXIT PROMPT", "exit prompt \"Complete\", \"Finished safely\"\n"),
    ("Command", "WRITE TO CLIPBOARD", "write to clipboard \"OpenDBC Classic\"\n"),
    ("Function", "APPNAME$", "value$ = appname$()\n"),
    ("Function", "CL$", "value$ = cl$()\n"),
    ("Function", "FILTERING AVAILABLE", "value# = filtering available()\n"),
    ("Function", "GET BACKBUFFER DEPTH", "value# = get backbuffer depth()\n"),
    ("Function", "GET BACKBUFFER HEIGHT", "value# = get backbuffer height()\n"),
    ("Function", "GET BACKBUFFER PITCH", "value# = get backbuffer pitch()\n"),
    ("Function", "GET BACKBUFFER WIDTH", "value# = get backbuffer width()\n"),
    ("Function", "GET CLIPBOARD$", "value$ = get clipboard$()\n"),
    ("Function", "GET DATE$", "value$ = get date$()\n"),
    ("Function", "GET TIME$", "value$ = get time$()\n"),
    ("Function", "HCOS", "value# = hcos(0)\n"),
    ("Function", "HSIN", "value# = hsin(0)\n"),
    ("Function", "HTAN", "value# = htan(45)\n"),
    ("Function", "STATISTIC", "value# = statistic(1)\n"),
    ("Function", "SYSTEM DMEM AVAILABLE", "value# = system dmem available()\n"),
    ("Function", "SYSTEM SMEM AVAILABLE", "value# = system smem available()\n"),
    ("Function", "SYSTEM TMEM AVAILABLE", "value# = system tmem available()\n"),
)

UNIT_WITNESSES += tuple(
    ("dbc", Kind, Name, Code, "host-core-system-state", DBC_HOST_CORE_TEST)
    for Kind, Name, Code in DBC_HOST_CORE_WITNESSES
)

DBC_PORTABLE_OBJECT_WITNESSES = (
    ("Command", "MAKE OBJECT BOX", "make object box 6, 2, 4, 6\n"),
    ("Command", "PITCH OBJECT DOWN", "pitch object down 6, 12\n"),
    ("Command", "PITCH OBJECT UP", "pitch object up 6, 5\n"),
    ("Command", "ROLL OBJECT LEFT", "roll object left 6, 20\n"),
    ("Command", "ROLL OBJECT RIGHT", "roll object right 6, 25\n"),
    ("Command", "TURN OBJECT LEFT", "turn object left 6, 10\n"),
    ("Command", "TURN OBJECT RIGHT", "turn object right 6, 25\n"),
    ("Function", "LIMB TEXTURE", "value# = limb texture(6, 0)\n"),
    ("Function", "LIMB TEXTURE NAME$", "value$ = limb texture name$(6, 0)\n"),
    ("Function", "OBJECT SIZE", "value# = object size(6)\n"),
    ("Function", "OBJECT SIZE X", "value# = object size x(6)\n"),
    ("Function", "OBJECT SIZE Y", "value# = object size y(6)\n"),
    ("Function", "OBJECT SIZE Z", "value# = object size z(6)\n"),
)

UNIT_WITNESSES += tuple(
    ("dbc", Kind, Name, Code, "object-geometry-texture-state", DBC_RESTORED_OBJECT_TEST)
    for Kind, Name, Code in DBC_PORTABLE_OBJECT_WITNESSES
)

UNIT_WITNESSES += (
    ("dbpro", "Function", "LIMB TEXTURE NAME", "value$ = limb texture name(2, 1, 2)\n",
     "object-geometry-texture-state",
     "test_engine_builtins.cpp::Portable object state animation and limbs preserve hierarchy and overload semantics"),
)

DBC_HAPTIC_TEST = (
    "test_input_haptics.cpp::Control device force feedback includes SDL rumble and haptic devices"
)

DBC_HAPTIC_WITNESSES = (
    ("Command", "FORCE AUTO CENTER OFF", "force auto center off\n"),
    ("Command", "FORCE AUTO CENTER ON", "force auto center on\n"),
    ("Command", "FORCE CHAINSAW", "force chainsaw 75, 250\n"),
    ("Command", "FORCE DOWN", "force down 100\n"),
    ("Command", "FORCE IMPACT", "force impact 25, 300\n"),
    ("Command", "FORCE LEFT", "force left 100\n"),
    ("Command", "FORCE NO EFFECT", "force no effect\n"),
    ("Command", "FORCE RIGHT", "force right 100\n"),
    ("Command", "FORCE UP", "force up 100\n"),
)

UNIT_WITNESSES += tuple(
    ("dbc", Kind, Name, Code, "input-haptic-device", DBC_HAPTIC_TEST)
    for Kind, Name, Code in DBC_HAPTIC_WITNESSES
)

UNIT_WITNESSES += (
    ("dbc", "Command", "RECORD SOUND", "record sound 13, 1\n", "audio-capture-validation", AUDIO_TEST),
    ("dbc", "Command", "SAVE SOUND", "save sound \"saved.wav\", 3\n", "audio-wave-file", AUDIO_TEST),
    ("dbc", "Command", "STOP RECORDING SOUND", "stop recording sound\n", "audio-capture-state", AUDIO_TEST),
)

UNIT_WITNESSES += (
    ("dbc", "Command", "FORCE ANGLE", "force angle 100, 45.5, 100\n", "input-haptic-no-device",
     "test_input_haptics.cpp::DBC haptic effects retain the historical parameter mapping"),
    ("dbc", "Command", "FORCE WATER EFFECT", "force water effect 100, 100\n", "input-haptic-no-device",
     "test_input_haptics.cpp::DBC haptic effects retain the historical parameter mapping"),
    ("dbc", "Command", "SET OBJECT COLLISION TO POLYGONS", "set object collision to polygons 1\n",
     "collision-state", "test_collision_math.cpp::polygon primitives classify and intersect degenerate geometry"),
)

CAMERA_LIGHT_TEST = (
    "test_engine_builtins.cpp::Portable cameras and lights preserve DBPro state and typed failures"
)

LOT20_WITNESS_GROUPS = (
    (("dbc", "dbpro"), "Command", "AUTOCAM OFF", "autocam off\n"),
    (("dbc", "dbpro"), "Command", "AUTOCAM ON", "autocam on\n"),
    (("dbc",), "Command", "CLEAR CAMERA VIEW", "clear camera view rgb(1, 2, 3)\n"),
    (("dbpro",), "Command", "COLOR AMBIENT LIGHT", "color ambient light rgb(16, 32, 48)\n"),
    (("dbpro",), "Command", "COLOR LIGHT", "color light 2, rgb(12, 34, 56)\n"),
    (("dbpro",), "Command", "COLOR LIGHT", "color light 2, 12, 34, 56\n"),
    (("dbpro",), "Command", "CONTROL CAMERA USING ARROWKEYS",
     "control camera using arrowkeys 1, 2.0, 3.0\n"),
    (("dbpro",), "Command", "DELETE CAMERA", "delete camera 1\n"),
    (("dbc", "dbpro"), "Command", "DELETE LIGHT", "delete light 2\n"),
    (("dbpro",), "Command", "HIDE LIGHT", "hide light 2\n"),
    (("dbpro",), "Command", "MAKE CAMERA", "make camera 1\n"),
    (("dbc", "dbpro"), "Command", "MAKE LIGHT", "make light 2\n"),
    (("dbpro",), "Command", "PITCH CAMERA DOWN", "pitch camera down 1, 2.0\n"),
    (("dbpro",), "Command", "PITCH CAMERA UP", "pitch camera up 1, 2.0\n"),
    (("dbc",), "Command", "POINT CAMERA", "point camera 1.0, 2.0, 3.0\n"),
    (("dbc", "dbpro"), "Command", "POINT LIGHT", "point light 2, 1.0, 2.0, 3.0\n"),
    (("dbpro",), "Command", "POSITION LIGHT", "position light 2, 1.0, 2.0, 3.0\n"),
    (("dbpro",), "Command", "POSITION LIGHT", "position light 2, 4\n"),
    (("dbpro",), "Command", "ROLL CAMERA LEFT", "roll camera left 1, 2.0\n"),
    (("dbc",), "Command", "ROLL CAMERA RIGHT", "roll camera right 2.0\n"),
    (("dbpro",), "Command", "ROLL CAMERA RIGHT", "roll camera right 1, 2.0\n"),
    (("dbc",), "Command", "ROTATE LIGHT", "rotate light 2, 1.0, 2.0, 3.0\n"),
    (("dbpro",), "Command", "ROTATE LIGHT", "rotate light 2, 1.0, 2.0, 3.0\n"),
    (("dbpro",), "Command", "ROTATE LIGHT", "rotate light 2, 4\n"),
    (("dbc", "dbpro"), "Command", "SET AMBIENT LIGHT", "set ambient light 40\n"),
    (("dbc",), "Command", "SET CAMERA ROTATION XYZ", "set camera rotation xyz\n"),
    (("dbpro",), "Command", "SET CAMERA ROTATION XYZ", "set camera rotation xyz 1\n"),
    (("dbpro",), "Command", "SET CAMERA ROTATION ZYX", "set camera rotation zyx 1\n"),
    (("dbc",), "Command", "SET CAMERA TO FOLLOW",
     "set camera to follow 1, 2, 3, 45, 20, 10, 1, 0\n"),
    (("dbpro",), "Command", "SET CAMERA TO FOLLOW",
     "set camera to follow 1, 1, 2, 3, 45, 20, 10, 1, 0\n"),
    (("dbc",), "Command", "SET CAMERA TO OBJECT ORIENTATION", "set camera to object orientation 1\n"),
    (("dbpro",), "Command", "SET CAMERA TO OBJECT ORIENTATION", "set camera to object orientation 1, 2\n"),
    (("dbpro",), "Command", "SET CURRENT CAMERA", "set current camera 1\n"),
    (("dbpro",), "Command", "SET DIRECTIONAL LIGHT", "set directional light 2, 1, -2, 3\n"),
    (("dbc", "dbpro"), "Command", "SET LIGHT RANGE", "set light range 2, 750\n"),
    (("dbc", "dbpro"), "Command", "SET LIGHT TO OBJECT ORIENTATION",
     "set light to object orientation 2, 1\n"),
    (("dbc", "dbpro"), "Command", "SET LIGHT TO OBJECT POSITION", "set light to object position 2, 1\n"),
    (("dbc", "dbpro"), "Command", "SET OBJECT TO CAMERA ORIENTATION",
     "set object to camera orientation 1\n"),
    (("dbc", "dbpro"), "Command", "SET POINT LIGHT", "set point light 2, 1, 2, 3\n"),
    (("dbc", "dbpro"), "Command", "SET SPOT LIGHT", "set spot light 2, 30, 60\n"),
    (("dbpro",), "Command", "SET VECTOR3 TO CAMERA POSITION", "set vector3 to camera position 2, 1\n"),
    (("dbpro",), "Command", "SET VECTOR3 TO CAMERA ROTATION", "set vector3 to camera rotation 3, 1\n"),
    (("dbpro",), "Command", "SET VECTOR3 TO LIGHT POSITION", "set vector3 to light position 5, 2\n"),
    (("dbpro",), "Command", "SET VECTOR3 TO LIGHT ROTATION", "set vector3 to light rotation 6, 2\n"),
    (("dbpro",), "Command", "SHOW LIGHT", "show light 2\n"),
    (("dbpro",), "Command", "TURN CAMERA LEFT", "turn camera left 1, 2.0\n"),
    (("dbpro",), "Command", "TURN CAMERA RIGHT", "turn camera right 1, 2.0\n"),
    (("dbc",), "Command", "XROTATE CAMERA", "xrotate camera 10.0\n"),
    (("dbpro",), "Command", "XROTATE CAMERA", "xrotate camera 1, 10.0\n"),
    (("dbpro",), "Command", "YROTATE CAMERA", "yrotate camera 1, 20.0\n"),
    (("dbc",), "Command", "ZROTATE CAMERA", "zrotate camera 30.0\n"),
    (("dbpro",), "Command", "ZROTATE CAMERA", "zrotate camera 1, 30.0\n"),

    (("dbc", "dbpro"), "Function", "LIGHT DIRECTION X", "value# = light direction x(2)\n"),
    (("dbc", "dbpro"), "Function", "LIGHT DIRECTION Y", "value# = light direction y(2)\n"),
    (("dbc", "dbpro"), "Function", "LIGHT DIRECTION Z", "value# = light direction z(2)\n"),
    (("dbc", "dbpro"), "Function", "LIGHT EXIST", "value = light exist(2)\n"),
    (("dbc", "dbpro"), "Function", "LIGHT POSITION X", "value# = light position x(2)\n"),
    (("dbc", "dbpro"), "Function", "LIGHT POSITION Y", "value# = light position y(2)\n"),
    (("dbc", "dbpro"), "Function", "LIGHT POSITION Z", "value# = light position z(2)\n"),
    (("dbc", "dbpro"), "Function", "LIGHT RANGE", "value# = light range(2)\n"),
    (("dbc", "dbpro"), "Function", "LIGHT TYPE", "value = light type(2)\n"),
    (("dbc", "dbpro"), "Function", "LIGHT VISIBLE", "value = light visible(2)\n"),
    (("dbpro",), "Function", "AMBIENT LIGHT", "value = ambient light()\n"),
    (("dbpro",), "Function", "CAMERA EXIST", "value = camera exist(1)\n"),
    (("dbpro",), "Function", "GET MAXIMUM LIGHTS", "value = get maximum lights()\n"),
)

UNIT_WITNESSES += tuple(
    (Profile, Kind, Name, Code, "engine-state", CAMERA_LIGHT_TEST)
    for Profiles, Kind, Name, Code in LOT20_WITNESS_GROUPS
    for Profile in Profiles
)

OBJECT_STATE_LIMB_TEST = (
    "test_engine_builtins.cpp::Portable object state animation and limbs preserve hierarchy and overload semantics"
)

OBJECT_RENDER_FLAGS_TEST = (
    "test_engine_builtins.cpp::DBPro object render flags drive portable renderer state"
)

LOT30_WITNESS_GROUPS = (
    ("SET OBJECT AMBIENT", "set object ambient 1, 0\n"),
    ("SET OBJECT CULL", "set object cull 1, 1\n"),
    ("SET OBJECT FILTER", "set object filter 1, 0\n"),
    ("SET OBJECT FILTER", "set object filter 1, 0, 0\n"),
    ("SET OBJECT FOG", "set object fog 1, 0\n"),
    ("SET OBJECT LIGHT", "set object light 1, 0\n"),
    ("SET OBJECT WIREFRAME", "set object wireframe 1, 1\n"),
)

UNIT_WITNESSES += tuple(
    ("dbpro", "Command", Name, Code, "render-state", OBJECT_RENDER_FLAGS_TEST)
    for Name, Code in LOT30_WITNESS_GROUPS
)

OBJECT_MATERIAL_TEST = (
    "test_engine_builtins.cpp::DBPro object materials drive the stock shader"
)

LOT31_WITNESS_GROUPS = (
    ("SET OBJECT AMBIENCE", "set object ambience 1, rgb(255, 255, 255)\n"),
    ("SET OBJECT DIFFUSE", "set object diffuse 1, rgb(255, 255, 255)\n"),
    ("SET OBJECT DIFFUSE", "set object diffuse 1, rgb(255, 255, 255), 1\n"),
    ("SET OBJECT EMISSIVE", "set object emissive 1, rgb(255, 0, 0)\n"),
    ("SET OBJECT SPECULAR", "set object specular 1, rgb(255, 255, 255)\n"),
    ("SET OBJECT SPECULAR POWER", "set object specular power 1, 7.5\n"),
)

UNIT_WITNESSES += tuple(
    ("dbpro", "Command", Name, Code, "material-pixel", OBJECT_MATERIAL_TEST)
    for Name, Code in LOT31_WITNESS_GROUPS
)

OBJECT_NORMAL_TEST = (
    "test_engine_builtins.cpp::DBPro object and limb normal commands rewrite rendered geometry"
)

LOT32_WITNESS_GROUPS = (
    ("SET OBJECT NORMALS", "set object normals 1\n"),
    ("SET OBJECT SMOOTHING", "set object smoothing 1, 100\n"),
    ("SET LIMB NORMALS", "set limb normals 1, 0\n"),
    ("SET LIMB SMOOTHING", "set limb smoothing 1, 0, 100\n"),
)

UNIT_WITNESSES += tuple(
    ("dbpro", "Command", Name, Code, "geometry-render", OBJECT_NORMAL_TEST)
    for Name, Code in LOT32_WITNESS_GROUPS
)

LOT21_WITNESS_GROUPS = (
    (("dbpro",), "Command", "ADD LIMB", "add limb 2, 1, 7\n"),
    (("dbc",), "Command", "COLOR OBJECT", "color object 1, rgb(1, 2, 3)\n"),
    (("dbc", "dbpro"), "Command", "DISABLE OBJECT ZDEPTH", "disable object zdepth 1\n"),
    (("dbc", "dbpro"), "Command", "ENABLE OBJECT ZDEPTH", "enable object zdepth 1\n"),
    (("dbc",), "Command", "FADE OBJECT", "fade object 1, 50\n"),
    (("dbc", "dbpro"), "Command", "FIX OBJECT PIVOT", "fix object pivot 1\n"),
    (("dbc",), "Command", "GHOST OBJECT OFF", "ghost object off 1\n"),
    (("dbc",), "Command", "GHOST OBJECT ON", "ghost object on 1, 1\n"),
    (("dbc",), "Command", "GLUE OBJECT TO LIMB", "glue object to limb 1, 2, 0\n"),
    (("dbpro",), "Command", "GLUE OBJECT TO LIMB", "glue object to limb 1, 2, 0, 2\n"),
    (("dbpro",), "Command", "HIDE LIMB", "hide limb 1, 0\n"),
    (("dbc",), "Command", "HIDE OBJECT", "hide object 1\n"),
    (("dbc", "dbpro"), "Command", "LINK LIMB", "link limb 1, 0, 1\n"),
    (("dbc", "dbpro"), "Command", "LOCK OBJECT OFF", "lock object off 1\n"),
    (("dbc", "dbpro"), "Command", "LOCK OBJECT ON", "lock object on 1\n"),
    (("dbc",), "Command", "LOOP OBJECT", "loop object 1, 0, 1\n"),
    (("dbpro",), "Command", "OFFSET LIMB", "offset limb 1, 0, 1, 2, 3, 1\n"),
    (("dbc",), "Command", "PLAY OBJECT", "play object 1, 0, 1\n"),
    (("dbpro",), "Command", "REMOVE LIMB", "remove limb 1, 1\n"),
    (("dbpro",), "Command", "ROTATE LIMB", "rotate limb 1, 0, 1, 2, 3, 1\n"),
    (("dbc",), "Command", "SET OBJECT FRAME", "set object frame 1, 1.5\n"),
    (("dbc", "dbpro"), "Command", "SET OBJECT INTERPOLATION", "set object interpolation 1, 75\n"),
    (("dbc", "dbpro"), "Command", "SET OBJECT ROTATION XYZ", "set object rotation xyz 1\n"),
    (("dbc",), "Command", "SET OBJECT ROTATION ZYX", "set object rotation zyx 1\n"),
    (("dbc", "dbpro"), "Command", "SET OBJECT SPEED", "set object speed 1, 125\n"),
    (("dbc",), "Command", "SET OBJECT TO OBJECT ORIENTATION", "set object to object orientation 1, 2\n"),
    (("dbpro",), "Command", "SET OBJECT TO OBJECT ORIENTATION", "set object to object orientation 1, 2, 1\n"),
    (("dbpro",), "Command", "SHOW LIMB", "show limb 1, 0\n"),
    (("dbc",), "Command", "SHOW OBJECT", "show object 1\n"),
    (("dbc", "dbpro"), "Command", "STOP OBJECT", "stop object 1\n"),
    (("dbc",), "Command", "TEXTURE LIMB", "texture limb 1, 0, 0\n"),
    (("dbc", "dbpro"), "Command", "UNGLUE OBJECT", "unglue object 1\n"),

    (("dbpro",), "Function", "LIMB ANGLE X", "value# = limb angle x(1, 0)\n"),
    (("dbpro",), "Function", "LIMB ANGLE Y", "value# = limb angle y(1, 0)\n"),
    (("dbpro",), "Function", "LIMB ANGLE Z", "value# = limb angle z(1, 0)\n"),
    (("dbc", "dbpro"), "Function", "LIMB DIRECTION X", "value# = limb direction x(1, 0)\n"),
    (("dbc", "dbpro"), "Function", "LIMB DIRECTION Y", "value# = limb direction y(1, 0)\n"),
    (("dbc", "dbpro"), "Function", "LIMB DIRECTION Z", "value# = limb direction z(1, 0)\n"),
    (("dbpro",), "Function", "LIMB EXIST", "value = limb exist(1, 0)\n"),
    (("dbpro",), "Function", "LIMB OFFSET X", "value# = limb offset x(1, 0)\n"),
    (("dbpro",), "Function", "LIMB OFFSET Y", "value# = limb offset y(1, 0)\n"),
    (("dbpro",), "Function", "LIMB OFFSET Z", "value# = limb offset z(1, 0)\n"),
    (("dbc", "dbpro"), "Function", "LIMB POSITION X", "value# = limb position x(1, 0)\n"),
    (("dbc", "dbpro"), "Function", "LIMB POSITION Y", "value# = limb position y(1, 0)\n"),
    (("dbc", "dbpro"), "Function", "LIMB POSITION Z", "value# = limb position z(1, 0)\n"),
    (("dbpro",), "Function", "LIMB VISIBLE", "value = limb visible(1, 0)\n"),
    (("dbc", "dbpro"), "Function", "OBJECT FRAME", "value# = object frame(1)\n"),
    (("dbc", "dbpro"), "Function", "OBJECT INTERPOLATION", "value# = object interpolation(1)\n"),
    (("dbc",), "Function", "OBJECT LOOPING", "value = object looping(1)\n"),
    (("dbc",), "Function", "OBJECT PLAYING", "value = object playing(1)\n"),
    (("dbc", "dbpro"), "Function", "OBJECT SPEED", "value# = object speed(1)\n"),
    (("dbc",), "Function", "OBJECT VISIBLE", "value = object visible(1)\n"),
    (("dbc", "dbpro"), "Function", "TOTAL OBJECT FRAMES", "value# = total object frames(1)\n"),
)

UNIT_WITNESSES += tuple(
    (Profile, Kind, Name, Code, "engine-state", OBJECT_STATE_LIMB_TEST)
    for Profiles, Kind, Name, Code in LOT21_WITNESS_GROUPS
    for Profile in Profiles
)

MUTABLE_MESH_DBO_TEST = (
    "test_engine_builtins.cpp::Portable mesh vertex data memblocks and DBO preserve mutable geometry"
)

LOT22_WITNESS_GROUPS = (
    (("dbpro",), "Command", "ADD MESH TO VERTEXDATA", "add mesh to vertexdata 11\n"),
    (("dbc", "dbpro"), "Command", "CHANGE MESH", "change mesh 1, 0, 10\n"),
    (("dbc", "dbpro"), "Command", "CHANGE MESH FROM MEMBLOCK", "change mesh from memblock 10, 1\n"),
    (("dbpro",), "Command", "CONVERT MESH TO VERTEXDATA", "convert mesh to vertexdata 10\n"),
    (("dbc", "dbpro"), "Command", "DELETE MESH", "delete mesh 10\n"),
    (("dbpro",), "Command", "DELETE MESH FROM VERTEXDATA", "delete mesh from vertexdata 3, 5, 3, 5\n"),
    (("dbpro",), "Command", "INSTANCE OBJECT", "instance object 2, 1\n"),
    (("dbc", "dbpro"), "Command", "LOAD MESH", "load mesh \"mesh.x\", 10\n"),
    (("dbpro",), "Command", "LOCK VERTEXDATA FOR LIMB", "lock vertexdata for limb 1, 0\n"),
    (("dbpro",), "Command", "LOCK VERTEXDATA FOR LIMB", "lock vertexdata for limb 1, 0, 2\n"),
    (("dbpro",), "Command", "LOCK VERTEXDATA FOR MESH", "lock vertexdata for mesh 10\n"),
    (("dbc", "dbpro"), "Command", "MAKE MEMBLOCK FROM MESH", "make memblock from mesh 1, 10\n"),
    (("dbc", "dbpro"), "Command", "MAKE MESH FROM MEMBLOCK", "make mesh from memblock 10, 1\n"),
    (("dbpro",), "Command", "MAKE MESH FROM OBJECT", "make mesh from object 10, 1\n"),
    (("dbpro",), "Command", "MAKE MESH FROM OBJECT", "make mesh from object 10, 1, 1\n"),
    (("dbpro",), "Command", "MAKE OBJECT", "make object 1, 10, 0\n"),
    (("dbpro",), "Command", "MAKE OBJECT FROM LIMB", "make object from limb 2, 1, 0\n"),
    (("dbpro",), "Command", "MAKE OBJECT FROM LIMB", "make object from limb 2, 1, 0, 1\n"),
    (("dbpro",), "Command", "SAVE MESH", "save mesh \"mesh.x\", 10\n"),
    (("dbpro",), "Command", "SAVE OBJECT", "save object \"object.dbo\", 1\n"),
    (("dbpro",), "Command", "SET INDEXDATA", "set indexdata 0, 1\n"),
    (("dbpro",), "Command", "SET VERTEXDATA DIFFUSE", "set vertexdata diffuse 0, -1\n"),
    (("dbpro",), "Command", "SET VERTEXDATA NORMALS", "set vertexdata normals 0, 0, 1, 0\n"),
    (("dbpro",), "Command", "SET VERTEXDATA POSITION", "set vertexdata position 0, 1, 2, 3\n"),
    (("dbpro",), "Command", "SET VERTEXDATA UV", "set vertexdata uv 0, 0.25, 0.75\n"),
    (("dbpro",), "Command", "SET VERTEXDATA UV", "set vertexdata uv 0, 3, 0.25, 0.75\n"),
    (("dbpro",), "Command", "UNLOCK VERTEXDATA", "unlock vertexdata\n"),

    (("dbpro",), "Function", "GET INDEXDATA", "value = get indexdata(0)\n"),
    (("dbpro",), "Function", "GET VERTEXDATA DIFFUSE", "value = get vertexdata diffuse(0)\n"),
    (("dbpro",), "Function", "GET VERTEXDATA INDEX COUNT", "value = get vertexdata index count()\n"),
    (("dbpro",), "Function", "GET VERTEXDATA NORMALS X", "value# = get vertexdata normals x(0)\n"),
    (("dbpro",), "Function", "GET VERTEXDATA NORMALS Y", "value# = get vertexdata normals y(0)\n"),
    (("dbpro",), "Function", "GET VERTEXDATA NORMALS Z", "value# = get vertexdata normals z(0)\n"),
    (("dbpro",), "Function", "GET VERTEXDATA POSITION X", "value# = get vertexdata position x(0)\n"),
    (("dbpro",), "Function", "GET VERTEXDATA POSITION Y", "value# = get vertexdata position y(0)\n"),
    (("dbpro",), "Function", "GET VERTEXDATA POSITION Z", "value# = get vertexdata position z(0)\n"),
    (("dbpro",), "Function", "GET VERTEXDATA U", "value# = get vertexdata u(0)\n"),
    (("dbpro",), "Function", "GET VERTEXDATA U", "value# = get vertexdata u(0, 3)\n"),
    (("dbpro",), "Function", "GET VERTEXDATA V", "value# = get vertexdata v(0)\n"),
    (("dbpro",), "Function", "GET VERTEXDATA V", "value# = get vertexdata v(0, 3)\n"),
    (("dbpro",), "Function", "GET VERTEXDATA VERTEX COUNT", "value = get vertexdata vertex count()\n"),
    (("dbc", "dbpro"), "Function", "MESH EXIST", "value = mesh exist(10)\n"),
)

UNIT_WITNESSES += tuple(
    (Profile, Kind, Name, Code, "engine-state", MUTABLE_MESH_DBO_TEST)
    for Profiles, Kind, Name, Code in LOT22_WITNESS_GROUPS
    for Profile in Profiles
)

COLLISION_PICK_TEST = (
    "test_engine_builtins.cpp::Portable collision volumes hit events limb bounds and picking preserve DBPro queries"
)

LOT23_WITNESS_GROUPS = (
    (("dbc", "dbpro"), "Command", "DELETE OBJECT COLLISION BOX", "delete object collision box 1\n"),
    (("dbc", "dbpro"), "Command", "DELETE STATIC OBJECTS", "delete static objects\n"),
    (("dbc", "dbpro"), "Command", "MAKE OBJECT COLLISION BOX",
     "make object collision box 1, -1, -1, -1, 1, 1, 1, 0\n"),
    (("dbc", "dbpro"), "Command", "MAKE STATIC COLLISION BOX",
     "make static collision box -1, -1, -1, 1, 1, 1\n"),
    (("dbpro",), "Command", "PICK SCREEN", "pick screen 320, 240, 10\n"),
    (("dbc", "dbpro"), "Command", "SET OBJECT COLLISION OFF", "set object collision off 1\n"),
    (("dbc", "dbpro"), "Command", "SET OBJECT COLLISION ON", "set object collision on 1\n"),
    (("dbc", "dbpro"), "Command", "SET OBJECT COLLISION TO BOXES", "set object collision to boxes 1\n"),
    (("dbc", "dbpro"), "Command", "SET OBJECT COLLISION TO SPHERES", "set object collision to spheres 1\n"),

    (("dbc",), "Function", "GET OBJECT COLLISION X", "value# = get object collision x(1)\n"),
    (("dbpro",), "Function", "GET OBJECT COLLISION X", "value# = get object collision x()\n"),
    (("dbc",), "Function", "GET OBJECT COLLISION Y", "value# = get object collision y(1)\n"),
    (("dbpro",), "Function", "GET OBJECT COLLISION Y", "value# = get object collision y()\n"),
    (("dbc",), "Function", "GET OBJECT COLLISION Z", "value# = get object collision z(1)\n"),
    (("dbpro",), "Function", "GET OBJECT COLLISION Z", "value# = get object collision z()\n"),
    (("dbpro",), "Function", "GET PICK DISTANCE", "value# = get pick distance()\n"),
    (("dbpro",), "Function", "GET PICK VECTOR X", "value# = get pick vector x()\n"),
    (("dbpro",), "Function", "GET PICK VECTOR Y", "value# = get pick vector y()\n"),
    (("dbpro",), "Function", "GET PICK VECTOR Z", "value# = get pick vector z()\n"),
    (("dbc", "dbpro"), "Function", "GET STATIC COLLISION HIT",
     "value = get static collision hit(0, 0, 0, 1, 1, 1, 1, 0, 0, 2, 1, 1)\n"),
    (("dbc",), "Function", "GET STATIC COLLISION X", "value# = get static collision x()\n"),
    (("dbpro",), "Function", "GET STATIC COLLISION X", "value# = get static collision x()\n"),
    (("dbc",), "Function", "GET STATIC COLLISION Y", "value# = get static collision y()\n"),
    (("dbpro",), "Function", "GET STATIC COLLISION Y", "value# = get static collision y()\n"),
    (("dbc",), "Function", "GET STATIC COLLISION Z", "value# = get static collision z()\n"),
    (("dbpro",), "Function", "GET STATIC COLLISION Z", "value# = get static collision z()\n"),
    (("dbc", "dbpro"), "Function", "OBJECT COLLISION", "value = object collision(1, 2)\n"),
    (("dbc", "dbpro"), "Function", "OBJECT HIT", "value# = object hit(1, 2)\n"),
    (("dbpro",), "Function", "LIMB COLLISION", "value# = limb collision(1, 0, 2, 0)\n"),
    (("dbpro",), "Function", "LIMB HIT", "value# = limb hit(1, 0, 2, 0)\n"),
    (("dbpro",), "Function", "PICK OBJECT", "value = pick object(320, 240, 1, 2)\n"),
    (("dbpro",), "Function", "PICK OBJECT", "value = pick object(320, 240, 1, 2, 0)\n"),
)

UNIT_WITNESSES += tuple(
    (Profile, Kind, Name, Code, "collision-state", COLLISION_PICK_TEST)
    for Profiles, Kind, Name, Code in LOT23_WITNESS_GROUPS
    for Profile in Profiles
)

EFFECT_STATE_TEST = "test_shader_compiler.cpp::Studio compiles a relocatable HLSL effect package"
EFFECT_PIXEL_TEST = "test_shader_compiler.cpp::Compiled HLSL effects replace the portable object pipeline at runtime"

LOT25_WITNESS_GROUPS = (
    ("Command", "DELETE EFFECT", "delete effect 1\n", "effect-state", EFFECT_STATE_TEST),
    ("Command", "LOAD EFFECT", 'load effect "solid.fx", 1, 0, 1\n', "effect-state", EFFECT_STATE_TEST),
    ("Command", "SET EFFECT ON", 'set effect on 1, "solid.fx", 0\n', "effect-state", EFFECT_STATE_TEST),
    ("Command", "SET LIMB EFFECT", "set limb effect 1, 0, 1\n", "effect-state", EFFECT_STATE_TEST),
    ("Command", "SET OBJECT EFFECT", "set object effect 1, 1\n", "effect-pixel", EFFECT_PIXEL_TEST),
    ("Function", "EFFECT EXIST", "value = effect exist(1)\n", "effect-state", EFFECT_STATE_TEST),
)

UNIT_WITNESSES += tuple(
    ("dbpro", Kind, Name, Code, Oracle, Test)
    for Kind, Name, Code, Oracle, Test in LOT25_WITNESS_GROUPS
)

EFFECT_CONTROL_TEST = (
    "test_shader_compiler.cpp::DBPro effect constants techniques transpose and checklists drive compiled HLSL"
)

LOT26_WITNESS_GROUPS = (
    ("PERFORM CHECKLIST FOR EFFECT ERRORS", "perform checklist for effect errors\n"),
    ("PERFORM CHECKLIST FOR EFFECT VALUES", "perform checklist for effect values 1\n"),
    ("SET EFFECT CONSTANT BOOLEAN", 'set effect constant boolean 1, "Enabled", 1\n'),
    ("SET EFFECT CONSTANT FLOAT", 'set effect constant float 1, "Strength", 1.0\n'),
    ("SET EFFECT CONSTANT INTEGER", 'set effect constant integer 1, "Count", 1\n'),
    ("SET EFFECT CONSTANT MATRIX", 'set effect constant matrix 1, "UserMatrix", 1\n'),
    ("SET EFFECT CONSTANT VECTOR", 'set effect constant vector 1, "Tint", 1\n'),
    ("SET EFFECT TECHNIQUE", 'set effect technique 1, "blue"\n'),
    ("SET EFFECT TRANSPOSE", "set effect transpose 1, 1\n"),
)

UNIT_WITNESSES += tuple(
    ("dbpro", "Command", Name, Code, "effect-control", EFFECT_CONTROL_TEST)
    for Name, Code in LOT26_WITNESS_GROUPS
)

BACKDROP_FOG_TEST = (
    "test_engine_builtins.cpp::Portable backdrops and linear fog preserve profile semantics"
)

LOT29_WITNESSES = (
    ("dbpro", "Command", "BACKDROP OFF", "backdrop off 1\n", "render-state"),
    ("dbpro", "Command", "BACKDROP ON", "backdrop on 1\n", "render-state"),
    ("dbpro", "Command", "FOG COLOR", "fog color rgb(64, 80, 96)\n", "render-pixel"),
    ("dbpro", "Command", "FOG COLOR", "fog color 112, 128, 144\n", "render-pixel"),
    ("dbpro", "Command", "FOG DISTANCE", "fog distance 100, 750\n", "render-pixel"),
    ("dbpro", "Command", "FOG OFF", "fog off\n", "render-pixel"),
    ("dbpro", "Command", "FOG ON", "fog on\n", "render-pixel"),
    ("dbc", "Function", "FOG AVAILABLE", "value = fog available()\n", "capability"),
    ("dbpro", "Function", "FOG AVAILABLE", "value = fog available()\n", "capability"),
)

UNIT_WITNESSES += tuple(
    (Profile, Kind, Name, Code, Oracle, BACKDROP_FOG_TEST)
    for Profile, Kind, Name, Code, Oracle in LOT29_WITNESSES
)

MATRIX_TERRAIN_TEST = (
    "test_engine_builtins.cpp::Portable terrain matrices preserve DBPro grid texture normal and vector contracts"
)

LOT34_DBC_WITNESSES = (
    ("Command", "DELETE MATRIX", "delete matrix 1\n"),
    ("Command", "FILL MATRIX", "fill matrix 1, 0, 1\n"),
    ("Command", "PREPARE MATRIX TEXTURE", "prepare matrix texture 1, 1, 1, 1\n"),
    ("Command", "RANDOMIZE MATRIX", "randomize matrix 1, 10\n"),
    ("Command", "SET MATRIX NORMAL", "set matrix normal 1, 0, 0, 0, 1, 0\n"),
    ("Command", "SET MATRIX TILE", "set matrix tile 1, 0, 0, 1\n"),
    ("Command", "SHIFT MATRIX DOWN", "shift matrix down 1\n"),
    ("Command", "SHIFT MATRIX LEFT", "shift matrix left 1\n"),
    ("Command", "SHIFT MATRIX RIGHT", "shift matrix right 1\n"),
    ("Command", "SHIFT MATRIX UP", "shift matrix up 1\n"),
    ("Function", "MATRIX EXIST", "value = matrix exist(1)\n"),
    ("Function", "MATRIX POSITION X", "value# = matrix position x(1)\n"),
    ("Function", "MATRIX POSITION Y", "value# = matrix position y(1)\n"),
    ("Function", "MATRIX POSITION Z", "value# = matrix position z(1)\n"),
    ("Function", "MATRIX TILE COUNT", "value = matrix tile count(1)\n"),
    ("Function", "MATRIX TILES EXIST", "value = matrix tiles exist(1)\n"),
)

UNIT_WITNESSES += tuple(
    ("dbc", Kind, Name, Code, "matrix-state", MATRIX_TERRAIN_TEST)
    for Kind, Name, Code in LOT34_DBC_WITNESSES
)

DBC_MATRIX_PROFILE_TEST = (
    "test_engine_builtins.cpp::DBC matrices render from below and retain the classic zero tile alias"
)

UNIT_WITNESSES += (
    ("dbc", "Command", "MAKE MATRIX", "make matrix 1, 10, 10, 1, 1\n", "render-profile",
     DBC_MATRIX_PROFILE_TEST),
    ("dbc", "Command", "SET MATRIX TILE", "set matrix tile 1, 0, 0, 0\n", "matrix-state",
     DBC_MATRIX_PROFILE_TEST),
)

REFLECTION_SHADING_TEST = (
    "test_engine_builtins.cpp::DBPro planar reflection shading uses the object as a stencil mirror"
)

UNIT_WITNESSES += (
    ("dbpro", "Command", "SET REFLECTION SHADING ON", "set reflection shading on 1\n", "planar-reflection",
     REFLECTION_SHADING_TEST),
    ("dbpro", "Command", "SET SHADING OFF", "set shading off 1\n", "planar-reflection",
    REFLECTION_SHADING_TEST),
)

SHADOW_SHADING_TEST = (
    "test_engine_builtins.cpp::DBPro shadow shading casts portable stencil volumes"
)

UNIT_WITNESSES += (
    ("dbpro", "Command", "SET SHADOW POSITION", "set shadow position -1, -7, 10, -3\n", "stencil-shadow",
     SHADOW_SHADING_TEST),
    ("dbpro", "Command", "SET SHADOW SHADING OFF", "set shadow shading off 2\n", "stencil-shadow",
     SHADOW_SHADING_TEST),
    ("dbpro", "Command", "SET SHADOW SHADING ON", "set shadow shading on 2\n", "stencil-shadow",
     SHADOW_SHADING_TEST),
    ("dbpro", "Command", "SET SHADOW SHADING ON", "set shadow shading on 2, 7, 250, 1\n",
     "stencil-shadow", SHADOW_SHADING_TEST),
)

ODE_RIGID_BODY_TEST = (
    "test_engine_builtins.cpp::DBPro ODE rigid bodies simulate through the portable Jolt backend"
)

LOT50_ODE_WITNESSES = (
    ("Command", "ODE START", "ode start\n"),
    ("Command", "ODE END", "ode end\n"),
    ("Command", "ODE UPDATE", "ode update\n"),
    ("Command", "ODE UPDATE", "ode update 0.0166667\n"),
    ("Command", "ODE CREATE DYNAMIC SPHERE", "ode create dynamic sphere 2\n"),
    ("Command", "ODE CREATE STATIC BOX", "ode create static box 1\n"),
    ("Command", "ODE CREATE STATIC SPHERE", "ode create static sphere 3\n"),
    ("Command", "ODE DESTROY OBJECT", "ode destroy object 2\n"),
    ("Command", "ODE SET ACTIVE", "ode set active 2, 1\n"),
    ("Command", "ODE SET ANGULAR VELOCITY", "ode set angular velocity 2, 0, 1, 0\n"),
    ("Command", "ODE SET BODY FRICTION", "ode set body friction 2, 0.75\n"),
    ("Command", "ODE SET BODY POSITION", "ode set body position 2, 1, 7, 2\n"),
    ("Command", "ODE SET BODY ROTATION", "ode set body rotation 2, 10, 20, 30\n"),
    ("Command", "ODE SET ENABLED", "ode set enabled 2, 1\n"),
    ("Command", "ODE SET GRAVITY", "ode set gravity 2, 1\n"),
    ("Command", "ODE SET LINEAR VELOCITY", "ode set linear velocity 2, 1, 2, 3\n"),
    ("Command", "ODE SET WORLD GRAVITY", "ode set world gravity 0, -9.81, 0\n"),
    ("Function", "ODE GET BODY HEIGHT", "value# = ode get body height(2)\n"),
    ("Function", "ODE GET BODY LINEAR VELOCITY X", "value# = ode get body linear velocity x(2)\n"),
    ("Function", "ODE GET BODY LINEAR VELOCITY Y", "value# = ode get body linear velocity y(2)\n"),
    ("Function", "ODE GET BODY LINEAR VELOCITY Z", "value# = ode get body linear velocity z(2)\n"),
)

UNIT_WITNESSES += tuple(
    ("dbpro", Kind, Name, Code, "jolt-rigid-body", ODE_RIGID_BODY_TEST)
    for Kind, Name, Code in LOT50_ODE_WITNESSES
)


UNIT_WITNESSES += tuple(
    (profile, kind, name, code, "xr-state-and-vector",
     "test_vr.cpp::VR builtins expose shared poses and validate destination vectors in both profiles")
    for profile in ("dbc", "dbpro")
    for kind, name, code in (
        ('Command', 'SET VR ENABLED', 'set vr enabled 0\n'),
        ('Command', 'SET TRACKING SPACE', 'set tracking space 0\n'),
        ('Command', 'RECENTER VR CAMERA', 'recenter vr camera\n'),
        ('Command', 'GET VR CONTROLLER POSITION', 'get vr controller position 0, 1\n'),
        ('Command', 'GET VR CONTROLLER ROTATION', 'get vr controller rotation 1, 1\n'),
        ('Command', 'GET VR HEAD POSITION', 'get vr head position 1\n'),
        ('Command', 'GET VR ROTATION', 'get vr rotation 1\n'),
        ('Function', 'IS VR CONTROLLER TRACKED', 'value = is vr controller tracked(0)\n'),
        ('Function', 'IS VR HEAD TRACKED', 'value = is vr head tracked()\n'),
        ('Function', 'IS VR SUPPORTED', 'value = is vr supported()\n'),
        ('Function', 'IS VR ENABLED', 'value = is vr enabled()\n'),
        ('Command', 'SET VR PREVIEW MODE', 'set vr preview mode 2\n'),
        ('Command', 'SET VR CAMERA HEIGHT', 'set vr camera height 1.5\n'),
        ('Function', 'GET VR CAMERA HEIGHT', 'value = get vr camera height()\n'),
    )
)
UNIT_WITNESSES += tuple(
    ("dbc", "Function", axis + " VECTOR3", "value = " + axis.lower() + " vector3(7)\n",
     "xr-state-and-vector",
     "test_vr.cpp::VR builtins expose shared poses and validate destination vectors in both profiles")
    for axis in ("X", "Y", "Z")
)


BSP_WORLD_TEST = "test_engine_builtins.cpp::DBPro BSP worlds load, draw the leaves their camera sees and delete"
UNIT_WITNESSES += tuple(
    ("dbpro", "Command", name, code, "bsp-world", BSP_WORLD_TEST)
    for name, code in (
        ("LOAD BSP", 'load bsp "ikzdm1.pk3", "ikzdm1.bsp"\n'),
        ("DELETE BSP", "delete bsp\n"),
        ("SET BSP CAMERA", "set bsp camera 1\n"),
    )
)
BSP_COLLISION_TEST = "test_engine_builtins.cpp::DBPro BSP collision keeps cameras and objects on the world's open side"
UNIT_WITNESSES += tuple(
    ("dbpro", kind, name, code, "bsp-collision", BSP_COLLISION_TEST)
    for kind, name, code in (
        ("Command", "SET BSP CAMERA COLLISION", "set bsp camera collision 1, 0, 0.75, 0\n"),
        ("Command", "SET BSP OBJECT COLLISION", "set bsp object collision 2, 1, 0.5, 1\n"),
        ("Command", "SET BSP CAMERA COLLISION RADIUS", "set bsp camera collision radius 3, 0, 1, 1, 1\n"),
        ("Command", "SET BSP OBJECT COLLISION RADIUS", "set bsp object collision radius 2, 1, 2, 4, 2\n"),
        ("Command", "SET BSP COLLISION OFF", "set bsp collision off 1\n"),
        ("Command", "SET BSP COLLISION THRESHHOLD", "set bsp collision threshhold 1, 0.001\n"),
        ("Command", "SET BSP COLLISION HEIGHT ADJUSTMENT", "set bsp collision height adjustment 1, 1.0\n"),
        ("Command", "PROCESS BSP COLLISION", "process bsp collision 1\n"),
        ("Function", "BSP COLLISION HIT", "value = bsp collision hit(1)\n"),
        ("Function", "BSP COLLISION X", "value# = bsp collision x(1)\n"),
        ("Function", "BSP COLLISION Y", "value# = bsp collision y(1)\n"),
        ("Function", "BSP COLLISION Z", "value# = bsp collision z(1)\n"),
    )
)

def main() -> None:
    parser = argparse.ArgumentParser(description="Regenerate catalogue-linked BASIC behavior witnesses")
    parser.add_argument("--opendbc", type=Path, required=True)
    parser.add_argument("--output", type=Path,
                        default=ROOT / "tests/unit/builtin_behavior_manifest.tsv")
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()

    witnesses: dict[tuple[str, int, int], tuple[str, str, str, str, str]] = {}
    runtime_outputs = tempfile.TemporaryDirectory(prefix="opendbc-behavior-output-")
    output_root = Path(runtime_outputs.name)
    directory = ROOT / "tests/dba"
    profiles = load_test_profiles(ROOT)
    sources = sorted(directory.glob("test_*.dba"))
    for source in sources:
        profile = profiles[source.name]
        expected = source.with_suffix(".expected")
        if not expected.is_file() or not expected.read_text().strip():
            raise SystemExit(f"behavior witness has no non-empty oracle: {source}")
        result = subprocess.run([str(args.opendbc), "inspect", str(source), "--json",
                                 "--dialect", profile],
                                text=True, capture_output=True, check=False)
        if result.returncode != 0:
            raise SystemExit(f"cannot inspect behavior witness {source}:\n{result.stderr}")
        inspection = json.loads(result.stdout)
        output = output_root / f"{source.stem}-{source.suffix[1:]}.out"
        run = subprocess.run([
            str(args.opendbc), "run", str(source), "--asset-root", str(source.parent),
            "--dialect", profile, "--log-output", str(output), "--max-frames", "5",
        ], text=True, capture_output=True, check=False)
        if run.returncode != 0:
            raise SystemExit(f"cannot execute behavior witness {source}:\n{run.stderr}")
        expected_bytes = expected.read_bytes()
        if not output.is_file() or output.read_bytes() != expected_bytes:
            raise SystemExit(f"behavior witness output differs from its oracle: {source}")
        digest = hashlib.sha256(expected_bytes).hexdigest()
        for use in inspection["builtin_overloads"]:
            key = (profile, int(use["symbol_id"]), int(use["overload_id"]))
            witnesses.setdefault(key, (use["kind"], use["name"], "golden-output", source.name, digest))

    with tempfile.TemporaryDirectory(prefix="opendbc-behavior-") as temporary:
        for profile, kind, name, code, oracle, test in UNIT_WITNESSES:
            source = Path(temporary) / "witness.dba"
            source.write_text(code)
            result = subprocess.run([str(args.opendbc), "inspect", str(source), "--json",
                                     "--dialect", profile],
                                    text=True, capture_output=True, check=False)
            if result.returncode != 0:
                raise SystemExit(f"cannot inspect unit behavior witness {name}:\n{result.stderr}")
            inspection = json.loads(result.stdout)
            matches = [use for use in inspection["builtin_overloads"]
                       if use["kind"] == kind and use["name"] == name]
            if len(matches) != 1:
                raise SystemExit(f"unit behavior witness did not resolve exactly one {kind} {name}")
            use = matches[0]
            key = (profile, int(use["symbol_id"]), int(use["overload_id"]))
            witnesses[key] = (kind, name, oracle, test, "-")

    rows = ["profile\tsymbol_id\toverload_id\tkind\tname\toracle\ttest\toracle_sha256"]
    for (profile, symbol_id, overload_id), (kind, name, oracle, test, digest) in sorted(witnesses.items()):
        rows.append(f"{profile}\t{symbol_id}\t{overload_id}\t{kind}\t{name}\t{oracle}\t{test}\t{digest}")
    Generated = "\n".join(rows) + "\n"
    if args.check:
        if not args.output.is_file() or args.output.read_text() != Generated:
            raise SystemExit(f"behavior manifest is stale: regenerate {args.output}")
        print(f"Behavior manifest is current: {len(witnesses)} witnesses")
    else:
        args.output.write_text(Generated)
        print(f"Wrote {len(witnesses)} builtin behavior witnesses to {args.output}")


if __name__ == "__main__":
    main()
