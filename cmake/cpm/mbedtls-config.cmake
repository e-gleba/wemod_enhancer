# Mbed-TLS SHA-256-only via MBEDTLS_CONFIG_FILE pointing at a generated
# header below. Exposes MbedTLS::mbedcrypto — link it for mbedtls_sha256().
# Ref: https://mbed-tls.readthedocs.io/en/latest/kb/how-to/using-loose-modules-without-the-full-library/
# NOTE: no mbedtls/check_config.h include here. Mbed TLS 3.x pulls it in
# automatically at the right point; including it manually trips -W#warnings.
# Ref: https://github.com/Mbed-TLS/mbedtls/blob/v3.6.5/include/mbedtls/build_info.h

file(
  WRITE "${CMAKE_CURRENT_BINARY_DIR}/mbedtls-sha256-config.h"
  [[
#ifndef WEMOD_MBEDTLS_SHA256_CONFIG_H
#define WEMOD_MBEDTLS_SHA256_CONFIG_H

#define MBEDTLS_PLATFORM_C
#define MBEDTLS_PLATFORM_MEMORY
#define MBEDTLS_SHA256_C

#endif
]])

set(MBEDTLS_CONFIG_FILE
    "${CMAKE_CURRENT_BINARY_DIR}/mbedtls-sha256-config.h"
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
