cmake_minimum_required(VERSION 3.20)

project(tgbot
        VERSION 1.0
        LANGUAGES CXX
)

message(STATUS "Running cmake")

set(Boost_COMPONENTS system url)

find_package(OpenSSL REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(SQLite3 REQUIRED)
find_package(Boost REQUIRED COMPONENTS ${Boost_COMPONENTS})
find_package(RapidJSON CONFIG REQUIRED)

add_executable(${PROJECT_NAME})

if (UNIX AND NOT APPLE)
    set_property(GLOBAL PROPERTY OS "linux")
    target_compile_definitions(${PROJECT_NAME}
        PRIVATE
            OS_LINUX=1
            OS_WINDOWS=0
    )
endif()

if (WIN32)
    set_property(GLOBAL PROPERTY OS "win32")
    target_compile_definitions(${PROJECT_NAME}
            PRIVATE
                OS_LINUX=0
                OS_WINDOWS=1
    )
endif()

set_target_properties(${PROJECT_NAME}
        PROPERTIES
        CXX_STANDARD 17
)
target_link_libraries(${PROJECT_NAME}
        PRIVATE
        ${Boost_LIBRARIES}
        SQLite::SQLite3
        rapidjson
        OpenSSL::SSL
        fmt::fmt-header-only
)

add_subdirectory(include)
add_subdirectory(src)
