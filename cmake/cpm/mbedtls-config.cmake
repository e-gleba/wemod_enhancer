# PSA-only SHA-256 via TF-PSA-Crypto (Mbed TLS 4.x split crypto out of Mbed TLS).
# Link MbedTLS::tfpsacrypto and call psa_hash_compute() — the legacy
# mbedtls_sha256.h API is gone in 4.x.
# Ref: https://github.com/Mbed-TLS/mbedtls/blob/development/docs/4.0-migration-guide.md
# Ref: https://github.com/Mbed-TLS/TF-PSA-Crypto/blob/development/docs/1.0-migration-guide.md
#
# 4.x tags no longer ship generated sources (error.c,
# ssl_debug_helpers_generated.c, psa_crypto_driver_wrappers_no_static.c),
# so GEN_FILES must stay ON. That needs Python 3 with Jinja2 + jsonschema
# and Perl on the build host.
# Ref: https://github.com/momo5502/sogen/pull/1475
#
# Shallow git clones break the 4.x build (missing tf-psa-crypto submodule
# and version files), so keep full history here.
# Ref: https://docs.pjsip.org/en/latest/specific-guides/security/mbedtls.html

# Both headers live in the binary dir so the source tree stays clean.
file(
  WRITE "${CMAKE_CURRENT_BINARY_DIR}/tf-psa-crypto-config.h"
  [[
#ifndef WEMOD_TF_PSA_CRYPTO_CONFIG_H
#define WEMOD_TF_PSA_CRYPTO_CONFIG_H

#define PSA_WANT_ALG_SHA_256 1

#endif
]])
file(
  WRITE "${CMAKE_CURRENT_BINARY_DIR}/mbedtls-config.h"
  [[
#ifndef WEMOD_MBEDTLS_CONFIG_H
#define WEMOD_MBEDTLS_CONFIG_H

#endif
]])

set(TF_PSA_CRYPTO_CONFIG_FILE
    "${CMAKE_CURRENT_BINARY_DIR}/tf-psa-crypto-config.h"
    CACHE FILEPATH "TF-PSA-Crypto feature selection: SHA-256 only" FORCE)
set(MBEDTLS_CONFIG_FILE
    "${CMAKE_CURRENT_BINARY_DIR}/mbedtls-config.h"
    CACHE FILEPATH "Mbed TLS feature selection: TLS/X.509 defaults" FORCE)

cpmaddpackage(
  NAME
  mbedtls
  GITHUB_REPOSITORY
  Mbed-TLS/mbedtls
  VERSION
  4.2.0
  GIT_TAG
  v4.2.0
  GIT_SHALLOW
  FALSE
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
  "GEN_FILES ON")
