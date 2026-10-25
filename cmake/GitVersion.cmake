set(SPEECHER_VERSION "${PROJECT_VERSION}")
# CFBundleShortVersionString and CFBundleVersion accept this for DMG distribution,
# but App Store submission would reject it; Speecher's macOS distribution is deliberately DMG-only.
set(SPEECHER_BUILD_NUMBER 0)

execute_process(
  COMMAND git rev-parse --is-inside-work-tree
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  RESULT_VARIABLE git_repository_result
  OUTPUT_VARIABLE git_repository
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET
)
execute_process(
  COMMAND git rev-parse --show-toplevel
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  RESULT_VARIABLE git_toplevel_result
  OUTPUT_VARIABLE git_toplevel
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET
)

if("${git_repository_result}" STREQUAL "0"
   AND "${git_repository}" STREQUAL "true"
   AND "${git_toplevel_result}" STREQUAL "0"
   AND "${git_toplevel}" STREQUAL "${CMAKE_CURRENT_SOURCE_DIR}")
  # Shallow clones truncate rev-list --count and hide tags; CI must checkout with fetch-depth: 0.
  execute_process(
    COMMAND git rev-parse --is-shallow-repository
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    RESULT_VARIABLE git_shallow_result
    OUTPUT_VARIABLE git_shallow
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
  )
  if(git_shallow_result STREQUAL "0" AND git_shallow STREQUAL "true")
    message(WARNING "Shallow Git repository: version build counts and tags may be incomplete. CI must use fetch-depth: 0.")
  endif()

  execute_process(
    COMMAND git rev-list --count HEAD
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    RESULT_VARIABLE git_build_number_result
    OUTPUT_VARIABLE git_build_number
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
  )
  if(git_build_number_result STREQUAL "0")
    set(SPEECHER_BUILD_NUMBER "${git_build_number}")
  endif()

  # Stable tags are vX.Y.Z reachable from master. RC and side-branch tags deliberately fall back;
  # the regex is strict on purpose. Either master counts, since a local one can be behind the
  # tag being rebuilt. A shallow or single-branch clone of the tag (git clone --depth 1 --branch
  # vX.Y.Z) has neither master to check against, so it takes any vX.Y.Z tag as stable, a
  # side-branch one included.
  set(git_master_refs "")
  foreach(ref refs/remotes/origin/master refs/heads/master)
    execute_process(
      COMMAND git rev-parse --verify --quiet "${ref}"
      WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
      RESULT_VARIABLE git_ref_result
      OUTPUT_QUIET
      ERROR_QUIET
    )
    if(git_ref_result STREQUAL "0")
      list(APPEND git_master_refs "${ref}")
    endif()
  endforeach()
  function(speecher_tag_on_master tag result)
    set(on_master FALSE)
    if(NOT git_master_refs)
      set(on_master TRUE)
    endif()
    foreach(ref IN LISTS git_master_refs)
      execute_process(
        COMMAND git merge-base --is-ancestor "${tag}" "${ref}"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        RESULT_VARIABLE ancestor_result
        ERROR_QUIET
      )
      if(ancestor_result STREQUAL "0")
        set(on_master TRUE)
      endif()
    endforeach()
    set(${result} ${on_master} PARENT_SCOPE)
  endfunction()

  execute_process(
    COMMAND git describe --exact-match --tags --match "v*"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    RESULT_VARIABLE git_exact_tag_result
    OUTPUT_VARIABLE git_exact_tag
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
  )
  set(git_exact_tag_is_stable FALSE)
  if(git_exact_tag_result STREQUAL "0"
     AND git_exact_tag MATCHES "^v([0-9]+)\\.([0-9]+)\\.([0-9]+)$")
    speecher_tag_on_master("${git_exact_tag}" git_exact_tag_is_stable)
  endif()

  if(git_exact_tag_is_stable)
    string(REGEX REPLACE "^v" "" SPEECHER_VERSION "${git_exact_tag}")
  else()
    execute_process(
      COMMAND git describe --tags --abbrev=0 --match "v*"
      WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
      RESULT_VARIABLE git_last_tag_result
      OUTPUT_VARIABLE git_last_tag
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_QUIET
    )
    set(git_version_base "${PROJECT_VERSION}")
    if(git_last_tag_result STREQUAL "0"
       AND git_last_tag MATCHES "^v([0-9]+)\\.([0-9]+)\\.([0-9]+)$")
      speecher_tag_on_master("${git_last_tag}" git_last_tag_on_master)
      if(git_last_tag_on_master)
        # Patch+1 makes nightlies sort below the next Stable Release.
        math(EXPR git_next_patch "${CMAKE_MATCH_3} + 1")
        set(git_version_base "${CMAKE_MATCH_1}.${CMAKE_MATCH_2}.${git_next_patch}")
      endif()
    endif()

    # The committer date in UTC makes the nightly version reproducible.
    execute_process(
      COMMAND "${CMAKE_COMMAND}" -E env TZ=UTC git log -1 --format=%cd --date=format-local:%Y%m%d
      WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
      RESULT_VARIABLE git_date_result
      OUTPUT_VARIABLE git_date
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_QUIET
    )
    execute_process(
      COMMAND git rev-parse --short HEAD
      WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
      RESULT_VARIABLE git_sha_result
      OUTPUT_VARIABLE git_short_sha
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_QUIET
    )
    if(git_date_result STREQUAL "0" AND git_sha_result STREQUAL "0")
      set(SPEECHER_VERSION "${git_version_base}-nightly.${git_date}+g${git_short_sha}")
    endif()
  endif()
endif()

# The Updates page lists the commits a nightly update brought in, so every
# build embeds the recent first-parent history. Record separator 0x1e and field
# separator 0x1f keep multi-line commit bodies parseable; core/ReleaseNotes.cpp
# reads the format back. Non-git builds embed an empty file.
set(SPEECHER_RELEASE_HISTORY_DIR "${CMAKE_CURRENT_BINARY_DIR}/release-history")
file(MAKE_DIRECTORY "${SPEECHER_RELEASE_HISTORY_DIR}")
set(speecher_history_staging "${SPEECHER_RELEASE_HISTORY_DIR}/history.log.new")
file(WRITE "${speecher_history_staging}" "")
if("${git_repository_result}" STREQUAL "0"
   AND "${git_repository}" STREQUAL "true"
   AND "${git_toplevel_result}" STREQUAL "0"
   AND "${git_toplevel}" STREQUAL "${CMAKE_CURRENT_SOURCE_DIR}")
  execute_process(
    COMMAND git log -n 200 --first-parent --format=%x1e%h%x1f%s%x1f%b
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    RESULT_VARIABLE git_history_result
    OUTPUT_FILE "${speecher_history_staging}"
    ERROR_QUIET
  )
  if(NOT git_history_result STREQUAL "0")
    file(WRITE "${speecher_history_staging}" "")
  endif()
endif()
# copy_if_different keeps the mtime stable across reconfigures so the resource
# only recompiles when the history actually changed.
execute_process(COMMAND "${CMAKE_COMMAND}" -E copy_if_different
  "${speecher_history_staging}" "${SPEECHER_RELEASE_HISTORY_DIR}/history.log")
file(REMOVE "${speecher_history_staging}")
