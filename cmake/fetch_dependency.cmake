# Pinned dependencies are fetched as shallow clones of their exact commit, submodules included,
# into a cache that every build directory shares: a new preset or a deleted build directory
# reuses the sources instead of cloning full histories again.
set(OPENDBC_DEPENDENCY_CACHE "${CMAKE_SOURCE_DIR}/build/_deps-cache" CACHE PATH
    "Shared source cache for pinned dependencies")

find_package(Git REQUIRED)

function(opendbc_run_git)
    execute_process(COMMAND "${GIT_EXECUTABLE}" ${ARGN}
        RESULT_VARIABLE OPENDBC_GIT_RESULT
        OUTPUT_VARIABLE OPENDBC_GIT_OUTPUT
        ERROR_VARIABLE OPENDBC_GIT_ERROR)
    if(NOT OPENDBC_GIT_RESULT EQUAL 0)
        list(JOIN ARGN " " OPENDBC_GIT_COMMAND)
        message(FATAL_ERROR "git ${OPENDBC_GIT_COMMAND} failed:\n${OPENDBC_GIT_OUTPUT}${OPENDBC_GIT_ERROR}")
    endif()
endfunction()

# opendbc_fetch_dependency(<name> <repository> <commit> [FetchContent_Declare options...])
# Declares <name> with SOURCE_DIR set to the cached checkout; further options such as
# PATCH_COMMAND and SOURCE_SUBDIR are passed through.
function(opendbc_fetch_dependency Name Repository Commit)
    if(NOT Commit MATCHES "^[0-9a-f]+$")
        message(FATAL_ERROR "${Name}: pin a commit hash, not '${Commit}'")
    endif()
    string(TOLOWER "${Name}" OPENDBC_LOWER_NAME)
    string(SUBSTRING "${Commit}" 0 12 OPENDBC_SHORT_COMMIT)
    set(OPENDBC_SOURCE "${OPENDBC_DEPENDENCY_CACHE}/${OPENDBC_LOWER_NAME}-${OPENDBC_SHORT_COMMIT}")
    if(NOT EXISTS "${OPENDBC_SOURCE}/.opendbc-fetched")
        message(STATUS "Fetching ${Name} ${OPENDBC_SHORT_COMMIT}")
        # A private staging directory keeps an interrupted or concurrent fetch out of the cache.
        string(RANDOM LENGTH 8 OPENDBC_STAGING_SUFFIX)
        set(OPENDBC_STAGING "${OPENDBC_SOURCE}.partial-${OPENDBC_STAGING_SUFFIX}")
        file(MAKE_DIRECTORY "${OPENDBC_STAGING}")
        opendbc_run_git(-C "${OPENDBC_STAGING}" init --quiet)
        opendbc_run_git(-C "${OPENDBC_STAGING}" fetch --quiet --depth 1 "${Repository}" "${Commit}")
        opendbc_run_git(-C "${OPENDBC_STAGING}" -c advice.detachedHead=false checkout --quiet FETCH_HEAD)
        if(EXISTS "${OPENDBC_STAGING}/.gitmodules")
            opendbc_run_git(-C "${OPENDBC_STAGING}" submodule update --quiet --init --recursive --depth 1)
        endif()
        file(TOUCH "${OPENDBC_STAGING}/.opendbc-fetched")
        if(EXISTS "${OPENDBC_SOURCE}/.opendbc-fetched")
            file(REMOVE_RECURSE "${OPENDBC_STAGING}")
        else()
            file(REMOVE_RECURSE "${OPENDBC_SOURCE}")
            file(RENAME "${OPENDBC_STAGING}" "${OPENDBC_SOURCE}")
        endif()
    endif()
    FetchContent_Declare(${Name} SOURCE_DIR "${OPENDBC_SOURCE}" ${ARGN})
endfunction()
