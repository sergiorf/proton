# SPDX-License-Identifier: AGPL-3.0-only
# nlohmann/json (MIT), the JSON library of the project files (PLAN.md M4.11, docs/LICENSING.md):
# header-only, fetched at configure time from its pinned release and checked against its hash.
# Defines the target nlohmann_json::nlohmann_json; its headers are system headers, so Proton's
# warning flags do not apply to them.

include(FetchContent)

FetchContent_Declare(nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
    URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SYSTEM
)
set(JSON_BuildTests OFF CACHE INTERNAL "")
set(JSON_Install OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(nlohmann_json)
