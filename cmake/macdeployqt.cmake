# Bundle Qt frameworks into QtSoundModem.app via macdeployqt.
#
# Without this step the .app links against ${brew_prefix}/lib paths
# and stops working the moment the user upgrades Qt or moves the
# bundle off this machine.

if(NOT APPLE)
    return()
endif()

# Qt6Core_DIR points at <prefix>/lib/cmake/Qt6Core. Walk up to <prefix>/bin.
get_filename_component(QSM_QT_BIN_DIR "${Qt6Core_DIR}/../../../bin" ABSOLUTE)

find_program(MACDEPLOYQT_EXECUTABLE macdeployqt
    HINTS
        "${QSM_QT_BIN_DIR}"
        /opt/homebrew/opt/qt/bin
        /opt/homebrew/bin
        /usr/local/opt/qt/bin
        /usr/local/bin
)

if(NOT MACDEPLOYQT_EXECUTABLE)
    message(WARNING
        "macdeployqt not found — the resulting QtSoundModem.app will "
        "depend on the Homebrew Qt install rather than being self-contained. "
        "Install Qt via Homebrew and re-run cmake.")
    return()
endif()

add_custom_command(TARGET QtSoundModem POST_BUILD
    COMMAND "${MACDEPLOYQT_EXECUTABLE}"
            "$<TARGET_BUNDLE_DIR:QtSoundModem>"
            -always-overwrite
    COMMENT "macdeployqt: bundling Qt frameworks into QtSoundModem.app"
    VERBATIM
)

# macdeployqt only handles Qt frameworks. The build also depends on
# Homebrew's fftw3f, which would otherwise leave the bundle linked to
# /opt/homebrew/opt/fftw/... and break the moment the .app is moved off
# this machine (or Homebrew is upgraded). Embed the real dylib into
# Contents/Frameworks/ and rewrite the executable's load command to
# point at @rpath. macdeployqt has already set the rpath to
# @executable_path/../Frameworks for Qt, which we reuse.
get_filename_component(FFTW3F_REAL "${FFTW3F_LIBRARY}" REALPATH)
get_filename_component(FFTW3F_NAME "${FFTW3F_REAL}" NAME)

add_custom_command(TARGET QtSoundModem POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory
            "$<TARGET_BUNDLE_CONTENT_DIR:QtSoundModem>/Frameworks"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${FFTW3F_REAL}"
            "$<TARGET_BUNDLE_CONTENT_DIR:QtSoundModem>/Frameworks/${FFTW3F_NAME}"
    COMMAND chmod u+w
            "$<TARGET_BUNDLE_CONTENT_DIR:QtSoundModem>/Frameworks/${FFTW3F_NAME}"
    COMMAND install_name_tool -id
            "@rpath/${FFTW3F_NAME}"
            "$<TARGET_BUNDLE_CONTENT_DIR:QtSoundModem>/Frameworks/${FFTW3F_NAME}"
    COMMAND /bin/bash -c
            "exe='$<TARGET_FILE:QtSoundModem>'; \
             for dep in $(otool -L \"$exe\" | awk 'NR>1 {print $1}' | grep '/libfftw3f'); do \
                 install_name_tool -change \"$dep\" '@rpath/${FFTW3F_NAME}' \"$exe\"; \
             done"
    COMMENT "Embedding ${FFTW3F_NAME} into QtSoundModem.app/Contents/Frameworks"
    VERBATIM
)

# Same treatment for libhidapi (commit 6 added the dependency for
# CM108 PTT). Without this the bundle dyld-fails the moment it lands
# on a Mac without /opt/homebrew/opt/hidapi/. macdeployqt.cmake is
# included before the APPLE link block in CMakeLists.txt populates
# HIDAPI_LIBRARY, so we re-find here to avoid first-configure misses.
find_library(HIDAPI_LIBRARY NAMES hidapi
    HINTS
        /opt/homebrew/lib
        /opt/homebrew/opt/hidapi/lib
        /usr/local/lib
        /usr/local/opt/hidapi/lib)
if(HIDAPI_LIBRARY)
    get_filename_component(HIDAPI_REAL "${HIDAPI_LIBRARY}" REALPATH)
    get_filename_component(HIDAPI_NAME "${HIDAPI_REAL}" NAME)

    add_custom_command(TARGET QtSoundModem POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${HIDAPI_REAL}"
                "$<TARGET_BUNDLE_CONTENT_DIR:QtSoundModem>/Frameworks/${HIDAPI_NAME}"
        COMMAND chmod u+w
                "$<TARGET_BUNDLE_CONTENT_DIR:QtSoundModem>/Frameworks/${HIDAPI_NAME}"
        COMMAND install_name_tool -id
                "@rpath/${HIDAPI_NAME}"
                "$<TARGET_BUNDLE_CONTENT_DIR:QtSoundModem>/Frameworks/${HIDAPI_NAME}"
        COMMAND /bin/bash -c
                "exe='$<TARGET_FILE:QtSoundModem>'; \
                 for dep in $(otool -L \"$exe\" | awk 'NR>1 {print $1}' | grep -E '/libhidapi'); do \
                     install_name_tool -change \"$dep\" '@rpath/${HIDAPI_NAME}' \"$exe\"; \
                 done"
        COMMENT "Embedding ${HIDAPI_NAME} into QtSoundModem.app/Contents/Frameworks"
        VERBATIM
    )
endif()
