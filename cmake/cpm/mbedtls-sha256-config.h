// Minimal Mbed-TLS feature selection: SHA-256 only.
// Passed to the library via MBEDTLS_CONFIG_FILE (see mbedtls-config.cmake).
// Ref: https://mbed-tls.readthedocs.io/en/latest/kb/how-to/using-loose-modules-without-the-full-library/

#ifndef WEMOD_MBEDTLS_SHA256_CONFIG_H
#define WEMOD_MBEDTLS_SHA256_CONFIG_H

// --- platform layer: required for portable builds ---------------------------
#define MBEDTLS_PLATFORM_C
#define MBEDTLS_PLATFORM_MEMORY

// --- SHA-256 only ------------------------------------------------------------
#define MBEDTLS_SHA256_C

// --- consistency self-check (2.x config layout, errors on bad combos) -------
#include "mbedtls/check_config.h"

#endif
