#include <doctest/doctest.h>

#include <string>

#include "core/ini.hpp"

using trace::IniFile;

TEST_CASE("IniFile round trips sections and keys in order") {
    IniFile ini;
    ini.set("general", "theme", "dark");
    ini.set("view", "grid", "true");
    ini.set("general", "scale", "125");

    const std::string text = ini.serialize();
    CHECK(text == "[general]\ntheme = dark\nscale = 125\n\n[view]\ngrid = true\n");

    const IniFile back = IniFile::parse(text);
    REQUIRE(back.get("general", "scale") != nullptr);
    CHECK(*back.get("general", "scale") == "125");
    CHECK(*back.get("view", "grid") == "true");
    CHECK(back.serialize() == text);
}

TEST_CASE("IniFile skips comments, blank lines and junk") {
    const IniFile ini = IniFile::parse("; comment\n# another\n\n[a]\njunk line\n=novalue\nkey = value\n[broken\nx=1\n");
    CHECK(*ini.get("a", "key") == "value");
    CHECK(ini.get("a", "junk line") == nullptr);
    CHECK(ini.get("a", "") == nullptr);
    // "[broken" has no closing bracket, so it is junk and x stays in section a.
    CHECK(*ini.get("a", "x") == "1");
}

TEST_CASE("IniFile handles CRLF, a BOM and whitespace around '='") {
    const IniFile ini = IniFile::parse("\xEF\xBB\xBF[ s ]\r\n  k  =   spaced value  \r\nother=1\r\n");
    REQUIRE(ini.get("s", "k") != nullptr);
    CHECK(*ini.get("s", "k") == "spaced value");
    CHECK(*ini.get("s", "other") == "1");
}

TEST_CASE("IniFile keeps the last of duplicate keys and merges repeated sections") {
    const IniFile ini = IniFile::parse("[a]\nk=1\nk=2\n[b]\nz=0\n[a]\nm=3\n");
    CHECK(*ini.get("a", "k") == "2");
    CHECK(*ini.get("a", "m") == "3");
    CHECK(ini.sections().size() == 2);
    CHECK(ini.section("a")->entries.size() == 2);
}

TEST_CASE("IniFile keeps keys before any header in the unnamed section") {
    IniFile ini = IniFile::parse("top=1\n[a]\nk=2\n");
    CHECK(*ini.get("", "top") == "1");
    ini.set("", "later", "x"); // still serialized before the first header
    const std::string text = ini.serialize();
    CHECK(text.rfind("top = 1\nlater = x\n", 0) == 0);
}

TEST_CASE("IniFile values never span lines") {
    IniFile ini;
    ini.set("a", "k", "one\ntwo\r\nthree");
    const IniFile back = IniFile::parse(ini.serialize());
    CHECK(*back.get("a", "k") == "one two  three");
}

TEST_CASE("IniFile tolerates empty and garbage input") {
    CHECK(IniFile::parse("").sections().empty());
    CHECK(IniFile::parse("\n\n\n").serialize().empty());
    CHECK(IniFile::parse(std::string("\0\0\0=\0", 5)).get("", "") == nullptr);
    CHECK(IniFile::parse("[a]").section("a") != nullptr);
}

TEST_CASE("parseInt enforces the range and rejects junk") {
    using trace::parseInt;
    CHECK(parseInt("42", 0, 100) == 42);
    CHECK(parseInt("  7 ", 0, 100) == 7);
    CHECK(parseInt("-3", -5, 5) == -3);
    CHECK_FALSE(parseInt("101", 0, 100).has_value());
    CHECK_FALSE(parseInt("-1", 0, 100).has_value());
    CHECK_FALSE(parseInt("12abc", 0, 100).has_value());
    CHECK_FALSE(parseInt("", 0, 100).has_value());
    CHECK_FALSE(parseInt("99999999999999999999", 0, 100).has_value());
    CHECK_FALSE(parseInt("1.5", 0, 100).has_value());
}

TEST_CASE("parseBool accepts the usual spellings") {
    using trace::parseBool;
    for (const char* s : {"true", "TRUE", "Yes", "on", "1", " true "})
        CHECK(parseBool(s) == true);
    for (const char* s : {"false", "False", "NO", "off", "0"})
        CHECK(parseBool(s) == false);
    CHECK_FALSE(parseBool("").has_value());
    CHECK_FALSE(parseBool("maybe").has_value());
    CHECK_FALSE(parseBool("2").has_value());
}

TEST_CASE("parseFloat enforces the range and rejects non-finite values") {
    using trace::parseFloat;
    REQUIRE(parseFloat("1.5", 0.0f, 2.0f).has_value());
    CHECK(*parseFloat("1.5", 0.0f, 2.0f) == doctest::Approx(1.5f));
    REQUIRE(parseFloat(" -0.25 ", -1.0f, 1.0f).has_value());
    CHECK(*parseFloat(" -0.25 ", -1.0f, 1.0f) == doctest::Approx(-0.25f));
    CHECK_FALSE(parseFloat("2.5", 0.0f, 2.0f).has_value());
    CHECK_FALSE(parseFloat("nan", 0.0f, 2.0f).has_value());
    CHECK_FALSE(parseFloat("inf", 0.0f, 2.0f).has_value());
    CHECK_FALSE(parseFloat("1.0x", 0.0f, 2.0f).has_value());
    CHECK_FALSE(parseFloat("", 0.0f, 2.0f).has_value());
}
