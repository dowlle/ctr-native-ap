# APVendorCheck.cmake -- configure-time gate for the vendored AP deps (issue #78).
#
# Included from the CTR_AP block of the top-level CMakeLists.txt. Refuses to
# configure a vendor tree that does not match ap/vendor/versions.lock, so a
# silently-swapped dependency (an RC once carried apclientpp 0.6.0 instead of
# the tested 0.6.4) fails here instead of shipping. Three checks per dep:
#   1. the dir exists                        -> FATAL_ERROR + fetch hint
#   2. the version macro matches the lock    -> FATAL_ERROR (free, always on)
#   3. the file-tree hash matches the lock   -> FATAL_ERROR (CTR_AP_VERIFY_VENDOR)
# It also records the resolved set in the configure log and generates
# ap_vendor_versions.h (surfaced at runtime next to CTR_AP_VERSION).
#
# The tree-hash algorithm here is byte-for-byte identical to tree_hash() in
# ap/vendor/fetch-deps.sh: sha256 over sorted "<relpath> <sha256(content)>\n"
# lines, LF-terminated, LC_ALL=C / strcmp path order.

option(CTR_AP_VERIFY_VENDOR
    "Verify vendored AP dependency tree hashes at configure time" ON)

set(_ap_vendor_dir "${CMAKE_SOURCE_DIR}/ap/vendor")
set(_ap_lock "${_ap_vendor_dir}/versions.lock")
if(NOT EXISTS "${_ap_lock}")
    message(FATAL_ERROR
        "AP vendor: ${_ap_lock} not found. This file pins the AP deps; it must "
        "be committed. Restore it before building.")
endif()

# --- deterministic tree hash (mirror of fetch-deps.sh tree_hash) -------------
string(ASCII 10 _ap_LF)
function(_ap_tree_hash dir out_var)
    file(GLOB_RECURSE _files RELATIVE "${dir}" "${dir}/*")
    list(SORT _files)                       # strcmp order == LC_ALL=C sort
    set(_manifest "")
    foreach(_f IN LISTS _files)
        if(_f MATCHES "^\\.git/")
            continue()
        endif()
        file(SHA256 "${dir}/${_f}" _h)
        string(APPEND _manifest "${_f} ${_h}${_ap_LF}")
    endforeach()
    string(SHA256 _tree "${_manifest}")
    set(${out_var} "${_tree}" PARENT_SCOPE)
endfunction()

# --- minimal versions.lock parser -------------------------------------------
# Populates, per dep, the cache-free variables _lk_<dep>_<key>. Records the dep
# order in _ap_deps.
file(STRINGS "${_ap_lock}" _lock_lines)
set(_ap_deps "")
set(_cur "")
foreach(_line IN LISTS _lock_lines)
    if(_line MATCHES "^\\[(.+)\\]$")
        set(_cur "${CMAKE_MATCH_1}")
        list(APPEND _ap_deps "${_cur}")
    elseif(_cur AND _line MATCHES "^[ \t]*([A-Za-z0-9_]+)[ \t]*=[ \t]*(.*)$")
        set(_k "${CMAKE_MATCH_1}")
        set(_v "${CMAKE_MATCH_2}")
        string(STRIP "${_v}" _v)
        set("_lk_${_cur}_${_k}" "${_v}")
    endif()
endforeach()

# --- helper: read a "#define <macro> <value>" out of a header ----------------
function(_ap_read_macro header macro out_var)
    set(${out_var} "" PARENT_SCOPE)
    if(NOT EXISTS "${header}")
        return()
    endif()
    file(STRINGS "${header}" _hit REGEX "^[ \t]*#[ \t]*define[ \t]+${macro}[ \t]")
    if(NOT _hit)
        return()
    endif()
    list(GET _hit 0 _hit)
    # strip everything up to and including the macro name, then trailing comment
    string(REGEX REPLACE "^.*#[ \t]*define[ \t]+${macro}[ \t]+" "" _val "${_hit}")
    string(REGEX REPLACE "(//|/\\*).*$" "" _val "${_val}")
    string(STRIP "${_val}" _val)
    set(${out_var} "${_val}" PARENT_SCOPE)
endfunction()

# whitespace-insensitive equality (macro spelling drift, e.g. "{0, 6, 4}")
function(_ap_norm in out_var)
    string(REGEX REPLACE "[ \t]+" "" _n "${in}")
    set(${out_var} "${_n}" PARENT_SCOPE)
endfunction()

# --- run the checks ----------------------------------------------------------
set(_ap_versionline "")
foreach(_dep IN LISTS _ap_deps)
    if("${_lk_${_dep}_optional}" STREQUAL "true")
        continue()
    endif()
    set(_dir "${_ap_vendor_dir}/${_lk_${_dep}_dir}")
    set(_ver "${_lk_${_dep}_version}")

    # 1. presence
    if(NOT IS_DIRECTORY "${_dir}")
        message(FATAL_ERROR
            "AP vendor: '${_lk_${_dep}_dir}' is missing under ap/vendor/.\n"
            "  Run  ap/vendor/fetch-deps.sh  to fetch the pinned dependencies\n"
            "  (see BUILDING.md). Expected ${_dep} ${_ver} at ${_dir}")
    endif()

    # 2. version macro (free; skip deps whose header constant is unreliable)
    set(_macro "${_lk_${_dep}_macro}")
    if(NOT "${_macro}" STREQUAL "-" AND NOT "${_macro}" STREQUAL "")
        set(_hdr "${_dir}/${_lk_${_dep}_macro_file}")
        if("${_macro}" STREQUAL "NLOHMANN_JSON_VERSION")
            _ap_read_macro("${_hdr}" "NLOHMANN_JSON_VERSION_MAJOR" _jM)
            _ap_read_macro("${_hdr}" "NLOHMANN_JSON_VERSION_MINOR" _jm)
            _ap_read_macro("${_hdr}" "NLOHMANN_JSON_VERSION_PATCH" _jp)
            set(_actual "${_jM}.${_jm}.${_jp}")
        else()
            _ap_read_macro("${_hdr}" "${_macro}" _actual)
        endif()
        _ap_norm("${_actual}" _an)
        _ap_norm("${_lk_${_dep}_macro_value}" _en)
        if(_an STREQUAL "")
            message(FATAL_ERROR
                "AP vendor: could not read ${_macro} from ${_hdr}.\n"
                "  The tree does not look like ${_dep} ${_ver}. Re-run "
                "ap/vendor/fetch-deps.sh.")
        endif()
        if(NOT _an STREQUAL _en)
            message(FATAL_ERROR
                "AP vendor: ${_dep} version mismatch.\n"
                "  expected ${_macro} = ${_lk_${_dep}_macro_value}\n"
                "  found    ${_macro} = ${_actual}\n"
                "  The vendor tree is not the pinned ${_ver}. Delete "
                "ap/vendor/${_lk_${_dep}_dir} and run ap/vendor/fetch-deps.sh.")
        endif()
    endif()

    # 3. tree hash (opt-out via -DCTR_AP_VERIFY_VENDOR=OFF; release forces ON)
    if(CTR_AP_VERIFY_VENDOR)
        _ap_tree_hash("${_dir}" _th)
        if(NOT _th STREQUAL "${_lk_${_dep}_tree_sha256}")
            message(FATAL_ERROR
                "AP vendor: ${_dep} tree hash mismatch (tamper / wrong revision).\n"
                "  expected ${_lk_${_dep}_tree_sha256}\n"
                "  found    ${_th}\n"
                "  Delete ap/vendor/${_lk_${_dep}_dir} and run "
                "ap/vendor/fetch-deps.sh, or set -DCTR_AP_VERIFY_VENDOR=OFF to "
                "skip tree hashing (NOT for release builds).")
        endif()
        message(STATUS "AP vendor: ${_dep} ${_ver} OK (tree ${_th})")
    else()
        message(STATUS "AP vendor: ${_dep} ${_ver} OK (macro; tree hash skipped)")
    endif()

    if(_ap_versionline STREQUAL "")
        set(_ap_versionline "${_dep} ${_ver}")
    else()
        set(_ap_versionline "${_ap_versionline}, ${_dep} ${_ver}")
    endif()
endforeach()

# --- surface the resolved set to the runtime --------------------------------
set(_ap_gen_dir "${CMAKE_BINARY_DIR}/generated/ap")
file(MAKE_DIRECTORY "${_ap_gen_dir}")
file(WRITE "${_ap_gen_dir}/ap_vendor_versions.h"
"#ifndef AP_VENDOR_VERSIONS_H\n"
"#define AP_VENDOR_VERSIONS_H\n"
"// GENERATED at configure time by cmake/APVendorCheck.cmake -- do not edit.\n"
"// The pinned Archipelago vendor set (ap/vendor/versions.lock), surfaced next\n"
"// to CTR_AP_VERSION so a bug report names the exact networking stack.\n"
"#define CTR_AP_VENDOR_VERSIONS \"${_ap_versionline}\"\n"
"#endif // AP_VENDOR_VERSIONS_H\n")
# ctr_native includes it as \"ap/ap_vendor_versions.h\"; add the generated root.
target_include_directories(ctr_native PRIVATE "${CMAKE_BINARY_DIR}/generated")

message(STATUS "AP vendor: resolved set -- ${_ap_versionline}")
