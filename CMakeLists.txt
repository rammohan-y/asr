cmake_minimum_required(VERSION 3.10)
project(VoskASRWebSocket)

set(CMAKE_CXX_STANDARD 17)

# Find required packages
find_package(Threads REQUIRED)

# Find nlohmann/json
# Try package manager first, then fallback to local installation
find_package(nlohmann_json QUIET)
if(NOT nlohmann_json_FOUND)
    message(STATUS "nlohmann/json not found in system, checking local installation...")
    set(NLOHMANN_JSON_DIR "${CMAKE_SOURCE_DIR}/lib/third_party/json")
    if(EXISTS "${NLOHMANN_JSON_DIR}/include/nlohmann/json.hpp")
        message(STATUS "Found nlohmann/json in local directory: ${NLOHMANN_JSON_DIR}")
        add_library(nlohmann_json INTERFACE)
        target_include_directories(nlohmann_json INTERFACE "${NLOHMANN_JSON_DIR}/include")
        add_library(nlohmann_json::nlohmann_json ALIAS nlohmann_json)
    elseif(EXISTS "${NLOHMANN_JSON_DIR}/single_include/nlohmann/json.hpp")
        message(STATUS "Found nlohmann/json (single header) in local directory: ${NLOHMANN_JSON_DIR}")
        add_library(nlohmann_json INTERFACE)
        target_include_directories(nlohmann_json INTERFACE "${NLOHMANN_JSON_DIR}/single_include")
        add_library(nlohmann_json::nlohmann_json ALIAS nlohmann_json)
    else
        message(FATAL_ERROR "nlohmann/json not found. Please install it via:\n  - Package manager: sudo apt-get install nlohmann-json3-dev\n  - Or run: ./install_third_party.sh")
    endif()
else
    message(STATUS "Found nlohmann/json via package manager")
endif()

# Find WebSocket++
# Try system paths first, then fallback to local installation
find_path(WEBSOCKETPP_INCLUDE_DIR 
    NAMES websocketpp/config/asio_no_tls.hpp
    PATHS 
        /usr/include 
        /usr/local/include
        "${CMAKE_SOURCE_DIR}/lib/third_party/websocketpp"
        "${CMAKE_SOURCE_DIR}/lib/third_party"
)

if(NOT WEBSOCKETPP_INCLUDE_DIR)
    message(FATAL_ERROR "WebSocket++ not found. Please install it via:\n  - Package manager: sudo apt-get install libwebsocketpp-dev\n  - Or run: ./install_third_party.sh")
else()
    message(STATUS "Found WebSocket++ at: ${WEBSOCKETPP_INCLUDE_DIR}")
endif()

# Find Boost (required by WebSocket++)
find_package(Boost REQUIRED COMPONENTS system)

# Vosk paths
set(VOSK_HOME "$ENV{HOME}/vosk")
set(VOSK_INCLUDE_DIR "${VOSK_HOME}/vosk")
set(VOSK_LIBRARY "${VOSK_HOME}/vosk/libvosk.so")

if(NOT EXISTS "${VOSK_LIBRARY}")
    message(FATAL_ERROR "Vosk library not found at ${VOSK_LIBRARY}. Run deploy_vosk_prebuilt.sh first")
endif()

include_directories(include)

# Add utilities subdirectory
add_subdirectory(utilities)
include_directories(utilities)
include_directories(${WEBSOCKETPP_INCLUDE_DIR})
include_directories(${VOSK_INCLUDE_DIR})

# Add your source files
file(GLOB SOURCES "src/*.cpp")

add_executable(vosk_asr_ws ${SOURCES})

# Link with Vosk and other libraries
target_link_libraries(vosk_asr_ws
    ${VOSK_LIBRARY}
    nlohmann_json::nlohmann_json  # JSON library
    Boost::system                  # Boost for WebSocket++
    pthread                        # POSIX threads
    dl                             # Dynamic linking
    m                              # Math library
    app_utilities
)

# Set rpath so it can find libvosk.so at runtime
set_target_properties(vosk_asr_ws PROPERTIES
    BUILD_RPATH "${VOSK_HOME}/vosk"
    INSTALL_RPATH "${VOSK_HOME}/vosk"
)

message(STATUS "Vosk ASR WebSocket Configuration:")
message(STATUS "  Vosk library: ${VOSK_LIBRARY}")
message(STATUS "  Vosk include: ${VOSK_INCLUDE_DIR}")
message(STATUS "  Model path: ${VOSK_HOME}/models/vosk-model-small-en-us-0.15")
