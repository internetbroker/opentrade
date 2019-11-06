add_library(ctpdata MODULE data.cc)
target_link_libraries(ctpdata ${CMAKE_CURRENT_SOURCE_DIR}/api/libthostmduserapi_se.so)
add_library(ctptrade MODULE trade.cc)
target_link_libraries(ctptrade ${CMAKE_CURRENT_SOURCE_DIR}/api/libthosttraderapi_se.so)
