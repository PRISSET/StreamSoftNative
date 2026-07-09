find_package(Crow CONFIG REQUIRED)
find_package(httplib CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(ixwebsocket CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

add_executable(streamsoft_core
    src/main.cpp
)

target_include_directories(streamsoft_core PRIVATE include)
target_link_libraries(streamsoft_core PRIVATE
    Crow::Crow
    httplib::httplib
    OpenSSL::SSL
    OpenSSL::Crypto
    ixwebsocket::ixwebsocket
    nlohmann_json::nlohmann_json
    winmm
    dxgi
)

# Dev-time only: points straight at the source tree's web/ folder (and the
# TTS/RVC adapter folders) so we can iterate without a copy/install step. The
# packaged installer will ship these next to the exe and these defines go
# away in favor of a relative lookup.
target_compile_definitions(streamsoft_core PRIVATE
    STREAMSOFT_WEB_DIR="${CMAKE_CURRENT_SOURCE_DIR}/web"
    STREAMSOFT_CACERT_PATH="${CMAKE_CURRENT_SOURCE_DIR}/certs/cacert.pem"
    STREAMSOFT_TTS_ADAPTER_DIR="${CMAKE_SOURCE_DIR}/adapters/tts"
    STREAMSOFT_RVC_ADAPTER_DIR="${CMAKE_SOURCE_DIR}/adapters/rvc"
    _WIN32_WINNT=0x0A00
    ASIO_STANDALONE
    CPPHTTPLIB_OPENSSL_SUPPORT
)
