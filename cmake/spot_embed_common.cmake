# Helpers shared by the scripts that embed files into generated headers.

# Turns a file name such as "inter_regular.ttf" or "sprite.vert" into a PascalCase identifier ("InterRegularTtf").
function(spot_to_identifier name output)
    string(REGEX REPLACE "[^A-Za-z0-9]+" ";" words "${name}")
    set(identifier "")
    foreach(word IN LISTS words)
        string(SUBSTRING "${word}" 0 1 first)
        string(SUBSTRING "${word}" 1 -1 rest)
        string(TOUPPER "${first}" first)
        string(APPEND identifier "${first}${rest}")
    endforeach()
    set(${output} "${identifier}" PARENT_SCOPE)
endfunction()

function(spot_to_byte_array file output)
    file(READ "${file}" hex HEX)
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
    # Breaks the array into lines so the generated header stays readable in a diff or a debugger.
    string(REGEX REPLACE "((0x..,){24})" "\\1\n    " bytes "${bytes}")
    set(${output} "${bytes}" PARENT_SCOPE)
endfunction()
