/*
 * Tests for the upload path of the server (the TransferSlot): how it takes
 * pbufs from the connection and passes the data on to the file.
 *
 * The transfer is injected directly into the server, bypassing the request
 * parsing and the upload handler.
 */

#include <nhttp/server.h>
#include <nhttp/splice.h>
#include <transfers/partial_file.hpp>
#include <lwip/altcp.h>
#include <lwip/pbuf.h>
#include <lwip/priv/altcp_priv.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

using std::string;
using namespace nhttp;

namespace {

class FakeConn : public altcp_pcb {
public:
    size_t recved = 0;
    bool closed = false;

    FakeConn() {
        std::memset(static_cast<altcp_pcb *>(this), 0, sizeof(altcp_pcb));
        fns = &funcs;
    }

private:
    static void recved_cb(altcp_pcb *me, uint16_t len) {
        static_cast<FakeConn *>(me)->recved += len;
    }

    static err_t close_cb(altcp_pcb *me) {
        static_cast<FakeConn *>(me)->closed = true;
        return ERR_OK;
    }

    static constexpr altcp_functions funcs = {
        .recved = &recved_cb,
        .close = &close_cb,
    };
};

// Collects what's written. Takes at most `budget` bytes, then it's busy (like
// the USB with all its buffers in flight) until usb_ready().
class FakeFile final : public transfers::PartialFile {
public:
    size_t budget = 0;
    string written;
    // Smaller than the segments, so they get written in several parts.
    std::array<uint8_t, 100> buffer;

    FakeFile()
        : PartialFile(State {}) {}

    bool supports_random_write() const override { return false; }
    bool seek(size_t) override { return false; }
    size_t tell() override { return written.size(); }
    bool write(const uint8_t *, size_t) override { return false; }
    bool sync() override { return true; }
    void release_file() override {}

    BufferPeek get_current_buffer(bool) override {
        if (budget == 0) {
            return WouldBlock {};
        }
        return BufferAndSizes { buffer.data(), 0, std::min(buffer.size(), budget) };
    }

    bool advance_written(size_t by) override {
        written.append(reinterpret_cast<const char *>(buffer.data()), by);
        budget -= by;
        return true;
    }

    // The USB has written its buffers, notify the server like the real file does.
    void usb_ready() {
        budget = std::numeric_limits<size_t>::max();
        written_callback(written_callback_arg);
    }
};

class FakeTransfer final : public splice::Transfer {
public:
    mutable FakeFile f;
    // The result as seen by done() (release() resets it afterwards).
    std::optional<splice::Result> done_result;
    // What poll_ready() reports (the file being prepared asynchronously).
    Readiness readiness = Readiness::Ready;
    bool aborted = false;

    Readiness poll_ready() override {
        return readiness;
    }

    // Finishing off the tcpip thread (start_finish/poll_finished).
    bool finish_async = false;
    bool finish_started = false;
    Readiness finished = Readiness::Ready;

    bool start_finish() override {
        finish_started = finish_async;
        return finish_async;
    }

    Readiness poll_finished() override {
        return finished;
    }

    void abort_prepare() override {
        aborted = true;
    }

    transfers::PartialFile *file() const override {
        // Like the real upload: no file until it's prepared.
        return readiness == Readiness::Ready ? &f : nullptr;
    }

    std::tuple<size_t, size_t> write(const uint8_t *in, size_t in_size, uint8_t *out, size_t out_size) override {
        const size_t size = std::min(in_size, out_size);
        memcpy(out, in, size);
        return { size, size };
    }

    std::optional<std::tuple<http::Status, const char *>> done() override {
        done_result = result;
        if (readiness == Readiness::Failed) {
            return std::make_tuple(http::Status::Conflict, "File already exists");
        }
        return std::nullopt;
    }

    bool progress(size_t) override {
        return true;
    }
};

class FakeDefs final : public ServerDefs {
public:
    const handler::Selector *const *selectors() const override { return nullptr; }
    const char *get_password() const override { return ""; }
    altcp_pcb *listener_alloc() const override { return nullptr; }
};

// A received TCP segment, as lwip gives it to us.
struct Segment {
    pbuf_custom p;
    string data;
};

size_t pbufs_freed = 0;

void segment_free(pbuf *) {
    pbufs_freed++;
}

struct Upload {
    // Outlives the server, which may still hold some of them.
    std::vector<std::unique_ptr<Segment>> segments;
    string sent;
    FakeDefs defs;
    FakeConn conn;
    FakeTransfer transfer;
    std::unique_ptr<Server> server = std::make_unique<Server>(defs);

    Upload(size_t expected_data, size_t file_budget, splice::Transfer::Readiness readiness = splice::Transfer::Readiness::Ready) {
        pbufs_freed = 0;
        transfer.f.budget = file_budget;
        transfer.readiness = readiness;
        server->inject_transfer(&conn, nullptr, 0, &transfer, expected_data);
    }

    // Sends a segment, returns what the server returned to lwip (as int, so
    // Catch doesn't print it as a char).
    int send(size_t size) {
        auto &segment = segments.emplace_back(std::make_unique<Segment>());
        for (size_t i = 0; i < size; i++) {
            segment->data += static_cast<char>('a' + (sent.size() + i) % 26);
        }
        sent += segment->data;
        segment->p.custom_free_function = segment_free;
        pbuf *p = pbuf_alloced_custom(PBUF_RAW, size, PBUF_REF, &segment->p, segment->data.data(), size);
        REQUIRE(p != nullptr);
        return conn.recv(conn.arg, &conn, p, ERR_OK);
    }

    // Sends segments as long as the TCP window (what's not recved yet) allows.
    void fill_window(size_t segment_size) {
        while (sent.size() - conn.recved + segment_size <= TCP_WND) {
            INFO("Segment " << segments.size());
            // Refusing in-window data makes lwip drop the following segments.
            REQUIRE(send(segment_size) == ERR_OK);
        }
    }

    int send_eof() {
        return conn.recv(conn.arg, &conn, nullptr, ERR_OK);
    }
};

} // namespace

TEST_CASE("Upload takes a full window of small segments") {
    // The firmware has an 8 KiB window and 512 B segments are common. The
    // window is smaller in the tests, so scale the segments down to get the
    // same number of them.
    constexpr size_t segment_size = TCP_WND / 16;

    SECTION("File busy from the start") {
        Upload upload(TCP_WND, 0);
        upload.fill_window(segment_size);
        REQUIRE(upload.segments.size() == 16);
        REQUIRE(upload.conn.recved == 0);
        // The EOF fits in too.
        REQUIRE(upload.send_eof() == ERR_OK);

        upload.transfer.f.usb_ready();

        REQUIRE(upload.transfer.f.written == upload.sent);
        REQUIRE(upload.conn.recved == TCP_WND);
        REQUIRE(upload.transfer.done_result == splice::Result::Ok);
        REQUIRE(upload.conn.closed);
        REQUIRE(pbufs_freed == upload.segments.size());
    }

    SECTION("First segment partly written") {
        // The window reopens by what's written, but the segment stays in the
        // queue until it's written whole.
        Upload upload(10 * TCP_WND, 30);
        upload.fill_window(segment_size);
        REQUIRE(upload.conn.recved == 30);

        upload.transfer.f.usb_ready();

        REQUIRE(upload.transfer.f.written == upload.sent);
        REQUIRE(upload.conn.recved == upload.sent.size());
        REQUIRE(pbufs_freed == upload.segments.size());

        REQUIRE(upload.send_eof() == ERR_OK);
        REQUIRE(upload.transfer.done_result == splice::Result::ClosedByClient);
        REQUIRE(upload.conn.closed);
    }
}

TEST_CASE("Upload waits for the file to be prepared") {
    // The file can be created asynchronously (off the tcpip thread). Until it's
    // ready, the data must wait in the queue: nothing refused (lwip would drop
    // the following segments), nothing acknowledged to the window (so the
    // client sees a closing window, not loss).
    constexpr size_t segment_size = TCP_WND / 16;
    using Readiness = splice::Transfer::Readiness;

    SECTION("Ready later") {
        Upload upload(TCP_WND, 0, Readiness::Waiting);
        upload.transfer.f.budget = std::numeric_limits<size_t>::max();
        upload.fill_window(segment_size);
        REQUIRE(upload.segments.size() == 16);
        REQUIRE(upload.conn.recved == 0);
        REQUIRE(upload.transfer.f.written.empty());

        upload.transfer.readiness = Readiness::Ready;
        upload.server->kick_transfer();

        // All the expected data arrived, so the transfer finishes right away.
        REQUIRE(upload.transfer.f.written == upload.sent);
        REQUIRE(upload.conn.recved == TCP_WND);
        REQUIRE(upload.transfer.done_result == splice::Result::Ok);
        REQUIRE(upload.conn.closed);
        REQUIRE(pbufs_freed == upload.segments.size());
    }

    SECTION("Preparation fails") {
        Upload upload(TCP_WND, 0, Readiness::Waiting);
        upload.fill_window(segment_size);

        upload.transfer.readiness = Readiness::Failed;
        upload.server->kick_transfer();

        // Nothing written, done() got to report the failure, and the queued
        // data went back to the window (so closing doesn't turn into a RST
        // while the client still sends).
        REQUIRE(upload.transfer.f.written.empty());
        REQUIRE(upload.transfer.done_result.has_value());
        REQUIRE(upload.conn.recved == upload.sent.size());
    }

    SECTION("Given up while waiting") {
        Upload upload(TCP_WND, 0, Readiness::Waiting);
        upload.fill_window(segment_size);
        // The connection breaks (lwip calls the error callback, the pcb is
        // already gone).
        REQUIRE(upload.conn.err != nullptr);
        upload.conn.err(upload.conn.arg, ERR_RST);
        REQUIRE(upload.transfer.aborted);
        REQUIRE(pbufs_freed == 16);
    }
}

TEST_CASE("Upload waits for the file to be finished") {
    // The end of the upload (sync, close, rename) can run off the tcpip thread;
    // the response (done()) must wait for it.
    constexpr size_t segment_size = TCP_WND / 16;
    Upload upload(TCP_WND, std::numeric_limits<size_t>::max());
    upload.transfer.finish_async = true;
    upload.transfer.finished = splice::Transfer::Readiness::Waiting;
    // Exactly the expected data (fill_window would go on: the window stays
    // open once the transfer has all it wants).
    for (size_t i = 0; i < 16; i++) {
        REQUIRE(upload.send(segment_size) == ERR_OK);
    }

    REQUIRE(upload.transfer.f.written == upload.sent);
    REQUIRE(upload.transfer.finish_started);
    REQUIRE_FALSE(upload.transfer.done_result.has_value());
    REQUIRE_FALSE(upload.conn.closed);

    upload.transfer.finished = splice::Transfer::Readiness::Ready;
    upload.server->kick_transfer();
    REQUIRE(upload.transfer.done_result == splice::Result::Ok);
    REQUIRE(upload.conn.closed);
}
