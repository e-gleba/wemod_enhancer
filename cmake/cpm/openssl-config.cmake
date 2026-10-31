# OpenSSL via the jimmy-park/openssl-cmake wrapper: OpenSSL itself has no
# native CMake build, so this package builds it and exposes the standard
# FindOpenSSL targets (OpenSSL::Crypto, OpenSSL::SSL) via OVERRIDE_FIND_PACKAGE.
# Configure ships no-tests and a static-only lib build; keep everything else
# at the wrapper defaults.

cpmaddpackage(
    NAME
    OpenSSL
    GITHUB_REPOSITORY
    jimmy-park/openssl-cmake
    VERSION
    3.5.4
    GIT_TAG
    3.5.4
    GIT_SHALLOW
    TRUE
    GIT_PROGRESS
    TRUE
    EXCLUDE_FROM_ALL
    YES
    SYSTEM
    YES
    OPTIONS
    "OPENSSL_TARGET_VERSION 3.5.4"
    "OPENSSL_CONFIGURE_OPTIONS no-ssl3\\;no-comp\\;no-idea\\;no-tests"
    "OPENSSL_BUILD_VERBOSE OFF"
    "OPENSSL_CONFIGURE_VERBOSE OFF"
    "OPENSSL_INSTALL OFF"
    "OPENSSL_INSTALL_CERT OFF"
    "OPENSSL_TEST OFF"
    "OPENSSL_USE_CCACHE ON")
