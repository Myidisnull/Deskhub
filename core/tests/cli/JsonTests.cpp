#include "Tests.h"
#include "support/TestSupport.h"

#include "deskhub/cli/Json.h"

#include <cstdio>
#include <string>

using namespace deskhub;

namespace {

void TestEscaping() {
    std::printf("[json] quotes, backslashes and control bytes come out escaped...\n");
    Check(cli::JsonEscape("plain") == "plain", "ordinary text is untouched");
    Check(cli::JsonEscape("say \"hi\"") == "say \\\"hi\\\"", "quotes are escaped");
    Check(cli::JsonEscape("C:\\path") == "C:\\\\path", "backslashes are escaped");
    Check(cli::JsonEscape("a\nb") == "a\\nb", "newline");
    Check(cli::JsonEscape("a\rb") == "a\\rb", "carriage return");
    Check(cli::JsonEscape("a\tb") == "a\\tb", "tab");
    Check(cli::JsonEscape(std::string("a\x01"
                                      "b")) == "a\\u0001b",
        "other control bytes");
    Check(cli::JsonEscape(std::string("a\x7F"
                                      "b")) == "a\\u007fb",
        "delete");
    Check(cli::JsonEscape(std::string(1, '\0')) == "\\u0000", "a NUL byte survives");
}

void TestUtf8() {
    std::printf("[json] valid UTF-8 passes through, invalid bytes cannot escape...\n");
    Check(cli::JsonEscape("m\xC3\xA0n h\xC3\xACnh") == "m\xC3\xA0n h\xC3\xACnh",
        "two-byte sequences pass through");
    Check(cli::JsonEscape("\xE2\x80\xA6") == "\xE2\x80\xA6", "three-byte sequences pass through");
    Check(cli::JsonEscape("\xF0\x9F\x96\xA5") == "\xF0\x9F\x96\xA5",
        "four-byte sequences pass through");

    const std::string replacement = "\xEF\xBF\xBD";
    Check(cli::JsonEscape("\xFF") == replacement, "a byte no sequence starts with");
    Check(cli::JsonEscape("\xC3") == replacement, "a truncated sequence");
    Check(cli::JsonEscape("\xC0\xAF") == replacement + replacement, "an overlong encoding");
    Check(cli::JsonEscape("\xED\xA0\x80") == replacement + replacement + replacement,
        "a surrogate half");
    Check(cli::JsonEscape("\xE2\x80") == replacement + replacement, "a clipped ellipsis");
    Check(cli::JsonEscape("a\xFF"
                          "b") == "a" + replacement + "b",
        "junk between good text");

    Check(cli::JsonEscape("\xC1\xBF") == replacement + replacement, "0xC1 starts nothing");
    Check(cli::JsonEscape("\xC2\x80") == "\xC2\x80", "the shortest two-byte sequence");
    Check(cli::JsonEscape("\xDF\xBF") == "\xDF\xBF", "the longest two-byte sequence");
    Check(cli::JsonEscape("\xE0\x9F\x80") == replacement + replacement + replacement,
        "an overlong three-byte sequence");
    Check(cli::JsonEscape("\xE0\xA0\x80") == "\xE0\xA0\x80", "the shortest three-byte sequence");
    Check(cli::JsonEscape("\xED\x9F\xBF") == "\xED\x9F\xBF", "the last one before the surrogates");
    Check(cli::JsonEscape("\xF0\x8F\xBF\xBF") ==
              replacement + replacement + replacement + replacement,
        "an overlong four-byte sequence");
    Check(cli::JsonEscape("\xF4\x8F\xBF\xBF") == "\xF4\x8F\xBF\xBF", "the last code point there is");
    Check(cli::JsonEscape("\xF4\x90\x80\x80") ==
              replacement + replacement + replacement + replacement,
        "one past the last code point");
    Check(cli::JsonEscape("\xF5\x80\x80\x80") ==
              replacement + replacement + replacement + replacement,
        "0xF5 starts nothing");
    Check(cli::JsonEscape("\xE2\x28\xA1") == replacement + "(" + replacement,
        "a bad continuation byte is not swallowed");
    Check(cli::JsonEscape("\xF0\x9F\x96") == replacement + replacement + replacement,
        "a four-byte sequence cut short at the end");
}

void TestWriterShapes() {
    std::printf("[json] objects, arrays and nesting come out well formed...\n");
    cli::JsonWriter empty;
    empty.ObjectBegin();
    empty.ObjectEnd();
    Check(empty.Text() == "{}", "an empty object");

    cli::JsonWriter flat;
    flat.ObjectBegin();
    flat.Field("name", "Display 1");
    flat.Field("width", int64_t(1920));
    flat.Field("shared", true);
    flat.Field("locked", false);
    flat.ObjectEnd();
    Check(flat.Text() == "{\"name\":\"Display 1\",\"width\":1920,\"shared\":true,\"locked\":false}",
        "fields of every type");

    cli::JsonWriter nested;
    nested.ObjectBegin();
    nested.FieldBegin("hosts");
    nested.ArrayBegin();
    nested.ObjectBegin();
    nested.Field("addr", "192.168.1.10:47777");
    nested.ObjectEnd();
    nested.ObjectBegin();
    nested.Field("addr", "192.168.1.11:47777");
    nested.ObjectEnd();
    nested.ArrayEnd();
    nested.Field("found", int64_t(2));
    nested.ObjectEnd();
    Check(nested.Text() ==
              "{\"hosts\":[{\"addr\":\"192.168.1.10:47777\"},{\"addr\":\"192.168.1.11:47777\"}],\"found\":2}",
        "an array of objects inside an object");

    cli::JsonWriter values;
    values.ArrayBegin();
    values.Value("one");
    values.Value(int64_t(2));
    values.ArrayEnd();
    Check(values.Text() == "[\"one\",2]", "bare values in an array");

    cli::JsonWriter escaped;
    escaped.ObjectBegin();
    escaped.Field("say \"what\"", "line\nbreak");
    escaped.ObjectEnd();
    Check(escaped.Text() == "{\"say \\\"what\\\"\":\"line\\nbreak\"}", "keys are escaped too");

    cli::JsonWriter literals;
    literals.ObjectBegin();
    literals.Field("text", "yes");
    literals.Field("count", 3);
    literals.ObjectEnd();
    Check(literals.Text() == "{\"text\":\"yes\",\"count\":3}",
        "a string literal stays a string and a plain int stays a number");

    cli::JsonWriter nulls;
    nulls.ArrayBegin();
    nulls.Value(static_cast<const char*>(nullptr));
    nulls.Value("after");
    nulls.ArrayEnd();
    Check(nulls.Text() == "[\"\",\"after\"]", "a null pointer is an empty string, not a crash");

    cli::JsonWriter nullField;
    nullField.ObjectBegin();
    nullField.Field("name", static_cast<const char*>(nullptr));
    nullField.ObjectEnd();
    Check(nullField.Text() == "{\"name\":\"\"}", "and the same as a field");

    cli::JsonWriter emptyArray;
    emptyArray.ArrayBegin();
    emptyArray.ArrayEnd();
    Check(emptyArray.Text() == "[]", "an empty array");

    cli::JsonWriter negative;
    negative.ObjectBegin();
    negative.Field("rttMs", int64_t(-1));
    negative.ObjectEnd();
    Check(negative.Text() == "{\"rttMs\":-1}", "negative numbers");
}

}

void RunCliJsonTests() {
    TestEscaping();
    TestUtf8();
    TestWriterShapes();
}
