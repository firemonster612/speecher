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
  # the regex is strict on purpose.
  execute_process(
    COMMAND git rev-parse --verify --quiet refs/heads/master
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    RESULT_VARIABLE git_local_master_result
    ERROR_QUIET
  )
  if(git_local_master_result STREQUAL "0")
    set(git_master_ref master)
  else()
    set(git_master_ref origin/master)
  endif()

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
    execute_process(
      COMMAND git merge-base --is-ancestor "${git_exact_tag}" "${git_master_ref}"
      WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
      RESULT_VARIABLE git_exact_tag_on_master_result
      ERROR_QUIET
    )
    if(git_exact_tag_on_master_result STREQUAL "0")
      set(git_exact_tag_is_stable TRUE)
    endif()
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
      execute_process(
        COMMAND git merge-base --is-ancestor "${git_last_tag}" "${git_master_ref}"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        RESULT_VARIABLE git_last_tag_on_master_result
        ERROR_QUIET
      )
      if(git_last_tag_on_master_result STREQUAL "0")
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
