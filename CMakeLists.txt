project(libfinder)
cmake_minimum_required(VERSION 2.8)
aux_source_directory(. SRC_LIST)
add_executable(${PROJECT_NAME} ${SRC_LIST})
find_package(Boost COMPONENTS filesystem program_options REQUIRED)
find_package(Threads REQUIRED)
target_link_libraries(${PROJECT_NAME} ${Boost_LIBRARIES})
target_link_libraries(${PROJECT_NAME} ${CMAKE_THREAD_LIBS_INIT})
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
	SOURCES ${MISC_FILES} ${UI_FILES}
)
