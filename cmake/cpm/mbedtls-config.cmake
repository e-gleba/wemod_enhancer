# SHA-256 via TF-PSA-Crypto inside Mbed TLS 4.x. patcher.cpp calls the PSA
# hash API (psa_hash_compute/setup/update/finish); the legacy
# mbedtls_sha256.h API is gone in 4.x.
# Ref: https://github.com/Mbed-TLS/mbedtls/blob/development/docs/4.0-migration-guide.md
# Ref: https://github.com/Mbed-TLS/TF-PSA-Crypto/blob/development/docs/1.0-migration-guide.md
#
# The official release tarball (mbedtls-4.2.0.tar.bz2) already contains the
# tf-psa-crypto submodule contents and the generated sources (error.c,
# ssl_debug_helpers_generated.c, psa_crypto_driver_wrappers_no_static.c),
# so GEN_FILES stays OFF and no Python/Jinja2/Perl is needed on the host.
# GitHub auto-generated source zips lack submodules and generated files and
# cannot be configured, so do not switch this back to GIT_TAG.
# Ref: https://github.com/Mbed-TLS/mbedtls/releases (4.2.0 note)
# Ref: https://pigweed.googlesource.com/third_party/github/ARMmbed/mbedtls/
#
# No custom TF_PSA_CRYPTO_CONFIG_FILE / MBEDTLS_CONFIG_FILE: replacing the
# default crypto config with a SHA-256-only header zeroes
# MBEDTLS_PK_MAX_PUBKEY_RAW_LEN (it only grows when RSA/ECC key types are
# enabled), and MSVC rejects the resulting zero-sized pub_raw[] with C2229.
# Instead the additive user config below only ensures PSA_WANT_ALG_SHA_256
# on top of upstream defaults.
# Ref: https://github.com/Mbed-TLS/TF-PSA-Crypto/blob/tf-psa-crypto-1.2.0/include/mbedtls/pk.h
# Ref: https://mcuxpresso.nxp.com/mcuxsdk/latest/middleware/mbedtls3x/docs/psa-transition.html

# Header lives in the binary dir so the source tree stays clean.
file(
  WRITE "${CMAKE_CURRENT_BINARY_DIR}/wemod-tf-psa-crypto-user-config.h"
  [[
#ifndef WEMOD_TF_PSA_CRYPTO_USER_CONFIG_H
#define WEMOD_TF_PSA_CRYPTO_USER_CONFIG_H

#if !defined(PSA_WANT_ALG_SHA_256)
#define PSA_WANT_ALG_SHA_256 1
#endif

#endif
]])

set(MBEDTLS_PSA_CRYPTO_USER_CONFIG_FILE
    "${CMAKE_CURRENT_BINARY_DIR}/wemod-tf-psa-crypto-user-config.h"
    CACHE FILEPATH "Additive TF-PSA-Crypto config: ensure SHA-256" FORCE)

cpmaddpackage(
  NAME
  mbedtls
  URL
  "https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-4.2.0/mbedtls-4.2.0.tar.bz2"
  URL_HASH
  "SHA256=2bed9d713b4668f76553b097e72b8aa30bc8f112a940d7ae228d524bbde6ffea"
  VERSION
  4.2.0
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

# 4.x subproject target names differ by level: TF-PSA-Crypto exposes plain
# `tfpsacrypto`, the Mbed TLS package wrapper exposes `MbedTLS::tfpsacrypto`.
# Accept either so the patcher links whatever actually exists.
# Ref: https://github.com/Mbed-TLS/TF-PSA-Crypto (add_subdirectory usage)
if(NOT TARGET wemod_tfpsacrypto)
  if(TARGET MbedTLS::tfpsacrypto)
    add_library(wemod_tfpsacrypto ALIAS MbedTLS::tfpsacrypto)
  elseif(TARGET tfpsacrypto)
    add_library(wemod_tfpsacrypto ALIAS tfpsacrypto)
  else()
    message(FATAL_ERROR "Mbed TLS 4.2.0 provided neither MbedTLS::tfpsacrypto nor tfpsacrypto")
  endif()
endif()
