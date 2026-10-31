# Mbed-TLS configured SHA-256-only via MBEDTLS_CONFIG_FILE: the feature
# selection lives in mbedtls-sha256-config.h next to this wrapper.
# Exposes MbedTLS::mbedcrypto — link that target for mbedtls_sha256().
# Ref: https://mbed-tls.readthedocs.io/en/latest/kb/how-to/using-loose-modules-without-the-full-library/

set(MBEDTLS_CONFIG_FILE
    "${CMAKE_CURRENT_LIST_DIR}/mbedtls-sha256-config.h"
    CACHE FILEPATH "Mbed-TLS feature selection: SHA-256 only" FORCE)

cpmaddpackage(
    NAME
    mbedtls
    GITHUB_REPOSITORY
    Mbed-TLS/mbedtls
    VERSION
    3.6.5
    GIT_TAG
    v3.6.5
    GIT_SHALLOW
    TRUE
    GIT_PROGRESS
    TRUE
    EXCLUDE_FROM_ALL
    YES
    SYSTEM
    YES
    OPTIONS
    "ENABLE_PROGRAMS OFF"
    "ENABLE_TESTING OFF"
    "MBEDTLS_AS_SUBPROJECT ON"
    "MBEDTLS_FATAL_WARNINGS OFF"
    "USE_STATIC_MBEDTLS_LIBRARY ON"
    "USE_SHARED_MBEDTLS_LIBRARY OFF"
    "LINK_WITH_PTHREAD OFF"
    "LINK_WITH_TRUSTED_STORAGE OFF"
    "GEN_FILES OFF")
