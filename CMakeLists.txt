project(shell)
cmake_minimum_required(VERSION 3.0)

OPTION(BUILD_TESTS "Build the tests" OFF)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall")

set(SRC_LIST shell.cpp)
add_library (${PROJECT_NAME}lib ${SRC_LIST})

add_executable(${PROJECT_NAME} main.cpp)
target_link_libraries(${PROJECT_NAME} ${PROJECT_NAME}lib)

if (BUILD_TESTS)
  add_subdirectory(ext/gtest)
  include_directories(${GTEST_INCLUDE_DIRS})
  set (test test.cpp)
  file(GLOB_RECURSE UNITTESTS *.test.cpp)
  add_executable (${PROJECT_NAME}test ${test} ${UNITTESTS})
  target_link_libraries(${PROJECT_NAME}test ${PROJECT_NAME}lib)
  add_dependencies(${PROJECT_NAME}test ${PROJECT_NAME} googletest)
  target_link_libraries(${PROJECT_NAME}test ${GTEST_LIBS_DIR}/libgtest.a ${GTEST_LIBS_DIR}/libgtest_main.a)

  if(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
      find_package(Threads)
      target_link_libraries(${PROJECT_NAME}test ${CMAKE_THREAD_LIBS_INIT})
  endif()
endif()

