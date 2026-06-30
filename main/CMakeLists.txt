# Collect the pre-gzipped web assets produced by utils/build_web.py.
# Run `python utils/build_web.py` before building so main/www/ is populated.
file(GLOB_RECURSE WWW_ASSETS RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" "www/*.gz")

if(NOT WWW_ASSETS)
    message(WARNING
        "No embedded web assets found in main/www/. "
        "Run 'python utils/build_web.py' to generate them before building.")
endif()

idf_component_register(
    SRCS
        "main.c"
        "config_store.c"
        "wifi_manager.c"
        "http_server.c"
    INCLUDE_DIRS
        "."
    EMBED_FILES
        ${WWW_ASSETS}
)
