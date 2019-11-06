add_library(sim_server MODULE sim_server.cc sim_server_file.cc)
target_link_libraries(sim_server ${QUICKFIX_LIBRARY_PATH})
add_library(sim_server_live MODULE sim_server.cc sim_server_live.cc)
target_link_libraries(sim_server_live ${QUICKFIX_LIBRARY_PATH})
