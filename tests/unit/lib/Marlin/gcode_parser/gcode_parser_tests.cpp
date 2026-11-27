#include <parser.h>

#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <memory>
#include <string_view>

namespace {

/// A copy of a G-code line in a heap buffer of the exact size, as the printer gives it to the
/// parser. The parser changes the line in place, and AddressSanitizer finds a read past the end.
class Line {
public:
    explicit Line(std::string_view text)
        : buffer(new char[text.size() + 1]) {
        memcpy(buffer.get(), text.data(), text.size());
        buffer[text.size()] = '\0';
        parser.parse(buffer.get());
    }

private:
    std::unique_ptr<char[]> buffer;
};

} // namespace

TEST_CASE("A command with no text argument has a null string_arg") {
    // M340, M997, M862.3 P1, M331, M332 and M334 read string_arg. They must check it for null.
    for (const char *text : { "M340", "M997", "M862.3 P1", "M331", "M334", "M0" }) {
        CAPTURE(text);
        Line line(text);
        CHECK(parser.string_arg == nullptr);
    }
}

TEST_CASE("A command with a text argument has string_arg at the text") {
    Line line("M340 192.168.1.2 8514");
    REQUIRE(parser.string_arg != nullptr);
    CHECK(std::string_view(parser.string_arg) == "192.168.1.2 8514");
}

TEST_CASE("M23, M28, M30, M117, M118, M928 and M973 always have a string_arg") {
    // The parser sets string_arg to the rest of the line for these codes, also if it is empty.
    for (const char *text : { "M23", "M28", "M30", "M117", "M118", "M928", "M973" }) {
        CAPTURE(text);
        Line line(text);
        REQUIRE(parser.string_arg != nullptr);
        CHECK(*parser.string_arg == '\0');
    }
    Line line("M117 Hello world");
    CHECK(std::string_view(parser.string_arg) == "Hello world");
}

TEST_CASE("A letter with no value is seen, but has no value") {
    Line line("G1 X F3000");
    CHECK(parser.seen('X'));
    CHECK_FALSE(parser.has_value());
    CHECK_FALSE(parser.seenval('X'));
    // The accessors give 0 for a letter with no value. A handler that tests seen() and reads the
    // value gets 0, for example a divisor or a limit of 0.
    CHECK(parser.value_float() == 0);
    CHECK(parser.value_long() == 0);
    // The *val() accessors with a default use seenval(), so they give the default.
    CHECK(parser.floatval('X', 5) == 5);
    CHECK(parser.seenval('F'));
    CHECK(parser.value_float() == 3000);
}

TEST_CASE("A sign with no digits is not a value") {
    Line line("G1 X- Y+ Z.");
    CHECK(parser.seen('X'));
    CHECK_FALSE(parser.has_value());
    CHECK(parser.seen('Y'));
    CHECK_FALSE(parser.has_value());
    CHECK(parser.seen('Z'));
    CHECK_FALSE(parser.has_value());
}

TEST_CASE("An absent letter is not seen and the accessors give the default") {
    Line line("M572 S0.05");
    CHECK_FALSE(parser.seen('W'));
    CHECK(parser.floatval('W', 0.04f) == 0.04f);
    CHECK(parser.intval('W', -1) == -1);
}

TEST_CASE("A zero value is a value") {
    // The handler, not the parser, must reject a 0 that it uses as a divisor or a loop step.
    Line line("M572 W0");
    REQUIRE(parser.seenval('W'));
    CHECK(parser.value_float() == 0);
}

TEST_CASE("The value accessors convert as the handlers expect") {
    SECTION("value_byte() clamps to 0..255") {
        Line line("M350 X300 Y-1");
        REQUIRE(parser.seenval('X'));
        CHECK(parser.value_byte() == 255);
        REQUIRE(parser.seenval('Y'));
        CHECK(parser.value_byte() == 0);
    }
    SECTION("value_int() keeps only 16 bits") {
        Line line("M486 S70000");
        REQUIRE(parser.seenval('S'));
        CHECK(parser.value_int() == 70000 - 65536);
    }
    SECTION("value_float() ignores an exponent") {
        // E is a parameter letter, so the parser does not read scientific notation.
        Line line("M958 F1e3");
        REQUIRE(parser.seenval('F'));
        CHECK(parser.value_float() == 1);
    }
}

TEST_CASE("The parser reads the command letter, the number and the subcode") {
    Line line("N12 M862.3 P\"COREONEL\"*42");
    CHECK(parser.command_letter == 'M');
    CHECK(parser.codenum == 862);
    CHECK(parser.subcode == 3);
    REQUIRE(parser.string_arg != nullptr);
    CHECK(std::string_view(parser.string_arg) == "P\"COREONEL\"");
}

TEST_CASE("A line with no command number is not a command") {
    for (const char *text : { "M", "G X1", ";comment", "X10" }) {
        CAPTURE(text);
        Line line(text);
        CHECK(parser.command_letter == '?');
    }
}

TEST_CASE("The special MMU tool commands have a string_arg") {
    Line line("T?");
    CHECK(parser.command_letter == 'T');
    REQUIRE(parser.string_arg != nullptr);
    CHECK(std::string_view(parser.string_arg) == "?");
}
