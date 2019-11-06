file(GLOB SRC_FILES *.cc)
add_executable(unit_test ${SRC_FILES})
target_link_libraries(unit_test ${EXE_DEPS})
