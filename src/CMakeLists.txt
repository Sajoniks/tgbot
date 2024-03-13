target_sources(${PROJECT_NAME}
    PRIVATE
        configuration/configuration.cpp
        tgapi/bot.cpp
        tgapi/rest_client.cpp
        tgapi/command_module.cpp
        log/log.cpp
        log/logmanager.cpp
        parse/api_types.cpp
        sqlite/sqlite.cpp
        main.cpp
)

get_property(build_os GLOBAL PROPERTY OS)
if (${build_os} STREQUAL "linux")
    target_sources(${PROJECT_NAME}
        PRIVATE
            util/linux_util.cpp
    )
endif ()