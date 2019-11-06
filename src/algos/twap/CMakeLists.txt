file(GLOB_RECURSE SRC_FILES *.cc)
get_filename_component(name ${CMAKE_CURRENT_SOURCE_DIR} NAME)
add_library(${name}_static STATIC twap.cc)
add_library(${name} MODULE ${SRC_FILES})
