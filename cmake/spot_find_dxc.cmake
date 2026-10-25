# Sets SPOT_DXC to the newest DXC of the Windows SDK, unless it is set already. DXIL is only needed for Direct3D 12, so
# it is only looked for on Windows hosts; without it, shaders have no DXIL.
if(CMAKE_HOST_WIN32 AND NOT DEFINED SPOT_DXC)
    file(GLOB windows_sdk_dxc "$ENV{ProgramFiles\(x86\)}/Windows Kits/10/bin/*/x64/dxc.exe")
    if(windows_sdk_dxc)
        list(SORT windows_sdk_dxc COMPARE NATURAL ORDER DESCENDING)
        list(GET windows_sdk_dxc 0 latest_dxc)
        set(SPOT_DXC "${latest_dxc}" CACHE FILEPATH "DXC used to compile shaders to DXIL; empty to skip DXIL")
    endif()
endif()
