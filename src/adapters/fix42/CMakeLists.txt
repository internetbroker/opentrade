file(GLOB_RECURSE SRC_FILES *.cc)
get_filename_component(name ${CMAKE_CURRENT_SOURCE_DIR} NAME)
add_library(${name} MODULE ${SRC_FILES})
target_link_libraries(${name} ${QUICKFIX_LIBRARY_PATH})
