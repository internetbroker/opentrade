subdirlist(SUBDIRS)
foreach(SUBDIR ${SUBDIRS})
  add_subdirectory(${SUBDIR})
endforeach()
