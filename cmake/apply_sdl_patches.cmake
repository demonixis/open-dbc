foreach(OPENDBC_SDL_PATCH IN ITEMS sdl-ios-simulator-metal.patch sdl-openxr-lifecycle.patch)
    set(OPENDBC_PATCH_FILE "${CMAKE_CURRENT_LIST_DIR}/patches/${OPENDBC_SDL_PATCH}")
    include("${CMAKE_CURRENT_LIST_DIR}/apply_idempotent_patch.cmake")
endforeach()
