idf_component_register(
    SRCS
        "main.c"
    INCLUDE_DIRS
        "."
    EMBED_FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/web/index.html"
        "${CMAKE_CURRENT_SOURCE_DIR}/web/css/base.css"
        "${CMAKE_CURRENT_SOURCE_DIR}/web/css/signals.css"
        "${CMAKE_CURRENT_SOURCE_DIR}/web/css/tabs.css"
        "${CMAKE_CURRENT_SOURCE_DIR}/web/js/app.js"
        "${CMAKE_CURRENT_SOURCE_DIR}/web/js/api.js"
        "${CMAKE_CURRENT_SOURCE_DIR}/web/js/signals.js"
        "${CMAKE_CURRENT_SOURCE_DIR}/web/js/settings.js"
        "${CMAKE_CURRENT_SOURCE_DIR}/web/js/api-docs.js"
        "${CMAKE_CURRENT_SOURCE_DIR}/web/tabs/monitor.html"
        "${CMAKE_CURRENT_SOURCE_DIR}/web/tabs/signals.html"
        "${CMAKE_CURRENT_SOURCE_DIR}/web/tabs/manual.html"
        "${CMAKE_CURRENT_SOURCE_DIR}/web/tabs/settings.html"
        "${CMAKE_CURRENT_SOURCE_DIR}/web/tabs/api.html"
)

# Ensure the files exist
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/web/css/base.css")
    message(FATAL_ERROR "base.css is missing!")
endif()

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/web/css/signals.css")
    message(FATAL_ERROR "signals.css is missing!")
endif()

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/web/css/tabs.css")
    message(FATAL_ERROR "tabs.css is missing!")
endif()

# Debugging commented out
