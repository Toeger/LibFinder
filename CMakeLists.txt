cmake_minimum_required(VERSION 3.30)
project(libfinder LANGUAGES CXX C)
set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(SRC_LIST
	asserts.cpp
	cast.cpp
	color.cpp
	format.cpp
	generate.cpp
	libclang.cpp
	libparser.cpp
	libtooling.cpp
	main.cpp
	profile.cpp
	raii.cpp
	symbol_database.cpp
	symbol_matcher.cpp
	system.cpp
	test.cpp
	utility.cpp
)
list(TRANSFORM SRC_LIST REPLACE "\.cpp$" ".h" OUTPUT_VARIABLE HEADER_LIST)
add_executable(${PROJECT_NAME} ${SRC_LIST} ${HEADER_LIST})

find_package(LLVM 21.1 REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE clangTooling)

find_package(Clang 21.1 CONFIG REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE libclang)
target_include_directories(${PROJECT_NAME} SYSTEM PRIVATE ${CLANG_INCLUDE_DIRS})

find_package(Boost COMPONENTS program_options REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE ${Boost_LIBRARIES})

find_package(Threads REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE ${CMAKE_THREAD_LIBS_INIT})

target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(${PROJECT_NAME} SYSTEM PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/external)

set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -ggdb -Wall -Werror -fsanitize=undefined,address -fno-omit-frame-pointer")
target_compile_features(${PROJECT_NAME} PRIVATE cxx_binary_literals)
set(MISC_FILES
	.clang-format
	.gitignore
	CMakeLists.txt
	README.md
)
add_custom_target(MISC_FILES
	COMMENT "This target only exists to list files in IDEs"
	SOURCES ${MISC_FILES}
)

find_package(Catch2 REQUIRED)
aux_source_directory(tests SRC_LIST_TESTS)
list(REMOVE_ITEM SRC_LIST "./main.cpp")
add_executable(${PROJECT_NAME}_tests ${SRC_LIST} ${SRC_LIST_TESTS})
target_include_directories(${PROJECT_NAME}_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(${PROJECT_NAME}_tests SYSTEM PRIVATE ${CLANG_INCLUDE_DIRS})
target_include_directories(${PROJECT_NAME}_tests SYSTEM PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/external)
target_link_libraries(${PROJECT_NAME}_tests PRIVATE libclang ${Boost_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT} Catch2::Catch2WithMain clangTooling)
target_compile_definitions(${PROJECT_NAME}_tests PRIVATE TEST_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests")
aux_source_directory(tests/test_data TESTS_LIST)
add_custom_target(TEST_FILES
	COMMENT "This target only exists to list files in IDEs"
	SOURCES ${TESTS_LIST}
)
