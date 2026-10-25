add_library(dbc_language STATIC
    dbpro_project.cpp
    dialect_detector.cpp
    frontend.cpp
    frontend_binder.cpp
    frontend_lexer.cpp
    frontend_parser.cpp
    language_catalogue.cpp
    language_profile.cpp
)

target_include_directories(dbc_language
    PUBLIC
        ${PROJECT_SOURCE_DIR}/src
)

dbc_enable_warnings(dbc_language)
