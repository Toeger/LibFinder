cmake_minimum_required(VERSION 3.30)
project(libfinder LANGUAGES CXX C)
set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
aux_source_directory(. SRC_LIST)
add_executable(${PROJECT_NAME} ${SRC_LIST})
find_package(Boost COMPONENTS program_options REQUIRED)
find_package(Threads REQUIRED)
find_package(Clang 21.1 CONFIG REQUIRED)
target_include_directories(${PROJECT_NAME} PRIVATE ${CLANG_INCLUDE_DIRS})
target_include_directories(${PROJECT_NAME} SYSTEM PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/external)
target_link_libraries(${PROJECT_NAME} PRIVATE libclang ${Boost_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT})
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
