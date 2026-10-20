# SPDX-License-Identifier: AGPL-3.0-only
# Builds a small tree in -DWORK=<dir> with good and bad files, runs the SPDX check (-DCHECK=...)
# on it and checks that it fails and names exactly the bad files.

file(REMOVE_RECURSE "${WORK}")
set(line "SPDX-License-Identifier: AGPL-3.0-only")
file(WRITE "${WORK}/CMakeLists.txt" "# ${line}\n")
file(WRITE "${WORK}/src/good.cpp" "// ${line}\nint x;\n")
file(WRITE "${WORK}/src/good_crlf.h" "// ${line}\r\n#pragma once\r\n")
file(WRITE "${WORK}/src/no_line.cpp" "int x;\n")
file(WRITE "${WORK}/src/second_line.h" "#pragma once\n// ${line}\n")
file(WRITE "${WORK}/src/wrong_comment.cpp" "# ${line}\n")
file(WRITE "${WORK}/src/other_license.cpp" "// SPDX-License-Identifier: AGPL-3.0-or-later\n")
file(WRITE "${WORK}/cmake/no_line.cmake" "set(x 1)\n")
file(WRITE "${WORK}/build/ignored.cpp" "int x;\n")

execute_process(COMMAND "${CMAKE_COMMAND}" -DROOT=${WORK} -P "${CHECK}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE output)
message("${output}")

if(result EQUAL 0)
    message(FATAL_ERROR "the check passed on a tree with files missing the SPDX line")
endif()
foreach(bad src/no_line.cpp src/second_line.h src/wrong_comment.cpp src/other_license.cpp
            cmake/no_line.cmake)
    if(NOT output MATCHES "${bad}")
        message(FATAL_ERROR "bad file not reported: ${bad}")
    endif()
endforeach()
foreach(good CMakeLists.txt src/good.cpp src/good_crlf.h build/ignored.cpp)
    if(output MATCHES "${good}")
        message(FATAL_ERROR "file reported wrongly: ${good}")
    endif()
endforeach()
