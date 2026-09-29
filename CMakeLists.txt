cmake_minimum_required(VERSION 3.16)

# QtSoundModem — CMake build for the macOS port (also configures on
# Linux). Upstream's qmake/Visual Studio files are not carried here.

# Apple Silicon-only target for the macOS port. Set before project()
# so CMake's compiler probe runs against the right architecture and
# deployment target.
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
    if(NOT DEFINED CMAKE_OSX_ARCHITECTURES)
        set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "" FORCE)
    endif()
    if(NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
        set(CMAKE_OSX_DEPLOYMENT_TARGET "14.0" CACHE STRING "" FORCE)
    endif()
endif()

if(APPLE)
    set(QSM_LANGUAGES C CXX OBJCXX)
else()
    set(QSM_LANGUAGES C CXX)
endif()

project(QtSoundModem
    VERSION 0.0.0.76
    LANGUAGES ${QSM_LANGUAGES}
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

find_package(Qt6 6.5 REQUIRED COMPONENTS
    Core
    Gui
    Widgets
    Network
    SerialPort
    Multimedia
)

# fftw3f — single-precision FFT, required on every platform.
find_library(FFTW3F_LIBRARY
    NAMES fftw3f
    HINTS
        ENV FFTW3F_ROOT
        /opt/homebrew/lib
        /opt/homebrew/opt/fftw/lib
        /usr/local/lib
        /usr/local/opt/fftw/lib
)
find_path(FFTW3F_INCLUDE_DIR
    NAMES fftw3.h
    HINTS
        ENV FFTW3F_ROOT
        /opt/homebrew/include
        /opt/homebrew/opt/fftw/include
        /usr/local/include
        /usr/local/opt/fftw/include
)
if(NOT FFTW3F_LIBRARY OR NOT FFTW3F_INCLUDE_DIR)
    message(FATAL_ERROR
        "fftw3f not found. On macOS: brew install fftw. "
        "On Debian/Ubuntu: apt install libfftw3-dev.")
endif()

# Platform-agnostic sources — compiled on every supported platform.
set(QSM_COMMON_SOURCES
    main.cpp
    QtSoundModem.cpp
    QtAudio.cpp
    Config.cpp
    ShowFilter.cpp
    tcpCode.cpp
    6pack.cpp

    ax25.c
    ax25_agw.c
    ax25_demod.c
    ax25_fec.c
    ax25_l2.c
    ax25_mod.c
    berlekamp.c
    BusyDetect.c
    galois.c
    il2p.c
    kiss_mode.c
    Modulate.c
    ofdm.c
    pktARDOP.c
    rs.c
    rsid.c
    RSUnit.c
    SMMain.c
    sm_main.c
    SoundInput.c
    UZ7HOUtils.c
    ARDOPC.c
    ardopSampleArrays.c
    dw9600.c
)

set(QSM_HEADERS
    UZ7HOStuff.h
    QtSoundModem.h
    tcpCode.h
)

set(QSM_FORMS
    calibrateDialog.ui
    devicesDialog.ui
    filterWindow.ui
    ModemDialog.ui
    QtSoundModem.ui
)

set(QSM_RESOURCES
    QtSoundModem.qrc
)

# Platform-specific audio / OS-shim sources.
#
# Not built on macOS (kept for upstream rebases): ALSASound.c, pulse.c,
# audio.c, Linux.c (Linux only), Waveout.c, hid.c.
set(QSM_PLATFORM_SOURCES "")
if(APPLE)
    list(APPEND QSM_PLATFORM_SOURCES
        MacBits.c
        MacPermissions.mm
        MacAudioRate.mm
    )
endif()
if(UNIX AND NOT APPLE)
    list(APPEND QSM_PLATFORM_SOURCES
        ALSASound.c
        pulse.c
        audio.c
        Linux.c
    )
endif()

add_executable(QtSoundModem
    ${QSM_COMMON_SOURCES}
    ${QSM_PLATFORM_SOURCES}
    ${QSM_HEADERS}
    ${QSM_FORMS}
    ${QSM_RESOURCES}
)

# Port version, appended to upstream's VersionString in the title bar
# (0.0.0.76+mac-0.1.2).
# CI passes the tag; local builds use git describe, evaluated at
# configure time (re-run cmake to refresh). --match keeps non-release
# tags such as corpus-v1 out.
set(QTSM_MAC_VERSION "" CACHE STRING "Port version, e.g. mac-0.1.2 (default: git describe)")
set(QSM_MAC_VERSION "${QTSM_MAC_VERSION}")
if(NOT QSM_MAC_VERSION)
    execute_process(
        COMMAND git describe --tags --match "mac-*" --dirty
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        OUTPUT_VARIABLE QSM_MAC_VERSION
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
endif()
if(QSM_MAC_VERSION)
    target_compile_definitions(QtSoundModem PRIVATE "QSM_MAC_SUFFIX=\"+${QSM_MAC_VERSION}\"")
else()
    target_compile_definitions(QtSoundModem PRIVATE "QSM_MAC_SUFFIX=\"\"")
endif()
# CFBundle versions must be numeric: mac-0.1.1-3-gabc1234 -> 0.1.1.
string(REGEX MATCH "[0-9]+\\.[0-9]+\\.[0-9]+" QSM_BUNDLE_VERSION "${QSM_MAC_VERSION}")
if(NOT QSM_BUNDLE_VERSION)
    set(QSM_BUNDLE_VERSION "${PROJECT_VERSION}")
endif()

# macOS: make it a .app bundle.
if(APPLE)
    set(QSM_MACOS_ICON "${CMAKE_SOURCE_DIR}/macos/QtSoundModem.icns")

    set_target_properties(QtSoundModem PROPERTIES
        MACOSX_BUNDLE                  TRUE
        MACOSX_BUNDLE_INFO_PLIST       "${CMAKE_SOURCE_DIR}/macos/Info.plist.in"
        MACOSX_BUNDLE_GUI_IDENTIFIER   "com.gm5dna.qtsoundmodem"
        MACOSX_BUNDLE_BUNDLE_NAME      "QtSoundModem"
        MACOSX_BUNDLE_BUNDLE_VERSION   "${QSM_BUNDLE_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${QSM_BUNDLE_VERSION}"
        MACOSX_BUNDLE_COPYRIGHT        "Copyright (C) 2019-2024 UZ7HO, G8BPQ contributors"
        MACOSX_BUNDLE_ICON_FILE        "QtSoundModem.icns"
    )

    # Copy the .icns into Contents/Resources/ as a POST_BUILD step
    # before macdeployqt runs (CMake runs POST_BUILD commands in
    # declaration order, and macdeployqt.cmake is included further down).
    # Using MACOSX_PACKAGE_LOCATION via set_source_files_properties
    # copies AFTER POST_BUILD, which leaves the icon outside the
    # final ad-hoc seal and breaks `codesign --verify`.
    add_custom_command(TARGET QtSoundModem POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory
                "$<TARGET_BUNDLE_CONTENT_DIR:QtSoundModem>/Resources"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${QSM_MACOS_ICON}"
                "$<TARGET_BUNDLE_CONTENT_DIR:QtSoundModem>/Resources/QtSoundModem.icns"
        COMMENT "Copying QtSoundModem.icns into bundle Resources"
        VERBATIM
    )
endif()

target_link_libraries(QtSoundModem PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    Qt6::SerialPort
    Qt6::Multimedia
    "${FFTW3F_LIBRARY}"
)

target_include_directories(QtSoundModem PRIVATE
    "${CMAKE_SOURCE_DIR}"
    "${FFTW3F_INCLUDE_DIR}"
)

if(UNIX AND NOT APPLE)
    # ALSA + dlopen for runtime-loaded libpulse (see pulse.c).
    find_library(ASOUND_LIBRARY NAMES asound)
    if(NOT ASOUND_LIBRARY)
        message(FATAL_ERROR "libasound not found (apt install libasound2-dev).")
    endif()
    target_link_libraries(QtSoundModem PRIVATE
        "${ASOUND_LIBRARY}"
        ${CMAKE_DL_LIBS}
    )
endif()

if(APPLE)
    # libhidapi (Homebrew) provides the hid_open_path / hid_write /
    # hid_close / hid_enumerate symbols SMMain.c calls for CM108
    # PTT. Headers live under /opt/homebrew/include/hidapi/.
    find_library(HIDAPI_LIBRARY NAMES hidapi
        HINTS
            /opt/homebrew/lib
            /opt/homebrew/opt/hidapi/lib
            /usr/local/lib
            /usr/local/opt/hidapi/lib)
    find_path(HIDAPI_INCLUDE_DIR NAMES hidapi.h
        PATH_SUFFIXES hidapi
        HINTS
            /opt/homebrew/include
            /opt/homebrew/opt/hidapi/include
            /usr/local/include
            /usr/local/opt/hidapi/include)
    if(NOT HIDAPI_LIBRARY OR NOT HIDAPI_INCLUDE_DIR)
        message(FATAL_ERROR "hidapi not found. brew install hidapi.")
    endif()

    target_include_directories(QtSoundModem PRIVATE
        "${HIDAPI_INCLUDE_DIR}")

    # AVFoundation is needed by MacPermissions.mm (microphone-permission
    # probe). CoreFoundation is pulled in transitively but list it for
    # documentation. CoreAudio + Foundation are needed by MacAudioRate.mm
    # for kAudioDevicePropertyNominalSampleRate and dispatch_*. AppKit /
    # IOKit / Metal already arrive via Qt.
    find_library(AVFOUNDATION_FRAMEWORK AVFoundation REQUIRED)
    find_library(COREFOUNDATION_FRAMEWORK CoreFoundation REQUIRED)
    find_library(COREAUDIO_FRAMEWORK   CoreAudio    REQUIRED)
    find_library(FOUNDATION_FRAMEWORK  Foundation   REQUIRED)

    target_link_libraries(QtSoundModem PRIVATE
        "${HIDAPI_LIBRARY}"
        "${AVFOUNDATION_FRAMEWORK}"
        "${COREFOUNDATION_FRAMEWORK}"
        "${COREAUDIO_FRAMEWORK}"
        "${FOUNDATION_FRAMEWORK}")

    # After the icon copy above (POST_BUILD runs in declaration order)
    # and after HIDAPI_LIBRARY is found.
    include("${CMAKE_SOURCE_DIR}/cmake/macdeployqt.cmake")
endif()

# Diagnostics. Off by default; CI turns QSM_WARNINGS on in a non-fatal
# job. The upstream C is noisy at -Wall, so never add -Werror here.
option(QSM_WARNINGS "Build with -Wall -Wextra" OFF)
option(QSM_TSAN "Build with ThreadSanitizer" OFF)
# Port-owned sources must stay warning-free; CI builds with this on.
# (-Wno-inconsistent-missing-override: upstream's QtSoundModem.h.)
# Off by default so a new compiler/Qt deprecation can't break user builds.
option(QSM_STRICT "Treat warnings as errors in port-owned sources" OFF)
if(QSM_STRICT)
    set(QSM_PORT_SOURCES QtAudio.cpp main.cpp)
    if(APPLE)
        list(APPEND QSM_PORT_SOURCES MacBits.c MacPermissions.mm MacAudioRate.mm)
    endif()
    set_source_files_properties(${QSM_PORT_SOURCES} PROPERTIES
        COMPILE_OPTIONS "-Wall;-Wextra;-Werror;-Wno-inconsistent-missing-override")
endif()
if(QSM_WARNINGS)
    target_compile_options(QtSoundModem PRIVATE -Wall -Wextra)
endif()
if(QSM_TSAN)
    target_compile_options(QtSoundModem PRIVATE -fsanitize=thread -g)
    target_link_options(QtSoundModem PRIVATE -fsanitize=thread)
endif()
