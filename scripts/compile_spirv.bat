@echo off
setlocal
for %%S in (dbclassic_tri vr_transfer) do (
  for %%T in (vert frag) do (
    glslangValidator -V --target-env vulkan1.0 -S %%T "%~dp0..\shaders\%%S.%%T.glsl" -o "%~dp0..\shaders\%%S.%%T.spv"
    if errorlevel 1 exit /b 1
  )
)
endlocal
