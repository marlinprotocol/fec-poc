cmake_minimum_required(VERSION 3.9)
project(fec VERSION 0.0.1 LANGUAGES CXX)

add_compile_options(-msse4.1 -D__restrict=)
set(CMAKE_CXX_FLAGS_DEBUG "-g -DDEBUG")

find_package(Boost COMPONENTS program_options REQUIRED)

add_subdirectory(3rdparty/wirehair)
    target_include_directories(wirehair-shared INTERFACE 3rdparty/wirehair)

file(GLOB siamese_sources 3rdparty/siamese/*.cpp)
set(wirehair_sources 3rdparty/wirehair/WirehairCodec.cpp 3rdparty/wirehair/WirehairTools.cpp 3rdparty/wirehair/wirehair.cpp)

add_library(siamese STATIC ${siamese_sources})
    target_include_directories(siamese PUBLIC 3rdparty/siamese)

    # Both Wirehair and Siamese leak the GF256Ctx symbol. Until the developer
    # fixes it using __attribute__(visibility), we have to work around it.
    #target_compile_options(siamese PRIVATE -DGF256Ctx=GF256Ctx_Siamese_)

add_library(siamese-shared SHARED ${siamese_sources})
    target_include_directories(siamese-shared PUBLIC 3rdparty/siamese)
    #target_compile_options(siamese PRIVATE -DGF256Ctx=GF256Ctx_Siamese_)

add_executable(main src/main.cpp)
    set_property(TARGET main PROPERTY CXX_STANDARD 17)
    target_compile_options(main PRIVATE -Werror -Wall -Wextra -pedantic-errors)
    target_include_directories(main
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${Boost_INCLUDE_DIR}
    )
    #target_link_libraries(main wirehair-shared siamese-shared ${Boost_LIBRARIES} pthread)
    target_link_libraries(main siamese-shared wirehair-shared ${Boost_LIBRARIES} pthread)

add_executable(main_noshared src/main.cpp ${siamese_sources} ${wirehair_sources})
    set_property(TARGET main_noshared PROPERTY CXX_STANDARD 17)
   #target_compile_options(main_noshared PRIVATE -Werror -Wall -Wextra -pedantic-errors)
    target_include_directories(main_noshared
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/wirehair
            ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/siamese
            ${Boost_INCLUDE_DIR}
    )
    #target_link_libraries(main_noshared wirehair-shared siamese-shared ${Boost_LIBRARIES} pthread)
    target_link_libraries(main_noshared ${Boost_LIBRARIES} pthread)

add_executable(exp src/exp.cpp)
    set_property(TARGET exp PROPERTY CXX_STANDARD 17)
    target_compile_options(exp PRIVATE -Werror -Wall -Wextra -pedantic-errors)
    target_include_directories(exp
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${Boost_INCLUDE_DIR}
    )
    target_link_libraries(exp wirehair-shared siamese pthread)

add_executable(ctr_exp src/ctr_exp.cpp)
    set_property(TARGET ctr_exp PROPERTY CXX_STANDARD 17)
    target_compile_options(ctr_exp PRIVATE -Werror -Wall -Wextra -pedantic-errors)
    target_include_directories(ctr_exp
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${Boost_INCLUDE_DIR}
    )
    target_link_libraries(ctr_exp wirehair-shared siamese pthread)

add_executable(source_sink_exp src/source_sink_exp.cpp)
    set_property(TARGET source_sink_exp PROPERTY CXX_STANDARD 17)
    target_compile_options(source_sink_exp PRIVATE -Werror -Wall -Wextra -pedantic-errors)
    target_include_directories(source_sink_exp
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${Boost_INCLUDE_DIR}
    )
    target_link_libraries(source_sink_exp wirehair-shared siamese pthread)

#add_executable(siamese_wtf src/siamese_wtf.cpp)
#    set_property(TARGET siamese_wtf PROPERTY CXX_STANDARD 17)
#    target_compile_options(siamese_wtf PRIVATE -Werror -Wall -Wextra -pedantic-errors)
#    target_include_directories(siamese_wtf
#        PRIVATE
#            ${CMAKE_CURRENT_SOURCE_DIR}/include
#            ${Boost_INCLUDE_DIR}
#    )
#    target_link_libraries(siamese_wtf siamese-shared wirehair-shared ${Boost_LIBRARIES} pthread)
