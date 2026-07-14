find_package(Crow CONFIG REQUIRED)
find_package(httplib CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(ixwebsocket CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

set(STREAMSOFT_YTDLP_PATH "${CMAKE_CURRENT_SOURCE_DIR}/tools/yt-dlp.exe" CACHE INTERNAL "")
if(WIN32 AND NOT EXISTS "${STREAMSOFT_YTDLP_PATH}")
    message(STATUS "Скачиваю yt-dlp.exe (резолвер прямых ссылок для музыки, ~18 МБ)...")
    file(DOWNLOAD
        "https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp.exe"
        "${STREAMSOFT_YTDLP_PATH}"
        SHOW_PROGRESS
        STATUS _ytdlp_dl_status)
    list(GET _ytdlp_dl_status 0 _ytdlp_dl_code)
    if(NOT _ytdlp_dl_code EQUAL 0)
        message(WARNING "Не удалось скачать yt-dlp.exe — резервное включение YouTube-треков будет недоступно, пока файл не появится в ${STREAMSOFT_YTDLP_PATH}")
        file(REMOVE "${STREAMSOFT_YTDLP_PATH}")
    endif()
endif()

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

target_compile_definitions(streamsoft_core PRIVATE
    STREAMSOFT_WEB_DIR="${CMAKE_CURRENT_SOURCE_DIR}/web"
    STREAMSOFT_CACERT_PATH="${CMAKE_CURRENT_SOURCE_DIR}/certs/cacert.pem"
    STREAMSOFT_TTS_ADAPTER_DIR="${CMAKE_SOURCE_DIR}/adapters/tts"
    STREAMSOFT_RVC_ADAPTER_DIR="${CMAKE_SOURCE_DIR}/adapters/rvc"
    STREAMSOFT_YTDLP_PATH="${STREAMSOFT_YTDLP_PATH}"
    _WIN32_WINNT=0x0A00
    ASIO_STANDALONE
    CPPHTTPLIB_OPENSSL_SUPPORT
)
