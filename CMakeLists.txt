cmake_minimum_required(VERSION 3.16)

# QtSoundModem — macOS-port CMake build.
#
# Buildability across the macos-port commit series (this is commit 5 of 6):
#   commit 2: cmake configure clean on macOS; compile/link fails by design.
#   commit 3: tighten LinuxBits.c __ARM_ARCH guard.
#   commit 4: add MacBits.c (POSIX + serial PTT shims); tighten SMMain.c
#             __ARM_ARCH guards so Pi GPIO symbols are not referenced.
#   commit 5 (this commit): migrate Qt Multimedia Qt5 → Qt6 in
#                           QtSoundModem.{cpp,h} + Config.cpp; add audio
#                           device-name storage to MacBits.c. Also fixes
#                           Qt 6 / macOS toolchain collisions that surface
#                           when the Qt5-API errors clear: UZ7HOStuff.h's
#                           Byte/Word/etc. macros become typedefs (Qt 6
#                           uses 'Byte' as a template parameter name);
#                           tcpCode.cpp / ax25.c htonl forward decls are
#                           skipped on __APPLE__ (macOS implements them
#                           as macros); il2p.c popcount fallbacks are
#                           skipped on clang/gcc; rsid.c includes
#                           <stdlib.h> instead of <malloc.h> on macOS.
#                           Every TU compiles. Link still fails — symbols
#                           routed through ALSASound.c / Linux.c (e.g.
#                           InitSound, SoundInit, SoundFlush, SendtoCard,
#                           getTicks, PollReceivedSamples, plus storage
#                           globals SoundMode / ReceiveSize / SendSize /
#                           onlyMixSnoop / txLatency / pttclk /
#                           CaptureIndex / PlayBackIndex / using48000 /
#                           useTimedPTT / CurrentLevel / CurrentLevelR /
#                           initPulse / printtick) are deferred to
#                           commit 6.
#   commit 6: add hid_mac.c, MacPermissions.mm, default SoundMode=5 on macOS;
#             provide MacBits.c stubs/storage for the symbols listed above;
#             at this point the macOS bundle compiles, links, and runs.
#
# The qmake .pro file is intentionally retained — upstream's Linux + Windows
# qmake build remains untouched. CMake also configures on Linux so this file
# isn't dead weight when rebasing on upstream master.

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
# Sources excluded from the macOS build (kept in tree so upstream
# rebases stay clean): ALSASound.c, pulse.c, audio.c, Linux.c,
# LinuxBits.c, Waveout.c, hid.c.
set(QSM_PLATFORM_SOURCES "")
if(APPLE)
    list(APPEND QSM_PLATFORM_SOURCES
        MacBits.c
        MacPermissions.mm
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

# Bundle target on macOS, plain executable elsewhere.
if(APPLE)
    set(QSM_MACOS_ICON "${CMAKE_SOURCE_DIR}/macos/QtSoundModem.icns")

    add_executable(QtSoundModem MACOSX_BUNDLE
        ${QSM_COMMON_SOURCES}
        ${QSM_PLATFORM_SOURCES}
        ${QSM_HEADERS}
        ${QSM_FORMS}
        ${QSM_RESOURCES}
    )

    set_target_properties(QtSoundModem PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST       "${CMAKE_SOURCE_DIR}/macos/Info.plist.in"
        MACOSX_BUNDLE_GUI_IDENTIFIER   "com.gm5dna.qtsoundmodem"
        MACOSX_BUNDLE_BUNDLE_NAME      "QtSoundModem"
        MACOSX_BUNDLE_BUNDLE_VERSION   "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_COPYRIGHT        "Copyright (C) 2019-2024 UZ7HO, G8BPQ contributors"
        MACOSX_BUNDLE_ICON_FILE        "QtSoundModem.icns"
    )

    # Copy the .icns into Contents/Resources/ as a POST_BUILD step
    # before macdeployqt runs (CMake runs POST_BUILD commands in
    # declaration order, and macdeployqt.cmake is included below).
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

    include("${CMAKE_SOURCE_DIR}/cmake/macdeployqt.cmake")
else()
    add_executable(QtSoundModem
        ${QSM_COMMON_SOURCES}
        ${QSM_PLATFORM_SOURCES}
        ${QSM_HEADERS}
        ${QSM_FORMS}
        ${QSM_RESOURCES}
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
    # documentation. AppKit/IOKit/Metal already arrive via Qt.
    find_library(AVFOUNDATION_FRAMEWORK AVFoundation REQUIRED)
    find_library(COREMEDIA_FRAMEWORK   CoreMedia    REQUIRED)
    find_library(COREFOUNDATION_FRAMEWORK CoreFoundation REQUIRED)

    target_link_libraries(QtSoundModem PRIVATE
        "${HIDAPI_LIBRARY}"
        "${AVFOUNDATION_FRAMEWORK}"
        "${COREMEDIA_FRAMEWORK}"
        "${COREFOUNDATION_FRAMEWORK}")
endif()
