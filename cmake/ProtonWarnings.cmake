# SPDX-License-Identifier: AGPL-3.0-only
# proton_warnings: interface target carrying the project's warning flags.
# Link it PRIVATE into every Proton target so third-party code is not affected.

add_library(proton_warnings INTERFACE)

if(MSVC)
    target_compile_options(proton_warnings INTERFACE /W4 /permissive- /utf-8 /Zc:__cplusplus)
    if(PROTON_WARNINGS_AS_ERRORS)
        target_compile_options(proton_warnings INTERFACE /WX)
    endif()
else()
    target_compile_options(proton_warnings INTERFACE -Wall -Wextra -Wpedantic)
    if(PROTON_WARNINGS_AS_ERRORS)
        target_compile_options(proton_warnings INTERFACE -Werror)
    endif()
endif()
