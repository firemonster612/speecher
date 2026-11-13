# Applies PATCH to the current directory, a fetched source tree, unless it is
# applied already, so a configure that runs the patch step again still works.
# GIT is git; CEILING stops git from taking the tree for part of the
# repository it was fetched into, which would make it skip every path.
#   cmake -DGIT=<git> -DPATCH=<file> -DCEILING=<dir> -P ApplyPatch.cmake
set(ENV{GIT_CEILING_DIRECTORIES} "${CEILING}")
execute_process(COMMAND "${GIT}" apply --reverse --check "${PATCH}"
  RESULT_VARIABLE applied OUTPUT_QUIET ERROR_QUIET)
if(applied EQUAL 0)
  return()
endif()
execute_process(COMMAND "${GIT}" apply --whitespace=nowarn "${PATCH}" RESULT_VARIABLE result)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "Could not apply ${PATCH}")
endif()
