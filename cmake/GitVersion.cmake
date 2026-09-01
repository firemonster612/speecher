set(SPEECHER_VERSION "${PROJECT_VERSION}")
set(SPEECHER_BUILD_NUMBER 0)

execute_process(
  COMMAND git rev-parse --is-inside-work-tree
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  RESULT_VARIABLE git_repository_result
  OUTPUT_VARIABLE git_repository
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET
)

if(git_repository_result STREQUAL "0" AND git_repository STREQUAL "true")
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

  execute_process(
    COMMAND git describe --exact-match --tags --match "v*"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    RESULT_VARIABLE git_exact_tag_result
    OUTPUT_VARIABLE git_exact_tag
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
  )
  if(git_exact_tag_result STREQUAL "0")
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
      math(EXPR git_next_patch "${CMAKE_MATCH_3} + 1")
      set(git_version_base "${CMAKE_MATCH_1}.${CMAKE_MATCH_2}.${git_next_patch}")
    endif()

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
