set(BUILD_TESTING OFF CACHE BOOL "")

cpmaddpackage(
  NAME unifex
  GITHUB_REPOSITORY facebookexperimental/libunifex
  GIT_TAG 9df21c58d34ce8a1cd3b15c3a7347495e29417a0
  OPTIONS
    "UNIFEX_BUILD_EXAMPLES OFF"
    "CMAKE_CXX_STANDARD 20"
)

if (unifex_ADDED)
  add_library(unifex::unifex ALIAS unifex)
endif()
