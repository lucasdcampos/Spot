# Compiling through a cache, sccache or ccache, makes compiling a file again nearly free when nothing it reads changed,
# as after switching branches or cleaning a build. Only the Ninja and Makefile generators run one, not Visual Studio's
# or Xcode's. SPOT_USE_COMPILER_CACHE=OFF turns it off, and SPOT_COMPILER_CACHE picks the program.
option(SPOT_USE_COMPILER_CACHE "Compile through sccache or ccache when one is installed" ON)

# Makes the targets created from then on, in the scope it is called from, compile through the cache.
macro(spot_use_compiler_cache)
    if(SPOT_USE_COMPILER_CACHE AND NOT CMAKE_CXX_COMPILER_LAUNCHER AND NOT CMAKE_GENERATOR MATCHES "^(Visual Studio|Xcode)")
        find_program(SPOT_COMPILER_CACHE NAMES sccache ccache)
        if(SPOT_COMPILER_CACHE)
            get_filename_component(spot_compiler_cache_name "${SPOT_COMPILER_CACHE}" NAME_WE)
            if(spot_compiler_cache_name STREQUAL "ccache")
                # ccache passes over files built with precompiled headers unless told what it may ignore for them.
                set(CMAKE_CXX_COMPILER_LAUNCHER
                    "${CMAKE_COMMAND};-E;env;CCACHE_SLOPPINESS=pch_defines,time_macros;${SPOT_COMPILER_CACHE}")
            else()
                set(CMAKE_CXX_COMPILER_LAUNCHER "${SPOT_COMPILER_CACHE}")
            endif()
            set(CMAKE_C_COMPILER_LAUNCHER "${CMAKE_CXX_COMPILER_LAUNCHER}")
            # A cache stores each object whole, so MSVC keeps debug information in the objects rather than in a PDB
            # they share.
            set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<$<CONFIG:Debug,RelWithDebInfo>:Embedded>")
        endif()
    endif()
endmacro()
