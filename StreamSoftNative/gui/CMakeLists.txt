set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Quick QuickControls2 QuickDialogs2 Network ShaderTools LabsPlatform)

# Same third-party deps as core/ — the GUI now embeds core's whole
# background-service bundle (see core_app.hpp) and runs it on its own
# thread, so this is one exe instead of two separate processes.
find_package(Crow CONFIG REQUIRED)
find_package(httplib CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(ixwebsocket CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

qt_policy(SET QTP0001 NEW)
qt_policy(SET QTP0004 NEW)

qt_add_executable(streamsoft_gui WIN32
    main.cpp
    api_client.cpp
    api_client.hpp
    overlay_ws_client.cpp
    overlay_ws_client.hpp
    $<$<BOOL:${WIN32}>:app_icon.rc>
)

# `pragma Singleton` inside Theme.qml only states intent — the build's qmldir
# generation needs this property too, or it silently registers Theme as a
# plain (non-singleton) type and every `Theme.xxx` property access in QML
# resolves to undefined with no runtime error at all.
set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE true)

qt_add_qml_module(streamsoft_gui
    URI StreamSoftGui
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/Theme.qml
        qml/components/GlassCard.qml
        qml/components/GlassSurface.qml
        qml/components/ModuleInstallCard.qml
        qml/components/SectionHeader.qml
        qml/components/PillButton.qml
        qml/components/NavButton.qml
        qml/components/GlassTextField.qml
        qml/components/GlassComboBox.qml
        qml/components/GlassSlider.qml
        qml/components/GlassToggle.qml
        qml/components/LinkChip.qml
        qml/components/MediaSlot.qml
        qml/components/Star.qml
        qml/pages/OverlayPage.qml
        qml/pages/ConnectionsPage.qml
        qml/pages/TtsPage.qml
        qml/pages/AlertsPage.qml
        qml/pages/CommandsPage.qml
        qml/pages/MutedPage.qml
        qml/pages/RvcPage.qml
        qml/pages/SettingsPage.qml
        qml/pages/UpdatesPage.qml
    RESOURCES
        qml/assets/bg-chess-queen.png
        qml/assets/bg-holo.png
        qml/assets/fonts/Inter.ttf
        qml/assets/icons/app-icon.png
)

qt_add_shaders(streamsoft_gui "streamsoft_gui_shaders"
    PREFIX "/qt/qml/StreamSoftGui/shaders"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/shaders"
    FILES
        shaders/glass.vert
        shaders/glass.frag
)

target_include_directories(streamsoft_gui PRIVATE . "${CMAKE_SOURCE_DIR}/core/include")
target_link_libraries(streamsoft_gui PRIVATE
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::QuickDialogs2
    Qt6::LabsPlatform
    Qt6::Network
    Crow::Crow
    httplib::httplib
    OpenSSL::SSL
    OpenSSL::Crypto
    ixwebsocket::ixwebsocket
    nlohmann_json::nlohmann_json
    winmm
    dxgi
)

if(WIN32)
    target_link_libraries(streamsoft_gui PRIVATE dwmapi)
endif()

target_compile_definitions(streamsoft_gui PRIVATE
    _WIN32_WINNT=0x0A00
    ASIO_STANDALONE
    CPPHTTPLIB_OPENSSL_SUPPORT
    STREAMSOFT_WEB_DIR="${CMAKE_SOURCE_DIR}/core/web"
    STREAMSOFT_CACERT_PATH="${CMAKE_SOURCE_DIR}/core/certs/cacert.pem"
    STREAMSOFT_TTS_ADAPTER_DIR="${CMAKE_SOURCE_DIR}/adapters/tts"
    STREAMSOFT_RVC_ADAPTER_DIR="${CMAKE_SOURCE_DIR}/adapters/rvc"
    # Kept in sync by hand with installer/streamsoft.iss's MyAppVersion —
    # auto_update.hpp compares this against GitHub Releases' latest tag.
    STREAMSOFT_VERSION="1.0.1"
    # Qt's signals/slots/emit macros collide with unrelated identifiers in
    # third-party headers now linked into this target — Crow has a method
    # literally named signals(), which Qt's macro mangles into `public()`
    # and breaks the parse. Neither api_client nor main.cpp use the bare
    # Qt keyword form (they'd need Q_SIGNALS/Q_SLOTS/Q_EMIT if they did).
    QT_NO_KEYWORDS
)

# Ship Qt's own DLLs next to the exe (windeployqt), same idea as vcpkg's
# applocal deployment for the core's OpenSSL/brotli DLLs.
if(WIN32)
    get_target_property(_qmake_exe Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(_qt_bin_dir "${_qmake_exe}" DIRECTORY)
    find_program(WINDEPLOYQT_EXE windeployqt HINTS "${_qt_bin_dir}")
    if(WINDEPLOYQT_EXE)
        add_custom_command(TARGET streamsoft_gui POST_BUILD
            COMMAND "${WINDEPLOYQT_EXE}" --no-translations --qmldir "${CMAKE_CURRENT_SOURCE_DIR}/qml" $<TARGET_FILE:streamsoft_gui>
            COMMENT "Running windeployqt"
        )
    endif()
endif()
