// Tests of the MSC class of the ST USB host library against a simulated mass storage device.
//
// The test links the library (Core and Class/MSC) with a simulated low-level driver (the USBH_LL_*
// functions below). The simulated device answers GET_MAX_LUN and the Bulk-Only Transport (CBW,
// data, CSW) for INQUIRY, TEST UNIT READY, READ CAPACITY(10), REQUEST SENSE, READ(10) and
// WRITE(10). Each URB completes at once.

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

#include "usbh_core.h"
#include "usbh_msc.h"

namespace {

uint32_t sim_ms = 0;

struct SimDevice {
    uint32_t block_size = 512; // answer to READ CAPACITY(10)
    uint8_t setup[8] {};
    USBH_URBStateTypeDef urb[16] {};
    uint32_t xfer[16] {};
    // Bulk-Only Transport
    std::vector<uint8_t> in_data;
    uint32_t in_pos = 0;
    uint32_t out_remaining = 0;
    std::vector<uint8_t> out_received; // the data stage of WRITE(10), as the device got it
    bool csw_pending = false;
    uint8_t csw[13] {};
};

SimDevice dev;

void put_be32(uint8_t *p, uint32_t v) {
    p[0] = v >> 24;
    p[1] = v >> 16;
    p[2] = v >> 8;
    p[3] = v;
}

uint32_t get_le32(const uint8_t *p) {
    return p[0] | p[1] << 8 | p[2] << 16 | uint32_t(p[3]) << 24;
}

void bot_command(const uint8_t *cbw) {
    const uint32_t len = get_le32(cbw + 8);
    const uint8_t op = cbw[15];
    dev.in_data.clear();
    dev.in_pos = 0;
    dev.out_remaining = 0;
    switch (op) {
    case 0x12: // INQUIRY
        dev.in_data.assign(std::min<uint32_t>(len, 36), 0);
        break;
    case 0x00: // TEST UNIT READY
        break;
    case 0x25: // READ CAPACITY(10)
        dev.in_data.assign(8, 0);
        put_be32(dev.in_data.data(), 0x0000FFFF);
        put_be32(dev.in_data.data() + 4, dev.block_size);
        break;
    case 0x28: // READ(10): the device sends all the data that the CBW asks for
        dev.in_data.assign(len, 0x5A);
        break;
    case 0x2A: // WRITE(10)
        dev.out_remaining = len;
        break;
    default:
        break;
    }
    std::memcpy(dev.csw, "USBS", 4);
    std::memcpy(dev.csw + 4, cbw + 4, 4); // tag
    std::memset(dev.csw + 8, 0, 5); // residue 0, status 0
    dev.csw_pending = true;
}

} // namespace

extern "C" {

uint32_t ticks_ms(void) { return sim_ms; }
uint32_t USB_GetCurrentFrame(USB_OTG_GlobalTypeDef const *) { return sim_ms & 0xFFFFu; }
uint32_t ulTaskNotifyTake(int, TickType_t) { return 1; }

USBH_StatusTypeDef USBH_LL_Init(USBH_HandleTypeDef *) { return USBH_OK; }
USBH_StatusTypeDef USBH_LL_DeInit(USBH_HandleTypeDef *) { return USBH_OK; }
USBH_StatusTypeDef USBH_LL_Start(USBH_HandleTypeDef *) { return USBH_OK; }
USBH_StatusTypeDef USBH_LL_Stop(USBH_HandleTypeDef *) { return USBH_OK; }
USBH_SpeedTypeDef USBH_LL_GetSpeed(USBH_HandleTypeDef *) { return USBH_SPEED_FULL; }
USBH_StatusTypeDef USBH_LL_ResetPort(USBH_HandleTypeDef *) { return USBH_OK; }
uint32_t USBH_LL_GetLastXferSize(USBH_HandleTypeDef *, uint8_t pipe) { return dev.xfer[pipe & 15]; }
USBH_StatusTypeDef USBH_LL_DriverVBUS(USBH_HandleTypeDef *, uint8_t) { return USBH_OK; }
USBH_StatusTypeDef USBH_LL_OpenPipe(USBH_HandleTypeDef *, uint8_t, uint8_t, uint8_t, uint8_t, uint8_t, uint16_t) { return USBH_OK; }
USBH_StatusTypeDef USBH_LL_ActivatePipe(USBH_HandleTypeDef *, uint8_t) { return USBH_OK; }
USBH_StatusTypeDef USBH_LL_ClosePipe(USBH_HandleTypeDef *, uint8_t) { return USBH_OK; }
USBH_StatusTypeDef USBH_LL_SetToggle(USBH_HandleTypeDef *, uint8_t, uint8_t) { return USBH_OK; }
uint8_t USBH_LL_GetToggle(USBH_HandleTypeDef *, uint8_t) { return 0; }
void USBH_Delay(uint32_t delay) { sim_ms += delay; }

USBH_URBStateTypeDef USBH_LL_GetURBState(USBH_HandleTypeDef *phost, uint8_t pipe) {
    sim_ms++;
    phost->Timer++;
    return dev.urb[pipe & 15];
}

USBH_StatusTypeDef USBH_LL_SubmitURB(USBH_HandleTypeDef *, uint8_t pipe, uint8_t direction, uint8_t ep_type,
    uint8_t token, uint8_t *pbuff, uint16_t length, uint8_t) {
    pipe &= 15;
    dev.urb[pipe] = USBH_URB_DONE;
    dev.xfer[pipe] = 0;
    if (ep_type == EP_TYPE_CTRL) {
        if (direction == 0 && token == 0) {
            std::memcpy(dev.setup, pbuff, 8);
        } else if (direction == 1 && length > 0 && dev.setup[1] == 0xFE) { // GET_MAX_LUN
            pbuff[0] = 0;
            dev.xfer[pipe] = 1;
        }
        return USBH_OK;
    }
    if (direction == 0) { // bulk OUT
        if (dev.out_remaining > 0) {
            const uint32_t n = std::min<uint32_t>(length, dev.out_remaining);
            dev.out_received.insert(dev.out_received.end(), pbuff, pbuff + n);
            dev.out_remaining -= n;
        } else if (length == 31 && get_le32(pbuff) == 0x43425355u) {
            bot_command(pbuff);
        }
        dev.xfer[pipe] = length;
        return USBH_OK;
    }
    // bulk IN: the data stage, then the CSW
    if (dev.in_pos < dev.in_data.size()) {
        const uint32_t n = std::min<uint32_t>(dev.in_data.size() - dev.in_pos, length);
        std::memcpy(pbuff, dev.in_data.data() + dev.in_pos, n);
        dev.in_pos += n;
        dev.xfer[pipe] = n;
    } else if (dev.csw_pending) {
        std::memcpy(pbuff, dev.csw, 13);
        dev.xfer[pipe] = 13;
        dev.csw_pending = false;
    }
    return USBH_OK;
}

} // extern "C"

namespace {

void user_callback(USBH_HandleTypeDef *, uint8_t) {}

/// Enumeration done: one MSC Bulk-Only interface. Runs the class init as USBH_Process() does.
USBH_HandleTypeDef *start(uint32_t block_size) {
    static USBH_HandleTypeDef h;
    static HCD_HandleTypeDef hcd;
    static USB_OTG_GlobalTypeDef otg;
    USBH_HandleTypeDef *ph = &h;
    if (ph->pActiveClass != nullptr) {
        ph->pActiveClass->DeInit(ph);
    }
    std::memset(ph, 0, sizeof(*ph));
    dev = SimDevice {};
    dev.block_size = block_size;

    USBH_Init(ph, user_callback, 0);
    USBH_RegisterClass(ph, USBH_MSC_CLASS);
    hcd.Instance = &otg;
    ph->pData = &hcd;
    ph->device.speed = USBH_SPEED_FULL;
    ph->device.address = 1;
    ph->device.is_connected = 1;
    ph->device.PortEnabled = 1;
    ph->device.CfgDesc.bNumInterfaces = 1;
    USBH_InterfaceDescTypeDef *itf = &ph->device.CfgDesc.Itf_Desc[0];
    itf->bInterfaceClass = 0x08;
    itf->bInterfaceSubClass = 0x06;
    itf->bInterfaceProtocol = 0x50;
    itf->bNumEndpoints = 2;
    itf->Ep_Desc[0].bEndpointAddress = 0x81;
    itf->Ep_Desc[0].bmAttributes = 2;
    itf->Ep_Desc[0].wMaxPacketSize = 64;
    itf->Ep_Desc[1].bEndpointAddress = 0x02;
    itf->Ep_Desc[1].bmAttributes = 2;
    itf->Ep_Desc[1].wMaxPacketSize = 64;
    ph->Control.pipe_size = 64;
    ph->Control.pipe_out = USBH_AllocPipe(ph, 0x00);
    ph->Control.pipe_in = USBH_AllocPipe(ph, 0x80);
    ph->RequestState = CMD_SEND;
    ph->gState = HOST_CLASS;
    ph->pActiveClass = ph->pClass[0];

    REQUIRE(ph->pActiveClass->Init(ph) == USBH_OK);
    USBH_StatusTypeDef st = USBH_BUSY;
    for (int i = 0; i < 1000 && st == USBH_BUSY; i++) {
        st = ph->pActiveClass->Requests(ph);
    }
    auto *msc = static_cast<MSC_HandleTypeDef *>(ph->pActiveClass->pData);
    for (int i = 0; i < 100000 && msc->state != MSC_IDLE; i++) {
        ph->pActiveClass->BgndProcess(ph);
    }
    REQUIRE(msc->state == MSC_IDLE);
    return ph;
}

constexpr size_t block = 512; // the buffer that FatFs and the transfers give for one block
constexpr size_t guard = 65536; // more than the largest possible overflow (65535 - 512)
constexpr uint8_t guard_fill = 0xC3;

size_t guard_bytes_changed(const std::vector<uint8_t> &buf) {
    return std::count_if(buf.begin() + block, buf.end(), [](uint8_t b) { return b != guard_fill; });
}

} // namespace

TEST_CASE("usb_host: MSC read and write move one block of 512 bytes", "[usb_host]") {
    USBH_HandleTypeDef *ph = start(512);
    std::vector<uint8_t> buf(block + guard, guard_fill);

    CHECK(USBH_MSC_Read(ph, 0, 0, buf.data(), 1) == USBH_OK);
    CHECK(std::all_of(buf.begin(), buf.begin() + block, [](uint8_t b) { return b == 0x5A; }));
    CHECK(guard_bytes_changed(buf) == 0);

    CHECK(USBH_MSC_Write(ph, 0, 0, buf.data(), 1) == USBH_OK);
    CHECK(dev.out_received.size() == block);
}

TEST_CASE("usb_host: MSC read and write refuse a device with a block size other than 512", "[usb_host]") {
    // The device reports the block size in READ CAPACITY. The transfer is length * block_size bytes.
    // With a larger block size, the device wrote past the 512-byte buffer of each block.
    const uint32_t block_size = GENERATE(1024u, 4096u, 65535u, 256u);
    CAPTURE(block_size);
    USBH_HandleTypeDef *ph = start(block_size);
    std::vector<uint8_t> buf(block + guard, guard_fill);

    CHECK(USBH_MSC_Read(ph, 0, 0, buf.data(), 1) == USBH_NOT_SUPPORTED);
    CHECK(guard_bytes_changed(buf) == 0);

    // A write must not send the memory after the buffer to the device
    CHECK(USBH_MSC_Write(ph, 0, 0, buf.data(), 1) == USBH_NOT_SUPPORTED);
    CHECK(dev.out_received.empty());
}
