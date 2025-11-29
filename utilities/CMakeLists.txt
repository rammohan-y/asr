add_library(app_utilities
    Logger.cpp
    GlobalLogger.cpp
    Uuid.cpp
)
target_include_directories(app_utilities PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

