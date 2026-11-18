// Tests of the Wi-Fi scan in espif.cpp: espif::scan::get_ap_info() and the ESP replies.
//
// The test includes espif.cpp, so that it can give bytes to the static UART parser uart_input().
// The stub directory replaces the UART, the DMA and the FreeRTOS queue (see stub/freertos/queue.hpp:
// no scheduler, a send to a full queue is counted instead of blocking).

#include <array>
#include <cstdint>
#include <optional>
#include <variant>
#include <vector>
#include <arpa/inet.h> // htons(): the test lwipopts.h turns off the lwIP byte order functions

// The value of src/persistent_stores/store_instances/config_store/constants.hpp. That header needs
// the printer options, so the test does not include it.
namespace config_store_ns {
inline constexpr size_t wifi_max_ssid_len { 32 };
}

#define FORCE_INLINE inline

#include "espif.cpp"

#include <catch2/catch_test_macros.hpp>

// ---- stubs of the platform ----
UART_HandleTypeDef uart_handle_for_esp {};

static size_t tx_count = 0;

HAL_StatusTypeDef HAL_UART_Transmit_DMA(UART_HandleTypeDef *, const uint8_t *, uint16_t) {
    ++tx_count;
    espif_tx_callback(); // the DMA ends at once
    return HAL_OK;
}
HAL_StatusTypeDef HAL_UART_Abort(UART_HandleTypeDef *) { return HAL_OK; }
HAL_StatusTypeDef HAL_UART_Init(UART_HandleTypeDef *) { return HAL_OK; }
HAL_StatusTypeDef HAL_UART_Receive_DMA(UART_HandleTypeDef *, uint8_t *, uint16_t) { return HAL_OK; }
bool running_in_tester_mode() { return false; }
uint32_t osDelay(uint32_t) { return 0; }

extern "C" {
void metric_record_custom(metric_t *, const char *, ...) {}
struct pbuf *pbuf_alloc_rx(u16_t) { return nullptr; }
u8_t pbuf_free(struct pbuf *) { return 0; }
void netif_set_link_up(struct netif *) {}
void netif_set_link_down(struct netif *) {}
err_t netifapi_netif_common(struct netif *, netifapi_void_fn, netifapi_errt_fn) { return ERR_OK; }
}

namespace {

/// The ESP reply to MSG_SCAN_AP_GET: intron, type, AP index, length 33, 32 bytes of SSID, password flag
std::vector<uint8_t> ap_reply(uint8_t index, uint8_t ssid_byte) {
    std::vector<uint8_t> msg(std::begin(tx_message.intron), std::end(tx_message.intron));
    msg.push_back(MSG_SCAN_AP_GET);
    msg.push_back(index);
    msg.push_back(0);
    msg.push_back(33);
    msg.insert(msg.end(), config_store_ns::wifi_max_ssid_len, ssid_byte);
    msg.push_back(1);
    return msg;
}

void esp_sends(std::vector<uint8_t> msg) {
    uart_input(msg.data(), msg.size(), nullptr);
}

void reset() {
    ScanData::ap_info_queue.clear();
    freertos::blocked_sends = 0;
    freertos::on_wait = nullptr;
    ::scan.ap_count = 8;
    reset_parser = true;
}

} // namespace

TEST_CASE("espif: a late scan reply does not write into the buffer of a call that returned", "[espif]") {
    reset();
    // get_ap_info() sends MSG_SCAN_AP_GET up to 5 times and waits 10 ms for each reply. Here, no
    // reply comes in time, and the call fails. The caller's buffer is then out of scope.
    std::array<uint8_t, config_store_ns::wifi_max_ssid_len> old_buffer;
    old_buffer.fill(0xC3);
    bool needs_password = false;
    CHECK(espif::scan::get_ap_info(1, old_buffer, needs_password) != ERR_OK);
    CHECK(tx_count > 0);

    // A reply to one of the tries arrives late. It must not change the old buffer.
    esp_sends(ap_reply(1, 'L'));
    CHECK(std::all_of(old_buffer.begin(), old_buffer.end(), [](uint8_t b) { return b == 0xC3; }));
}

TEST_CASE("espif: two late scan replies do not block the network task", "[espif]") {
    reset();
    std::array<uint8_t, config_store_ns::wifi_max_ssid_len> buffer {};
    bool needs_password = false;
    CHECK(espif::scan::get_ap_info(2, buffer, needs_password) != ERR_OK);

    // The queue holds 1 reply. The parser runs in the network task: a blocking send of the
    // second reply would stop all networking until the next scan request.
    esp_sends(ap_reply(2, 'X'));
    esp_sends(ap_reply(2, 'Y'));
    CHECK(freertos::blocked_sends == 0);
}

TEST_CASE("espif: get_ap_info() returns the SSID of the reply for its index", "[espif]") {
    reset();
    // A late reply to an earlier call is still in the queue
    std::array<uint8_t, config_store_ns::wifi_max_ssid_len> earlier {};
    bool needs_password = false;
    CHECK(espif::scan::get_ap_info(5, earlier, needs_password) != ERR_OK);
    esp_sends(ap_reply(5, 'S'));

    // During the wait, the ESP sends a reply for another index, then the reply for this call
    int waits = 0;
    freertos::on_wait = [&] {
        ++waits;
        if (waits == 1) {
            esp_sends(ap_reply(3, 'X'));
        } else {
            esp_sends(ap_reply(4, 'A'));
        }
    };
    std::array<uint8_t, config_store_ns::wifi_max_ssid_len> buffer {};
    REQUIRE(espif::scan::get_ap_info(4, buffer, needs_password) == ERR_OK);
    CHECK(std::all_of(buffer.begin(), buffer.end(), [](uint8_t b) { return b == 'A'; }));
    CHECK(needs_password);
    freertos::on_wait = nullptr;
}

TEST_CASE("espif: get_ap_info() drops a late reply for the same index", "[espif]") {
    reset();
    // A call for index 4 fails, and its reply comes late, with an old SSID
    std::array<uint8_t, config_store_ns::wifi_max_ssid_len> buffer {};
    bool needs_password = false;
    CHECK(espif::scan::get_ap_info(4, buffer, needs_password) != ERR_OK);
    esp_sends(ap_reply(4, 'S'));

    // The next call for index 4 gets the new reply, not the late one
    freertos::on_wait = [] { esp_sends(ap_reply(4, 'A')); };
    REQUIRE(espif::scan::get_ap_info(4, buffer, needs_password) == ERR_OK);
    CHECK(std::all_of(buffer.begin(), buffer.end(), [](uint8_t b) { return b == 'A'; }));
    freertos::on_wait = nullptr;
}
