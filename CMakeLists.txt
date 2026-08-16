cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Optional, Kconfig-gated capabilities (audio, later others) live outside
# components/ - see modules/README.md for the contract each one follows.
set(EXTRA_COMPONENT_DIRS modules)

project(layout_carousel)
