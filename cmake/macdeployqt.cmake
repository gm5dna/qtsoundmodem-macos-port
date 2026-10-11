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

# macdeployqt only handles Qt frameworks and is not guaranteed to pull in
# Homebrew's fftw3f and hidapi, which would leave the bundle linked to
# /opt/homebrew/... Embed the real dylibs into Contents/Frameworks/ and
# point the executable's load commands at @executable_path/../Frameworks
# (as macdeployqt does for Qt), so the bundle always loads its own copies.
# The copy is named after the dylib's install name (otool -D), the name
# macdeployqt uses when it does deploy it, so the two never coexist.
# Signing happens in the final step, after every install_name_tool edit.
foreach(_qsm_lib IN ITEMS FFTW3F HIDAPI)
    get_filename_component(_qsm_real "${${_qsm_lib}_LIBRARY}" REALPATH)
    execute_process(COMMAND otool -D "${_qsm_real}"
        OUTPUT_VARIABLE _qsm_id OUTPUT_STRIP_TRAILING_WHITESPACE)
    string(REGEX REPLACE ".*\n" "" _qsm_id "${_qsm_id}")
    get_filename_component(_qsm_name "${_qsm_id}" NAME)
    string(REGEX REPLACE "\\..*" "" _qsm_stem "${_qsm_name}")
    set(_qsm_dest "$<TARGET_BUNDLE_CONTENT_DIR:QtSoundModem>/Frameworks/${_qsm_name}")
    add_custom_command(TARGET QtSoundModem POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory
                "$<TARGET_BUNDLE_CONTENT_DIR:QtSoundModem>/Frameworks"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${_qsm_real}" "${_qsm_dest}"
        COMMAND chmod u+w "${_qsm_dest}"
        COMMAND install_name_tool -id "@executable_path/../Frameworks/${_qsm_name}" "${_qsm_dest}"
        COMMAND /bin/bash -c
                "exe='$<TARGET_FILE:QtSoundModem>'; \
                 for dep in $(otool -L \"$exe\" | awk 'NR>1 {print $1}' | grep '/${_qsm_stem}'); do \
                     install_name_tool -change \"$dep\" '@executable_path/../Frameworks/${_qsm_name}' \"$exe\"; \
                 done"
        COMMENT "Embedding ${_qsm_name} into QtSoundModem.app/Contents/Frameworks"
        VERBATIM
    )
endforeach()
unset(_qsm_lib)
unset(_qsm_real)
unset(_qsm_id)
unset(_qsm_name)
unset(_qsm_stem)
unset(_qsm_dest)

# Final ad-hoc signing pass. install_name_tool invalidates signatures,
# so this MUST be the last POST_BUILD step.
#
# All signing happens on a copy in TMPDIR. Under ~/Documents the file
# provider re-stamps com.apple.FinderInfo on package roots (.app,
# .framework) faster than it can be cleared, and codesign rejects it
# ("detritus not allowed"). ditto --noextattr strips xattrs on the way
# out and back; the seal lives in _CodeSignature, so FinderInfo stamped
# on the in-place bundle afterwards does not invalidate it. On failure
# the TMPDIR copy is kept, as it may be the only signed bundle left.
add_custom_command(TARGET QtSoundModem POST_BUILD
    COMMAND /bin/bash -c
            "set -euo pipefail; \
             bundle='$<TARGET_BUNDLE_DIR:QtSoundModem>'; \
             sign_dir=$(mktemp -d -t qtsm-bundle-sign); \
             app=\"$sign_dir/$(basename \"$bundle\")\"; \
             ditto --norsrc --noextattr --noacl \"$bundle\" \"$app\"; \
             sign() { codesign --force --sign - --timestamp=none \"$@\"; }; \
             for f in \"$app\"/Contents/Frameworks/*.dylib \"$app\"/Contents/Frameworks/*.framework; do \
                 [ -e \"$f\" ] || continue; sign \"$f\"; \
             done; \
             if [ -d \"$app/Contents/PlugIns\" ]; then \
                 find \"$app/Contents/PlugIns\" -name '*.dylib' -exec codesign --force --sign - --timestamp=none {} +; \
             fi; \
             sign \"$app\"; \
             rm -rf \"$bundle\"; \
             ditto --norsrc --noextattr --noacl \"$app\" \"$bundle\"; \
             codesign --verify --verbose=2 \"$bundle\"; \
             rm -rf \"$sign_dir\""
    COMMENT "Signing QtSoundModem.app ad-hoc after macdeployqt and install_name_tool"
    VERBATIM
)
