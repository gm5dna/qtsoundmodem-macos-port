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
    # Pre-clean: macdeployqt's deposits (Contents/Frameworks/,
    # Contents/PlugIns/) and any prior _CodeSignature/ are not tracked
    # by cmake, so `make clean` / `cmake --build --clean-first` leaves
    # them behind. macdeployqt's internal verify then trips on a stale
    # signature whose resources no longer match.
    COMMAND "${CMAKE_COMMAND}" -E rm -rf
            "$<TARGET_BUNDLE_CONTENT_DIR:QtSoundModem>/Frameworks"
            "$<TARGET_BUNDLE_CONTENT_DIR:QtSoundModem>/PlugIns"
            "$<TARGET_BUNDLE_CONTENT_DIR:QtSoundModem>/_CodeSignature"
    # Do not pass -codesign here. On macOS 26.x macdeployqt can leave a
    # partial Contents/_CodeSignature/CodeResources behind when its
    # internal signing fails on FinderInfo/fileprovider metadata. We do
    # all signing ourselves after every install_name_tool edit is done.
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
    # Do not sign here: install_name_tool changes are not complete until
    # every embedded dependency and executable load command has been
    # rewritten. The final post-build step signs everything in dependency
    # order with xattr cleanup before each codesign invocation.
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
        # Signing is centralized in the final post-build step after all
        # install_name_tool edits have completed.
        COMMAND /bin/bash -c
                "exe='$<TARGET_FILE:QtSoundModem>'; \
                 for dep in $(otool -L \"$exe\" | awk 'NR>1 {print $1}' | grep -E '/libhidapi'); do \
                     install_name_tool -change \"$dep\" '@rpath/${HIDAPI_NAME}' \"$exe\"; \
                 done"
        COMMENT "Embedding ${HIDAPI_NAME} into QtSoundModem.app/Contents/Frameworks"
        VERBATIM
    )
endif()

# Final ad-hoc signing pass. install_name_tool invalidates existing
# signatures and dyld will refuse modified binaries with stale ones, so
# this MUST be the last POST_BUILD step.
#
# Strategy:
#   1. Scrub the bundle once via ditto --norsrc --noextattr --noacl to
#      remove any com.apple.FinderInfo / fileprovider metadata that
#      build/copy steps have left on package roots. Codesign rejects
#      FinderInfo ("detritus not allowed"); com.apple.provenance is
#      kernel-protected and codesign tolerates it.
#   2. Sign nested code in dependency order: dylibs and frameworks in
#      Frameworks/, then everything in PlugIns/.
#   3. Codesign the .app bundle as a whole, in TMPDIR. Building under
#      ~/Documents puts the .app inside macOS's file provider, which
#      re-stamps com.apple.FinderInfo and com.apple.fileprovider.fpfs#P
#      on package roots (.app, .framework) faster than `xattr -c` can
#      clear them. Bundle-level codesign then dies with "detritus not
#      allowed". TMPDIR (/var/folders/...) is outside file-provider
#      control. We ditto the bundle there, sign, then ditto back. The
#      signature is sealed inside Contents/_CodeSignature/CodeResources,
#      so any FinderInfo stamped onto the in-place .app afterwards does
#      not invalidate it. Without this final step the .app validates as
#      "code has no resources but signature indicates they must be
#      present".
add_custom_command(TARGET QtSoundModem POST_BUILD
    COMMAND /bin/bash -c
            "set -euo pipefail; \
             bundle='$<TARGET_BUNDLE_DIR:QtSoundModem>'; \
             exe='$<TARGET_FILE:QtSoundModem>'; \
             clean_xattrs() { \
                 if [ -d \"$1\" ]; then \
                     find \"$1\" -exec xattr -c {} \\; 2>/dev/null || true; \
                 else \
                     xattr -c \"$1\" 2>/dev/null || true; \
                 fi; \
             }; \
             scrub_package_roots() { \
                 tmp_parent=$(mktemp -d \"$bundle.resign.XXXXXX\"); \
                 tmp_bundle=\"$tmp_parent/$(basename \"$bundle\")\"; \
                 exe_backup=\"$tmp_parent/$(basename \"$exe\").backup\"; \
                 if [ -f \"$exe\" ]; then \
                     ditto --norsrc --noextattr --noacl \"$exe\" \"$exe_backup\"; \
                 fi; \
                 ditto --norsrc --noextattr --noacl \"$bundle\" \"$tmp_bundle\"; \
                 rm -rf \"$bundle\"; \
                 mv \"$tmp_bundle\" \"$bundle\"; \
                 if [ -f \"$exe_backup\" ]; then \
                     mkdir -p \"$(dirname \"$exe\")\"; \
                     ditto --norsrc --noextattr --noacl \"$exe_backup\" \"$exe\"; \
                     chmod u+x \"$exe\"; \
                     rm -f \"$exe_backup\"; \
                 fi; \
                 rmdir \"$tmp_parent\"; \
             }; \
             sign_code() { \
                 clean_xattrs \"$1\"; \
                 codesign --force --sign - --timestamp=none \"$1\"; \
             }; \
             scrub_package_roots; \
             rm -rf \"$bundle/Contents/_CodeSignature\"; \
             for f in \"$bundle\"/Contents/Frameworks/*.dylib; do \
                 [ -e \"$f\" ] || continue; \
                 sign_code \"$f\"; \
             done; \
             for f in \"$bundle\"/Contents/Frameworks/*.framework; do \
                 [ -e \"$f\" ] || continue; \
                 sign_code \"$f\"; \
             done; \
             if [ -d \"$bundle/Contents/PlugIns\" ]; then \
                 find \"$bundle/Contents/PlugIns\" -name '*.dylib' -print0 | \
                     while IFS= read -r -d '' f; do sign_code \"$f\"; done; \
             fi; \
             sign_dir=$(mktemp -d -t qtsm-bundle-sign); \
             sign_path=\"$sign_dir/$(basename \"$bundle\")\"; \
             ditto --norsrc --noextattr --noacl \"$bundle\" \"$sign_path\"; \
             codesign --force --sign - --timestamp=none \"$sign_path\"; \
             rm -rf \"$bundle\"; \
             ditto --norsrc --noextattr --noacl \"$sign_path\" \"$bundle\"; \
             rm -rf \"$sign_dir\"; \
             codesign --verify --verbose=2 \"$bundle\""
    COMMENT "Signing QtSoundModem.app ad-hoc after macdeployqt and install_name_tool"
    VERBATIM
)
