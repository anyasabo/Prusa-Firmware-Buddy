#!/usr/bin/env python3
"""Write lib/vex.json (OpenVEX 0.2.0) from the triage table below.

The table is the reviewed record of which scanner findings apply to Buddy and
why. Grype reads the output with `grype sbom:buddy.cdx.json --vex lib/vex.json`.
Products are the purls from lib/sbom.yml; an unknown component fails.

Keep statements specific: name the config option, file or call site that
decides it. CI fails when lib/vex.json is not regenerated after a change here.

Needs pyyaml.
"""
from __future__ import annotations

import json
import sys
import uuid

from sbom_manifest import MANIFEST, ROOT, load_components

OUTPUT = ROOT / "lib" / "vex.json"
# Date of the last change to TRIAGE; becomes the document timestamp.
REVIEWED = "2026-11-04"
NAMESPACE = uuid.UUID("0b1d6f53-5a1e-4c43-9c1a-7665782d6275")

# OpenVEX justifications for not_affected.
NP = "vulnerable_code_not_present"
NIEP = "vulnerable_code_not_in_execute_path"
NCA = "vulnerable_code_cannot_be_controlled_by_adversary"
MIT = "inline_mitigations_already_exist"

Decision = tuple[list[str], dict[str, str]]


def not_affected(cves: list[str], justification: str, impact: str) -> Decision:
    return cves, {
        "status": "not_affected",
        "justification": justification,
        "impact_statement": impact,
    }


def affected(cves: list[str], action: str, notes: str) -> Decision:
    return cves, {
        "status": "affected",
        "action_statement": action,
        "status_notes": notes,
    }


def investigating(cves: list[str], notes: str) -> Decision:
    return cves, {"status": "under_investigation", "status_notes": notes}


MBEDTLS_CFG = "include/mbedtls/cipher_config_ece.h"
NVD_RANGE = "NVD version range is wrong for this release"
ESP32_MAP = "the ESP32 esp-nic link map"
# ICMPv6 (CVE-2020-22283), mDNS (CVE-2026-112062), SNMP (CVE-2026-119053, -8836).
LWIP_CVES = [
    "CVE-2020-22283", "CVE-2026-112062", "CVE-2026-119053", "CVE-2026-8836"
]

# Manifest key -> decisions.
TRIAGE: dict[str, list[Decision]] = {
    "lib/Middlewares/Third_Party/mbedtls": [
        not_affected(
            ["CVE-2022-35409", "CVE-2022-46393"], NP,
            f"DTLS only; DTLS and the SSL server are disabled ({MBEDTLS_CFG})."
        ),
        not_affected(
            ["CVE-2023-43615"], NP,
            "Only NULL/RC4 suites are affected; the only suite enabled is ECDHE-ECDSA-AES128-GCM-SHA256."
        ),
        not_affected(
            ["CVE-2024-23775", "CVE-2025-47917", "CVE-2025-48965"], NP,
            "X.509 writing/string-to-names code; MBEDTLS_X509_CRT_WRITE_C and MBEDTLS_X509_CREATE_C are off."
        ),
        not_affected(["CVE-2024-28960", "CVE-2024-45157", "CVE-2026-25835"],
                     NP, "PSA Crypto; MBEDTLS_PSA_CRYPTO_C is off."),
        not_affected(
            ["CVE-2025-52496"], NP,
            "AESNI (x86) support; MBEDTLS_AESNI_C is off and the target is Cortex-M."
        ),
        not_affected(["CVE-2025-52497"], NP,
                     "PEM parsing; MBEDTLS_PEM_PARSE_C is off."),
        not_affected(
            ["CVE-2025-66442"], NP,
            "Only occurs with LLVM select-optimize; Buddy builds with GCC."),
        not_affected(
            ["CVE-2026-34871"], NP,
            "Linux /dev/urandom fallback; MBEDTLS_NO_PLATFORM_ENTROPY with a hardware entropy source."
        ),
        not_affected(["CVE-2026-34872"], NP, "FFDH; MBEDTLS_DHM_C is off."),
        not_affected(
            ["CVE-2026-34877"], NP,
            "Context serialization; MBEDTLS_SSL_CONTEXT_SERIALIZATION is off."
        ),
        not_affected(
            ["CVE-2025-59438"], NP,
            "Padding error timing in the cipher layer; no cipher padding mode is enabled and E2EE calls mbedtls_aes_crypt_cbc directly."
        ),
        not_affected(
            ["CVE-2025-54764"], NCA,
            "Microarchitectural (SSBleed/M-Step) attack on RSA keygen needs a co-located privileged attacker; not applicable to a Cortex-M MCU."
        ),
        not_affected(
            ["CVE-2025-27809"], MIT,
            "src/connect/tls/tls.cpp calls mbedtls_ssl_set_hostname()."),
        not_affected(["CVE-2021-36647"], NP,
                     f"{NVD_RANGE}: fixed in 2.27.0/2.16.11, before 2.28.0."),
        not_affected(
            ["CVE-2021-43666"], NP,
            f"{NVD_RANGE}: the pkcs12 empty-password fix is in 2.28.0 (Fixes #5136)."
        ),
        not_affected(
            ["CVE-2021-45451"], NP,
            f"{NVD_RANGE}: psa_aead_generate_nonce does not exist in 2.28."),
        not_affected(
            ["CVE-2023-52353"], NP,
            "3.x TLS version negotiation; not present in the 2.28 branch (no advisory, not in its ChangeLog)."
        ),
        affected(
            ["CVE-2025-27810"],
            "Upgrade mbedTLS (2.28 is end of life; fixed in 2.28.10) or move to 3.6 LTS.",
            "Finished-message check can be skipped after an allocation or hardware failure; Buddy allocates with calloc_fallible on a small heap."
        ),
        affected(
            ["CVE-2022-46392", "CVE-2024-23170"],
            "Upgrade mbedTLS; affects only presets with HAS_E2EE_SUPPORT() (RSA enabled).",
            "RSA private-key side channels; RSA-OAEP decrypt is used by E2EE (src/common/e2ee/e2ee.cpp). Requires precise timing or memory-access observation."
        ),
        # July 2026 advisories. Their CVE IDs are still RESERVED at CVE.org, so no CPE
        # feed has them; utils/sbom_feeds.py mbedtls-advisories finds them.
        not_affected(["CVE-2026-50584"], NP,
                     "ChaCha20; MBEDTLS_CHACHA20_C is off."),
        not_affected(
            ["CVE-2026-50581"], NP,
            "Extended master secret; MBEDTLS_SSL_EXTENDED_MASTER_SECRET is off."
        ),
        not_affected(
            ["CVE-2026-35336"], NIEP,
            "mbedtls_ecdh_calc_secret with an undersized buffer; TLS always passes a large enough buffer and Buddy has no direct calls."
        ),
        not_affected(
            ["CVE-2026-50587"], NIEP,
            "RSA PKCS#1 v1.5 decryption; E2EE uses RSA-OAEP (PKCS_V21)."),
        not_affected(
            ["CVE-2026-50585", "CVE-2026-50586"], NP,
            "DTLS session-reset state and TLS 1.2 server NewSessionTicket; no DTLS, no SSL server, no session tickets."
        ),
        not_affected(["CVE-2026-50580"], NP,
                     "ECDHE-PSK client; no PSK suites are enabled."),
        not_affected(
            ["CVE-2026-49300"], NCA,
            "CA-bit parsing differential needs user-supplied trust roots validated elsewhere; Buddy embeds one fixed CA."
        ),
        not_affected(
            ["CVE-2026-54435"], NCA,
            "ECC fast-reduction side channel needs a local privileged trace (enclave model); not an attacker the MCU has."
        ),
        affected(
            ["CVE-2026-54441"],
            "Upgrade mbedTLS (no 2.28 fix exists; 2.28 is end of life).",
            "Signature-algorithm restrictions are not enforced on the certificate chain; low impact with a single pinned Prusa CA."
        ),
    ],
    "lib/Middlewares/Third_Party/FatFs": [
        not_affected(["CVE-2026-6683", "CVE-2026-6687"], NP,
                     "exFAT code; FF_FS_EXFAT 0 (include/buddy/ffconf.h)."),
        not_affected(["CVE-2026-6684"], NP,
                     "GPT scanning with FF_LBA64; FF_LBA64 0."),
        affected(
            ["CVE-2026-6682"],
            "Backport the mount_volume() fasize overflow check or update FatFs.",
            "FAT32 mount_volume() fasize overflow; reachable with a crafted USB drive."
        ),
        investigating([
            "CVE-2026-6686"
        ], "f_lseek past EOF exposes old cluster data; check whether Buddy ever extends files with f_lseek."
                      ),
        affected(
            ["CVE-2026-6688"],
            "Size readdir's name buffer NAME_MAX + 1 (fork branch readdir-off-by-one, c1c5cf831).",
            "Buddy's caller has the pattern: dirnext_r (src/buddy/filesystem_fatfs.cpp) writes filename[len + 1] into libsysbase's char[NAME_MAX] buffer; a crafted FAT volume (254-byte UTF-8 LFN, NUL inside the 8.3 entry) gives a 1-byte stack write. Reproduced under ASan."
        ),
    ],
    "lib/Middlewares/Third_Party/FreeRTOS": [
        not_affected(
            ["CVE-2026-77235", "CVE-2026-77236"], NP,
            "TrustZone secure-context code; Buddy uses the ARM_CM33_NTZ (non-TrustZone) port."
        ),
        not_affected(
            ["CVE-2026-77234"], NP,
            "Timer-command handling; configUSE_TIMERS is 0 in every Buddy FreeRTOSConfig (default). The only MPU build, src/puppy/xbuddy_extension, runs unprivileged tasks but no timers."
        ),
        not_affected(
            ["CVE-2026-77237", "CVE-2026-119086"], NP,
            "Queue sets; configUSE_QUEUE_SETS is 0 (default) in every Buddy FreeRTOSConfig."
        ),
        not_affected(["CVE-2026-109587", "CVE-2026-109588"], NP,
                     "Arm Cortex-R82 MPU port, V11.3.0-V11.3.1 only."),
    ],
    "lib/Middlewares/Third_Party/LwIP": [
        not_affected(["CVE-2020-22283"], NP,
                     "ICMPv6; LWIP_IPV6 is off (include/buddy/lwipopts.h)."),
        not_affected(["CVE-2020-22284"], NP,
                     "6LoWPAN/ZEP netif; not compiled."),
    ],
    "lib/WUI/mdns": [
        not_affected(
            ["CVE-2020-22283"], NP,
            "ICMPv6 is not part of the mDNS responder; LWIP_IPV6 is off."),
        not_affected(
            ["CVE-2026-112062"], NIEP,
            "The vulnerable memcpy in mdns_handle_response() (lib/WUI/mdns/mdns.c:1864, fixed upstream in ab2f69c7) runs only for answers that match a pending search request. Requests are created only by mdns_search_service(), which the firmware never calls (lib/WUI/wui.cpp uses only mdns_resp_init, mdns_resp_add_netif, mdns_resp_add_service_prusalink and mdns_resp_remove_netif)."
        ),
    ],
    "lib/WUI/sntp": [
        not_affected(["CVE-2020-22283"], NP, "ICMPv6; LWIP_IPV6 is off."),
        not_affected(["CVE-2020-22284"], NP,
                     "6LoWPAN/ZEP netif; not part of the SNTP client."),
    ],
    "lib/Marlin": [
        not_affected(
            ["CVE-2018-1000537"], NP,
            "Marlin's sd/cardreader.cpp is not compiled (lib/AddMarlin.cmake); Buddy uses FatFs and its own media code."
        ),
    ],
    "lib/Middlewares/ST/STM32_USB_Host_Library": [
        affected(
            ["CVE-2021-42553"],
            "Add `ep_ix < USBH_MAX_NUM_ENDPOINTS` to the endpoint loop in USBH_ParseCfgDesc (as ST v3.5.1/v3.5.3), or update to v3.5.5.",
            "Endpoint-count overflow of Itf_Desc[].Ep_Desc[2] into USBH_HandleTypeDef (usbh_ctlreq.c:453); triggered by plugging in a crafted USB device."
        ),
    ],
    "bootstrap:gcc-arm-none-eabi/gcc-runtime": [
        not_affected(
            ["CVE-2023-4039"], NP,
            "AArch64 -fstack-protector issue; Buddy targets 32-bit Cortex-M (Thumb)."
        ),
    ],
    "bootstrap:esp8266-rtos-sdk/components/freertos": [
        not_affected([
            "CVE-2024-28115", "CVE-2026-77234", "CVE-2026-77237",
            "CVE-2026-119086"
        ], NP, "MPU-port issues; the ESP8266 port has no MPU support."),
        not_affected(
            ["CVE-2021-31571", "CVE-2021-31572", "CVE-2021-32020"], NCA,
            "Kernel size overflows reachable only through sizes chosen by the firmware itself (esp-nic), not by network input."
        ),
    ],
    "bootstrap:esp8266-rtos-sdk/components/mbedtls/mbedtls": [
        not_affected(
            [
                "CVE-2020-16150", "CVE-2020-36423", "CVE-2020-36425",
                "CVE-2020-36426", "CVE-2020-36476", "CVE-2020-36477",
                "CVE-2020-36478", "CVE-2021-44732", "CVE-2022-35409",
                "CVE-2022-46393", "CVE-2023-43615", "CVE-2023-52353",
                "CVE-2024-23775", "CVE-2025-27809", "CVE-2025-27810",
                "CVE-2025-47917", "CVE-2025-48965", "CVE-2025-52497"
            ], NIEP,
            "TLS/DTLS/X.509/PEM code. In the Wi-Fi module image mbedTLS is only called from wpa_supplicant's crypto_mbedtls-bignum.c and -ec.c; esp-nic uses no TLS. (Confirm with the ESP link map.)"
        ),
        not_affected(
            [
                "CVE-2020-36421", "CVE-2020-36424", "CVE-2022-46392",
                "CVE-2024-23170", "CVE-2021-24119", "CVE-2025-54764"
            ], NIEP,
            "RSA/static-DH/PEM side channels; the supplicant does not use RSA or PEM (SAE uses ECC/FFC bignum)."
        ),
        not_affected(
            ["CVE-2020-10932", "CVE-2020-36422", "CVE-2020-36475"], NCA,
            "ECC/bignum side channels needing local, cache- or enclave-grade observation of the module; not available to a radio-range attacker."
        ),
        not_affected(["CVE-2025-52496"], NP,
                     "AESNI (x86) support; the ESP8266 is Xtensa."),
        not_affected(
            ["CVE-2025-66442"], NP,
            "LLVM select-optimize only; the ESP8266 SDK builds with GCC."),
        not_affected(["CVE-2026-34871"], NP,
                     "Linux /dev/urandom fallback; not Linux."),
        not_affected(["CVE-2026-34872"], NP,
                     "Reported for 3.5.x and later; not this 2.16 fork."),
        not_affected(
            ["CVE-2021-36647", "CVE-2021-43666", "CVE-2021-45451"], NP,
            f"{NVD_RANGE} (fixed earlier, or the API does not exist in 2.16)."
        ),
        not_affected(
            ["CVE-2025-59438"], NIEP,
            "Cipher-layer padding timing; the supplicant uses its own AES/CCMP code."
        ),
        not_affected(
            [
                "CVE-2026-49300", "CVE-2026-50580", "CVE-2026-50581",
                "CVE-2026-50585", "CVE-2026-50586", "CVE-2026-54441"
            ], NP,
            "TLS/X.509 code; the ESP8266 link map (lib/esp-nic build, 2026-11-04) pulls no object from libmbedtls.a or libmbedx509.a, only libmbedcrypto.a."
        ),
        not_affected(["CVE-2026-50584"], NP,
                     "ChaCha20; the ESP8266 link map has no chacha20 object."),
        not_affected(
            ["CVE-2026-35336"], NP,
            "mbedtls_ecdh_calc_secret; the ESP8266 link map keeps no ecdh code."
        ),
        not_affected(
            ["CVE-2026-54435"], NCA,
            "ECC fast-reduction side channel needs local, privileged trace access to the module."
        ),
    ],
    "bootstrap:esp-idf": [
        not_affected(
            [
                "CVE-2025-66409", "CVE-2025-68473", "CVE-2025-68474",
                "CVE-2025-55297", "CVE-2025-64342", "CVE-2026-81507",
                "CVE-2026-46532"
            ], NP,
            "Bluetooth/BluFi; the bt component is not fetched (bootstrap.py `missing`)."
        ),
        not_affected(["CVE-2026-45542"], NP,
                     "protocomm is not fetched (bootstrap.py `missing`)."),
        not_affected(["CVE-2026-55687"], NP,
                     "JPEG driver for ESP32-P4; not part of an ESP32 build."),
        not_affected(["CVE-2026-45328", "CVE-2026-45329"], NP,
                     "ESP-TEE exists from 5.5; not in 5.0.3."),
        not_affected(["CVE-2026-45541", "CVE-2024-53845"], NIEP,
                     "esp_http_server and ESPTouch are not used by esp-nic."),
        not_affected(
            ["CVE-2026-45160"], NIEP,
            "DHCP server option parser; esp-nic runs WIFI_MODE_STA only (uart_nic_esp32.cpp:147), so dhcps never starts."
        ),
        not_affected(
            ["CVE-2024-28183"], NP,
            "Secure-boot anti-rollback TOCTOU; secure boot is not enabled in esp32.sdkconfig."
        ),
        not_affected(
            [
                "CVE-2026-113781", "CVE-2026-81508", "CVE-2026-25507",
                "CVE-2026-25508", "CVE-2022-24893"
            ], NP,
            "Bluetooth (BLE Mesh, A2DP, BLE provisioning); the bt component is not fetched."
        ),
        not_affected(["CVE-2026-113782"], NP,
                     "Thread border router; openthread is not fetched."),
        not_affected(["CVE-2026-113780"], NP,
                     "ESP-TEE exists from 5.5; not in 5.0.3."),
        not_affected(["CVE-2025-65092"], NP,
                     "ESP32-P4 JPEG decoder; not part of an ESP32 build."),
        not_affected(["CVE-2026-25532"], NIEP,
                     "WPS enrollee; esp-nic never enables WPS."),
        not_affected(["CVE-2025-52471"], NIEP,
                     "ESP-NOW; esp-nic does not use it."),
        not_affected(
            ["CVE-2026-113783"], NP,
            "lwIP DNS cache in ESP-IDF; the ESP32 esp-nic link map pulls no object from liblwip.a."
        ),
    ],
    # Decided from the ESP32 esp-nic link map (lib/esp-nic/build.py esp32, 2026-11-04):
    # only libmbedcrypto.a and libwpa_supplicant.a call into mbedTLS; libmbedtls.a,
    # libmbedx509.a and liblwip.a contribute no objects.
    "bootstrap:esp-idf/components/mbedtls/mbedtls": [
        not_affected([
            "CVE-2023-43615", "CVE-2023-45199", "CVE-2023-52353",
            "CVE-2024-30166", "CVE-2024-45159", "CVE-2025-27809",
            "CVE-2025-27810", "CVE-2026-25834", "CVE-2026-34877",
            "CVE-2026-50580", "CVE-2026-50581", "CVE-2026-50585",
            "CVE-2026-50586", "CVE-2026-50588", "CVE-2026-50640",
            "CVE-2026-73064"
        ], NP, f"TLS code; {ESP32_MAP} pulls no object from libmbedtls.a."),
        not_affected(
            [
                "CVE-2024-23775", "CVE-2025-47917", "CVE-2026-49300",
                "CVE-2026-50579", "CVE-2026-54441"
            ], NP,
            f"X.509/PKCS7 code; {ESP32_MAP} pulls no object from libmbedx509.a."
        ),
        not_affected(["CVE-2025-48965"], NP,
                     f"mbedtls_asn1_store_named_data is not in {ESP32_MAP}."),
        not_affected(["CVE-2025-49600", "CVE-2025-49601"], NP,
                     f"LMS; no lms object in {ESP32_MAP}."),
        not_affected(
            ["CVE-2025-52497"], NP,
            f"mbedtls_pem_read_buffer and mbedtls_pk_parse_* are not in {ESP32_MAP}."
        ),
        not_affected(
            ["CVE-2026-34872"], NP,
            "FFDH; MBEDTLS_DHM_C is off and no PSA FFDH code is linked."),
        not_affected(
            ["CVE-2026-34876"], NP,
            f"mbedtls_ccm_finish is not in {ESP32_MAP} (only ccm_crypt/update)."
        ),
        not_affected(
            ["CVE-2025-59438"], NP,
            f"Padding-error timing in PSA's error translation; no psa_crypto code is in {ESP32_MAP}, and the advisory says the legacy cipher API is constant-time."
        ),
        not_affected(["CVE-2026-50584"], NP,
                     f"ChaCha20; no chacha20 object in {ESP32_MAP}."),
        not_affected(["CVE-2025-52496"], NP,
                     "AESNI detection race; x86 only (Xtensa target)."),
        not_affected(
            ["CVE-2025-66442"], NP,
            "Only with LLVM's select-optimize; ESP-IDF builds with Xtensa GCC."
        ),
        not_affected(["CVE-2026-34871"], NP,
                     "Linux /dev/urandom fallback; not Linux."),
        not_affected(
            ["CVE-2024-45157"], NP,
            "Only when MBEDTLS_PSA_HMAC_DRBG_MD_TYPE is set; ESP-IDF 5.0.3 does not set it."
        ),
        not_affected(
            ["CVE-2024-23170"], NIEP,
            "RSA private-key timing; RSA is linked only through pk_wrap tables, the station loads no RSA key, and no EAP/enterprise code is linked."
        ),
        not_affected(
            ["CVE-2026-50587"], NP,
            f"Affects psa_asymmetric_decrypt and pk_decrypt with MBEDTLS_USE_PSA_CRYPTO only; no psa_crypto code is in {ESP32_MAP}."
        ),
        not_affected(
            ["CVE-2026-35336"], NIEP,
            "The only linked caller, wpa_supplicant's crypto_ecdh_set_peerkey (OWE), passes pbits/8 = 32 bytes, which is enough because OWE accepts only group 19 (P-256). That function has a separate key-length heap overflow; see finding 7 in the SBOM notes."
        ),
        not_affected(
            ["CVE-2024-28960"], NCA,
            "PSA buffers shared across an isolation boundary; the module runs one image with no such boundary."
        ),
        not_affected(
            ["CVE-2026-25835"], NCA,
            "RNG state cloning (fork, VM snapshot, hibernation); none happen on the module."
        ),
        not_affected(
            ["CVE-2025-54764", "CVE-2026-54435"], NCA,
            "Timing/trace side channels need a local attacker on the module."),
    ],
    "bootstrap:esp8266-rtos-sdk/components/lwip/lwip": [
        not_affected(
            LWIP_CVES, NP,
            "lwIP code is not in the image: the ESP8266 esp-nic link map keeps only lwip_htons from liblwip.a; --gc-sections discards the rest."
        ),
    ],
    "bootstrap:esp-idf/components/lwip/lwip": [
        not_affected(
            LWIP_CVES, NP,
            f"lwIP is not in the image: {ESP32_MAP} only LOADs liblwip.a and keeps nothing from it."
        ),
    ],
    "bootstrap:esp8266-rtos-sdk/components/wpa_supplicant": [
        # Dragonblood (hostap 2019-1/2019-5, 2022-1): Espressif's sae.c is pre-2.8
        # (fixed 40 iterations, but no const_time_select).
        not_affected(
            ["CVE-2019-9494", "CVE-2022-23303"], NCA,
            "The fork has the vulnerable SAE pattern (conditional copy in sae_derive_pwe_ecc, no const_time helpers), but exploitation needs attacker code co-located on the Wi-Fi module to observe cache access; the module runs only vendor firmware. Only used when the AP offers WPA3-SAE."
        ),
        not_affected(
            [
                "CVE-2019-9495", "CVE-2019-9497", "CVE-2019-9498",
                "CVE-2019-9499"
            ], NP,
            "EAP-pwd; no EAP method code is linked into the Wi-Fi module image."
        ),
        not_affected(
            ["CVE-2019-9496"], NIEP,
            "AP-side SAE confirm handling; esp-nic runs in station mode only."
        ),
        not_affected(
            ["CVE-2026-78807"], NCA,
            "Needs a local attacker able to configure networks on the device; the Wi-Fi module has no local users (configuration comes from the printer over UART)."
        ),
        not_affected(
            ["CVE-2026-21094"], NP,
            "Samsung Android (SMR) wpa_supplicant change; not in Espressif's fork."
        ),
    ],
    "lib/inih": [
        not_affected(
            ["CVE-2025-71450"], NP,
            "The abyss[] overlong-line discard loop (fixed in r63) does not exist in Buddy's r46-based ini.c."
        ),
    ],
}


def statements() -> list[dict]:
    components = load_components()
    out = []
    for key, decisions in TRIAGE.items():
        purl = components.get(key, {}).get("purl")
        if not purl:
            sys.exit(f"gen_vex.py: {key}: not in {MANIFEST} or has no purl")
        for cves, fields in decisions:
            out += [{
                "vulnerability": {
                    "name": cve
                },
                "products": [{
                    "@id": purl
                }],
                **fields
            } for cve in cves]
    return out


def main() -> int:
    body = statements()
    doc_id = uuid.uuid5(NAMESPACE, json.dumps(body, sort_keys=True))
    doc = {
        "@context": "https://openvex.dev/ns/v0.2.0",
        "@id": f"urn:uuid:{doc_id}",
        "author": "Buddy SBOM triage (utils/gen_vex.py)",
        "timestamp": f"{REVIEWED}T00:00:00Z",
        "version": 1,
        "statements": body,
    }
    OUTPUT.write_text(json.dumps(doc, indent=1) + "\n")
    print(f"{len(body)} statements -> {OUTPUT.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
