cmake_minimum_required(VERSION 3.9)
project(fec VERSION 0.0.1 LANGUAGES CXX)

add_compile_options(-msse4.1)

find_package(Boost COMPONENTS program_options REQUIRED)

file(GLOB siamese_sources 3rdparty/siamese/*.cpp)
set(wirehair_sources 3rdparty/wirehair/WirehairCodec.cpp 3rdparty/wirehair/WirehairTools.cpp 3rdparty/wirehair/wirehair.cpp)

add_library(wirehair-and-siamese STATIC ${wirehair_sources} ${siamese_sources})
    target_compile_options(wirehair-and-siamese PRIVATE -D__restrict=)
    target_compile_options(wirehair-and-siamese PRIVATE $<$<CONFIG:DEBUG>:-g -DDEBUG -O0 -march=native>)
    target_compile_options(wirehair-and-siamese PRIVATE $<$<CONFIG:RELEASE>:-O3 -march=native>)
    target_include_directories(wirehair-and-siamese PUBLIC 3rdparty/wirehair 3rdparty/siamese)

add_executable(main src/main.cpp)
    set_property(TARGET main PROPERTY CXX_STANDARD 17)
    target_compile_options(main PRIVATE -Werror -Wall -Wextra -pedantic-errors)
    target_include_directories(main
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${Boost_INCLUDE_DIR}
    )
    target_link_libraries(main wirehair-and-siamese ${Boost_LIBRARIES} pthread)
