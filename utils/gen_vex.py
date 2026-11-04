#!/usr/bin/env python3
"""Write lib/vex.json (OpenVEX 0.2.0) from the triage table below.

The table is the reviewed record of which scanner findings apply to Buddy and
why. Grype reads the output with `grype sbom:buddy.cdx.json --vex lib/vex.json`.
Products are the purls from lib/sbom.yml; an unknown component name fails.

Status values: not_affected (with an OpenVEX justification), affected (with an
action), under_investigation. Keep impact statements specific: name the config
option, file or call site that decides it.

Needs pyyaml.
"""
from __future__ import annotations

import json
import sys
from pathlib import Path

import yaml

ROOT = Path(__file__).resolve().parent.parent
MANIFEST = ROOT / "lib" / "sbom.yml"
OUTPUT = ROOT / "lib" / "vex.json"

NP = "vulnerable_code_not_present"
NIEP = "vulnerable_code_not_in_execute_path"
NCA = "vulnerable_code_cannot_be_controlled_by_adversary"
MIT = "inline_mitigations_already_exist"

MBEDTLS_CFG = "include/mbedtls/cipher_config_ece.h"
NVD_RANGE = "NVD version range is wrong for this release"

# component (manifest key) -> list of (cves, status, justification-or-action, statement)
TRIAGE = {
    "lib/Middlewares/Third_Party/mbedtls": [
        (["CVE-2022-35409", "CVE-2022-46393"], "not_affected", NP,
         f"DTLS only; DTLS and the SSL server are disabled ({MBEDTLS_CFG})."),
        (["CVE-2023-43615"], "not_affected", NP,
         "Only NULL/RC4 suites are affected; the only suite enabled is ECDHE-ECDSA-AES128-GCM-SHA256."
         ),
        (["CVE-2024-23775", "CVE-2025-47917",
          "CVE-2025-48965"], "not_affected", NP,
         "X.509 writing/string-to-names code; MBEDTLS_X509_CRT_WRITE_C and MBEDTLS_X509_CREATE_C are off."
         ),
        (["CVE-2024-28960", "CVE-2024-45157", "CVE-2026-25835"],
         "not_affected", NP, "PSA Crypto; MBEDTLS_PSA_CRYPTO_C is off."),
        (["CVE-2025-52496"], "not_affected", NP,
         "AESNI (x86) support; MBEDTLS_AESNI_C is off and the target is Cortex-M."
         ),
        (["CVE-2025-52497"], "not_affected", NP,
         "PEM parsing; MBEDTLS_PEM_PARSE_C is off."),
        (["CVE-2025-66442"], "not_affected", NP,
         "Only occurs with LLVM select-optimize; Buddy builds with GCC."),
        (["CVE-2026-34871"], "not_affected", NP,
         "Linux /dev/urandom fallback; MBEDTLS_NO_PLATFORM_ENTROPY with a hardware entropy source."
         ),
        (["CVE-2026-34872"], "not_affected", NP,
         "FFDH; MBEDTLS_DHM_C is off."),
        (["CVE-2026-34877"], "not_affected", NP,
         "Context serialization; MBEDTLS_SSL_CONTEXT_SERIALIZATION is off."),
        (["CVE-2025-59438"], "not_affected", NP,
         "Padding error timing in the cipher layer; no cipher padding mode is enabled and E2EE calls mbedtls_aes_crypt_cbc directly."
         ),
        (["CVE-2025-54764"], "not_affected", NCA,
         "Microarchitectural (SSBleed/M-Step) attack on RSA keygen needs a co-located privileged attacker; not applicable to a Cortex-M MCU."
         ),
        (["CVE-2025-27809"], "not_affected", MIT,
         "src/connect/tls/tls.cpp calls mbedtls_ssl_set_hostname()."),
        (["CVE-2021-36647"], "not_affected", NP,
         f"{NVD_RANGE}: fixed in 2.27.0/2.16.11, before 2.28.0."),
        (["CVE-2021-43666"], "not_affected", NP,
         f"{NVD_RANGE}: the pkcs12 empty-password fix is in 2.28.0 (Fixes #5136)."
         ),
        (["CVE-2021-45451"], "not_affected", NP,
         f"{NVD_RANGE}: psa_aead_generate_nonce does not exist in 2.28."),
        (["CVE-2023-52353"], "not_affected", NP,
         "3.x TLS version negotiation; not present in the 2.28 branch (no advisory, not in its ChangeLog)."
         ),
        (["CVE-2025-27810"], "affected",
         "Upgrade mbedTLS (2.28 is end of life; fixed in 2.28.10) or move to 3.6 LTS.",
         "Finished-message check can be skipped after an allocation or hardware failure; Buddy allocates with calloc_fallible on a small heap."
         ),
        (["CVE-2022-46392", "CVE-2024-23170"], "affected",
         "Upgrade mbedTLS; affects only presets with HAS_E2EE_SUPPORT() (RSA enabled).",
         "RSA private-key side channels; RSA-OAEP decrypt is used by E2EE (src/common/e2ee/e2ee.cpp). Requires precise timing or memory-access observation."
         ),
    ],
    "lib/Middlewares/Third_Party/FatFs": [
        (["CVE-2026-6683", "CVE-2026-6687"], "not_affected", NP,
         "exFAT code; FF_FS_EXFAT 0 (include/buddy/ffconf.h)."),
        (["CVE-2026-6684"], "not_affected", NP,
         "GPT scanning with FF_LBA64; FF_LBA64 0."),
        (["CVE-2026-6682"], "affected",
         "Backport the mount_volume() fasize overflow check or update FatFs.",
         "FAT32 mount_volume() fasize overflow; reachable with a crafted USB drive."
         ),
        (["CVE-2026-6686"], "under_investigation", None,
         "f_lseek past EOF exposes old cluster data; check whether Buddy ever extends files with f_lseek."
         ),
        (["CVE-2026-6688"], "affected",
         "Size readdir's name buffer NAME_MAX + 1 (fork branch readdir-off-by-one, 4c9ad7e8a).",
         "Buddy's caller has the pattern: dirnext_r (src/buddy/filesystem_fatfs.cpp) writes filename[len + 1] into libsysbase's char[NAME_MAX] buffer; a crafted FAT volume (254-byte UTF-8 LFN, NUL inside the 8.3 entry) gives a 1-byte stack write. Reproduced under ASan."
         ),
    ],
    "lib/Middlewares/Third_Party/FreeRTOS": [
        (["CVE-2026-77235", "CVE-2026-77236"], "not_affected", NP,
         "TrustZone secure-context code; Buddy uses the ARM_CM33_NTZ (non-TrustZone) port."
         ),
        (["CVE-2026-77234", "CVE-2026-77237"], "under_investigation", None,
         "MPU ports only. The main firmware has the MPU off; src/puppy/xbuddy_extension sets configENABLE_MPU 1."
         ),
    ],
    "lib/Middlewares/Third_Party/LwIP": [
        (["CVE-2020-22283"], "not_affected", NP,
         "ICMPv6; LWIP_IPV6 is off (include/buddy/lwipopts.h)."),
        (["CVE-2020-22284"], "not_affected", NP,
         "6LoWPAN/ZEP netif; not compiled."),
    ],
    "lib/WUI/mdns": [
        (["CVE-2020-22283"], "not_affected", NP,
         "ICMPv6 is not part of the mDNS responder; LWIP_IPV6 is off."),
    ],
    "lib/WUI/sntp": [
        (["CVE-2020-22283"], "not_affected", NP, "ICMPv6; LWIP_IPV6 is off."),
        (["CVE-2020-22284"], "not_affected", NP,
         "6LoWPAN/ZEP netif; not part of the SNTP client."),
    ],
    "lib/Marlin": [
        (["CVE-2018-1000537"], "not_affected", NP,
         "Marlin's sd/cardreader.cpp is not compiled (lib/AddMarlin.cmake); Buddy uses FatFs and its own media code."
         ),
    ],
    "lib/Middlewares/ST/STM32_USB_Host_Library": [
        (["CVE-2021-42553"], "affected",
         "Add `ep_ix < USBH_MAX_NUM_ENDPOINTS` to the endpoint loop in USBH_ParseCfgDesc (as ST v3.5.1/v3.5.3), or update to v3.5.5.",
         "Endpoint-count overflow of Itf_Desc[].Ep_Desc[2] into USBH_HandleTypeDef (usbh_ctlreq.c:453); triggered by plugging in a crafted USB device."
         ),
    ],
    "bootstrap:gcc-arm-none-eabi/gcc-runtime": [
        (["CVE-2023-4039"], "not_affected", NP,
         "AArch64 -fstack-protector issue; Buddy targets 32-bit Cortex-M (Thumb)."
         ),
    ],
    "bootstrap:esp8266-rtos-sdk/components/freertos": [
        (["CVE-2024-28115", "CVE-2026-77234",
          "CVE-2026-77237"], "not_affected", NP,
         "MPU-port issues; the ESP8266 port has no MPU support."),
        (["CVE-2021-31571", "CVE-2021-31572",
          "CVE-2021-32020"], "not_affected", NCA,
         "Kernel size overflows reachable only through sizes chosen by the firmware itself (esp-nic), not by network input."
         ),
    ],
    "bootstrap:esp8266-rtos-sdk/components/mbedtls/mbedtls": [
        ([
            "CVE-2020-16150", "CVE-2020-36423", "CVE-2020-36425",
            "CVE-2020-36426", "CVE-2020-36476", "CVE-2020-36477",
            "CVE-2020-36478", "CVE-2021-44732", "CVE-2022-35409",
            "CVE-2022-46393", "CVE-2023-43615", "CVE-2023-52353",
            "CVE-2024-23775", "CVE-2025-27809", "CVE-2025-27810",
            "CVE-2025-47917", "CVE-2025-48965", "CVE-2025-52497"
        ], "not_affected", NIEP,
         "TLS/DTLS/X.509/PEM code. In the Wi-Fi module image mbedTLS is only called from wpa_supplicant's crypto_mbedtls-bignum.c and -ec.c; esp-nic uses no TLS. (Confirm with the ESP link map.)"
         ),
        ([
            "CVE-2020-36421", "CVE-2020-36424", "CVE-2022-46392",
            "CVE-2024-23170", "CVE-2021-24119", "CVE-2025-54764"
        ], "not_affected", NIEP,
         "RSA/static-DH/PEM side channels; the supplicant does not use RSA or PEM (SAE uses ECC/FFC bignum)."
         ),
        (["CVE-2020-10932", "CVE-2020-36422",
          "CVE-2020-36475"], "not_affected", NCA,
         "ECC/bignum side channels needing local, cache- or enclave-grade observation of the module; not available to a radio-range attacker."
         ),
        (["CVE-2025-52496"], "not_affected", NP,
         "AESNI (x86) support; the ESP8266 is Xtensa."),
        (["CVE-2025-66442"], "not_affected", NP,
         "LLVM select-optimize only; the ESP8266 SDK builds with GCC."),
        (["CVE-2026-34871"], "not_affected", NP,
         "Linux /dev/urandom fallback; not Linux."),
        (["CVE-2026-34872"], "not_affected", NP,
         "Reported for 3.5.x and later; not this 2.16 fork."),
        (["CVE-2021-36647", "CVE-2021-43666",
          "CVE-2021-45451"], "not_affected", NP,
         f"{NVD_RANGE} (fixed earlier, or the API does not exist in 2.16)."),
        (["CVE-2025-59438"], "not_affected", NIEP,
         "Cipher-layer padding timing; the supplicant uses its own AES/CCMP code."
         ),
    ],
    "bootstrap:esp-idf": [
        ([
            "CVE-2025-66409", "CVE-2025-68473", "CVE-2025-68474",
            "CVE-2025-55297", "CVE-2025-64342", "CVE-2026-81507",
            "CVE-2026-46532"
        ], "not_affected", NP,
         "Bluetooth/BluFi; the bt component is not fetched (bootstrap.py `missing`)."
         ),
        (["CVE-2026-45542"], "not_affected", NP,
         "protocomm is not fetched (bootstrap.py `missing`)."),
        (["CVE-2026-55687"], "not_affected", NP,
         "JPEG driver for ESP32-P4; not part of an ESP32 build."),
        (["CVE-2026-45328", "CVE-2026-45329"], "not_affected", NP,
         "ESP-TEE exists from 5.5; not in 5.0.3."),
        (["CVE-2026-45541", "CVE-2024-53845"], "not_affected", NIEP,
         "esp_http_server and ESPTouch are not used by esp-nic."),
        (["CVE-2026-45160"], "not_affected", NIEP,
         "DHCP server option parser; esp-nic runs WIFI_MODE_STA only (uart_nic_esp32.cpp:147), so dhcps never starts."
         ),
        (["CVE-2024-28183"], "not_affected", NP,
         "Secure-boot anti-rollback TOCTOU; secure boot is not enabled in esp32.sdkconfig."
         ),
    ],
}

# mbedTLS advisories from July 2026. Their CVE IDs are still RESERVED at CVE.org,
# so no CPE feed has them; utils/sbom_feeds.py mbedtls-advisories finds them.
TRIAGE["lib/Middlewares/Third_Party/mbedtls"] += [
    (["CVE-2026-50584"], "not_affected", NP,
     "ChaCha20; MBEDTLS_CHACHA20_C is off."),
    (["CVE-2026-50581"], "not_affected", NP,
     "Extended master secret; MBEDTLS_SSL_EXTENDED_MASTER_SECRET is off."),
    (["CVE-2026-35336"], "not_affected", NIEP,
     "mbedtls_ecdh_calc_secret with an undersized buffer; TLS always passes a large enough buffer and Buddy has no direct calls."
     ),
    (["CVE-2026-50587"], "not_affected", NIEP,
     "RSA PKCS#1 v1.5 decryption; E2EE uses RSA-OAEP (PKCS_V21)."),
    (["CVE-2026-50585", "CVE-2026-50586"], "not_affected", NP,
     "DTLS session-reset state and TLS 1.2 server NewSessionTicket; no DTLS, no SSL server, no session tickets."
     ),
    (["CVE-2026-50580"], "not_affected", NP,
     "ECDHE-PSK client; no PSK suites are enabled."),
    (["CVE-2026-49300"], "not_affected", NCA,
     "CA-bit parsing differential needs user-supplied trust roots validated elsewhere; Buddy embeds one fixed CA."
     ),
    (["CVE-2026-54435"], "not_affected", NCA,
     "ECC fast-reduction side channel needs a local privileged trace (enclave model); not an attacker the MCU has."
     ),
    (["CVE-2026-54441"], "affected",
     "Upgrade mbedTLS (no 2.28 fix exists; 2.28 is end of life).",
     "Signature-algorithm restrictions are not enforced on the certificate chain; low impact with a single pinned Prusa CA."
     ),
]
TRIAGE["bootstrap:esp8266-rtos-sdk/components/mbedtls/mbedtls"] += [
    ([
        "CVE-2026-49300", "CVE-2026-50580", "CVE-2026-50581", "CVE-2026-50584",
        "CVE-2026-50585", "CVE-2026-50586", "CVE-2026-35336", "CVE-2026-54441"
    ], "not_affected", NIEP,
     "TLS/X.509/ECDH-API code; in the Wi-Fi module image mbedTLS is only called from wpa_supplicant's bignum/ECC wrappers. (Confirm with the ESP link map.)"
     ),
    (["CVE-2026-54435"], "not_affected", NCA,
     "ECC fast-reduction side channel needs local, privileged trace access to the module."
     ),
]
# ESP32 image: decide per CVE once the link map shows what esp-nic links.
TRIAGE["bootstrap:esp-idf/components/mbedtls/mbedtls"] = [
    ([
        "CVE-2023-43615", "CVE-2023-45199", "CVE-2023-52353", "CVE-2024-23170",
        "CVE-2024-23775", "CVE-2024-28960", "CVE-2024-30166", "CVE-2024-45157",
        "CVE-2024-45159", "CVE-2025-27809", "CVE-2025-27810", "CVE-2025-47917",
        "CVE-2025-48965", "CVE-2025-49600", "CVE-2025-49601", "CVE-2025-52496",
        "CVE-2025-52497", "CVE-2025-54764", "CVE-2025-59438", "CVE-2025-66442",
        "CVE-2026-25834", "CVE-2026-25835", "CVE-2026-34871", "CVE-2026-34872",
        "CVE-2026-34876", "CVE-2026-34877", "CVE-2026-35336", "CVE-2026-49300",
        "CVE-2026-50579", "CVE-2026-50580", "CVE-2026-50581", "CVE-2026-50584",
        "CVE-2026-50585", "CVE-2026-50586", "CVE-2026-50587", "CVE-2026-50588",
        "CVE-2026-50640", "CVE-2026-54435", "CVE-2026-54441", "CVE-2026-73064"
    ], "under_investigation", None,
     "esp-nic uses no TLS, so most of mbedTLS should be garbage-collected at link time; decide per CVE from the ESP32 link map."
     ),
]

# Found by utils/sbom_feeds.py (GitHub repository advisories, NVD++ keywords), 2026-11-04.
TRIAGE["lib/Middlewares/Third_Party/FreeRTOS"] += [
    (["CVE-2026-119086"], "under_investigation", None,
     "MPU ports with queue sets only (like CVE-2026-77237); main firmware has the MPU off, src/puppy/xbuddy_extension has it on."
     ),
    (["CVE-2026-109587", "CVE-2026-109588"], "not_affected", NP,
     "Arm Cortex-R82 MPU port, V11.3.0-V11.3.1 only."),
]
TRIAGE["bootstrap:esp8266-rtos-sdk/components/freertos"] += [
    (["CVE-2026-119086"], "not_affected", NP,
     "MPU-port issue; the ESP8266 port has no MPU support."),
]
TRIAGE["bootstrap:esp-idf"] += [
    ([
        "CVE-2026-113781", "CVE-2026-81508", "CVE-2026-25507",
        "CVE-2026-25508", "CVE-2022-24893"
    ], "not_affected", NP,
     "Bluetooth (BLE Mesh, A2DP, BLE provisioning); the bt component is not fetched."
     ),
    (["CVE-2026-113782"], "not_affected", NP,
     "Thread border router; openthread is not fetched."),
    (["CVE-2026-113780"], "not_affected", NP,
     "ESP-TEE exists from 5.5; not in 5.0.3."),
    (["CVE-2025-65092"], "not_affected", NP,
     "ESP32-P4 JPEG decoder; not part of an ESP32 build."),
    (["CVE-2026-25532"], "not_affected", NIEP,
     "WPS enrollee; esp-nic never enables WPS."),
    (["CVE-2025-52471"], "not_affected", NIEP,
     "ESP-NOW; esp-nic does not use it."),
    (["CVE-2026-113783"], "under_investigation", None,
     "lwIP DNS cache in ESP-IDF; esp-nic resolves nothing itself, but whether lwIP is active on the module needs the ESP32 link map."
     ),
]
TRIAGE["bootstrap:esp8266-rtos-sdk/components/wpa_supplicant"] = [
    # Dragonblood (hostap 2019-1/2019-5, 2022-1) against Espressif's fork of the
    # ESP8266 SDK (sae.c is pre-2.8: fixed 40 iterations, but no const_time_select).
    (["CVE-2019-9494", "CVE-2022-23303"], "not_affected", NCA,
     "The fork has the vulnerable SAE pattern (conditional copy in sae_derive_pwe_ecc, no const_time helpers), but exploitation needs attacker code co-located on the Wi-Fi module to observe cache access; the module runs only vendor firmware. Only used when the AP offers WPA3-SAE."
     ),
    (["CVE-2019-9495", "CVE-2019-9497", "CVE-2019-9498",
      "CVE-2019-9499"], "not_affected", NP,
     "EAP-pwd; no EAP method code is linked into the Wi-Fi module image."),
    (["CVE-2019-9496"], "not_affected", NIEP,
     "AP-side SAE confirm handling; esp-nic runs in station mode only."),
    (["CVE-2026-78807"], "not_affected", NCA,
     "Needs a local attacker able to configure networks on the device; the Wi-Fi module has no local users (configuration comes from the printer over UART)."
     ),
    (["CVE-2026-21094"], "not_affected", NP,
     "Samsung Android (SMR) wpa_supplicant change; not in Espressif's fork."),
]

UNDER_INVESTIGATION_ALL = {
    "bootstrap:esp8266-rtos-sdk/components/lwip/lwip":
    "No lwIP strings in the shipped image; it may not be linked. Confirm with the ESP8266 link map.",
    "bootstrap:esp-idf/components/lwip/lwip":
    "esp-nic forwards frames; confirm with the ESP32 link map which lwIP parts are linked.",
}


def main() -> int:
    components = yaml.safe_load(MANIFEST.read_text())["components"]

    def purl(key: str) -> str:
        entry = components.get(key)
        if entry is None or not entry.get("purl"):
            sys.exit(f"gen_vex.py: {key}: not in {MANIFEST} or has no purl")
        return entry["purl"]

    statements = []
    for key, rows in TRIAGE.items():
        product = [{"@id": purl(key)}]
        for cves, status, how, text in rows:
            for cve in cves:
                st = {
                    "vulnerability": {
                        "name": cve
                    },
                    "products": product,
                    "status": status
                }
                if status == "not_affected":
                    st["justification"] = how
                    st["impact_statement"] = text
                elif status == "affected":
                    st["action_statement"] = how
                    st["status_notes"] = text
                else:
                    st["status_notes"] = text
                statements.append(st)
    doc = {
        "@context": "https://openvex.dev/ns/v0.2.0",
        "@id":
        "https://github.com/anyasabo/Prusa-Firmware-Buddy/blob/sbom-scanning/lib/vex.json",
        "author": "Buddy SBOM triage (see lib/sbom.yml and utils/gen_vex.py)",
        "timestamp": "2026-11-04T00:00:00Z",
        "version": 1,
        "statements": statements,
    }
    OUTPUT.write_text(json.dumps(doc, indent=1) + "\n")
    print(f"{len(statements)} statements -> {OUTPUT.relative_to(ROOT)}")
    for key, why in UNDER_INVESTIGATION_ALL.items():
        purl(key)  # validate
        print(f"  open (all CVEs): {key}: {why}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
