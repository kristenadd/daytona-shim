# The launcher logo and app icons: the generic ones in the repository, or your
# own named in branding.ini beside CMakeLists.txt (not committed; see
# tools/branding/README.md). Lines are `key = path`, the path absolute or
# relative to branding.ini; a key left out keeps the generic picture.
#
# daytona_branding(<key> <default path> <out var>) sets <out var> to the file to use.
set(DAYTONA_BRANDING_FILE "${CMAKE_CURRENT_SOURCE_DIR}/branding.ini" CACHE FILEPATH
    "logo and icon remap file (tools/branding/README.md)")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${DAYTONA_BRANDING_FILE}")
set(daytona_branding_lines "")
if(EXISTS "${DAYTONA_BRANDING_FILE}")
    file(STRINGS "${DAYTONA_BRANDING_FILE}" daytona_branding_lines)
endif()

function(daytona_branding key default out)
    set(path "${default}")
    foreach(line IN LISTS daytona_branding_lines)
        if(line MATCHES "^[ \t]*${key}[ \t]*=[ \t]*(.*[^ \t\r])[ \t\r]*$")
            set(value "${CMAKE_MATCH_1}")
            get_filename_component(dir "${DAYTONA_BRANDING_FILE}" DIRECTORY)
            get_filename_component(value "${value}" ABSOLUTE BASE_DIR "${dir}")
            if(EXISTS "${value}")
                set(path "${value}")
                message(STATUS "Branding: ${key} from ${value}")
            else()
                message(WARNING "branding.ini: ${key} = ${value} does not exist; using the generic one")
            endif()
        endif()
    endforeach()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${path}")
    set(${out} "${path}" PARENT_SCOPE)
endfunction()
