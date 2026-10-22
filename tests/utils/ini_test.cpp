#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <string>

extern "C" {
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/ini.h>
#include <86box/plat.h>

wchar_t *trim_w(wchar_t *str);
}

#ifdef _WIN32
#    include <process.h>
#    include <windows.h>
#    define getpid _getpid
#else
#    include <strings.h>
#    include <unistd.h>
#endif

// Host stubs for what ini.c pulls in from the platform layer.
extern "C" {
FILE *
plat_fopen(const char *path, const char *mode)
{
    return fopen(path, mode);
}

FILE *
rom_fopen(const char *fn, char *mode)
{
    return fopen(fn, mode);
}

// ini_strip_quotes() trims both copies of the value; ini.c only has trim_w.
char *
trim(char *str)
{
    char *start = str;
    while (*start == ' ' || *start == '\t')
        start++;
    size_t len = strlen(start);
    while (len > 0 && (start[len - 1] == ' ' || start[len - 1] == '\t'))
        len--;
    memmove(str, start, len);
    str[len] = '\0';
    return str;
}

#ifndef _WIN32
int
stricmp(const char *s1, const char *s2)
{
    return strcasecmp(s1, s2);
}
#endif

#ifdef _WIN32
// As in qt_platform.cpp: -1 on failure.
size_t
mbstoc16s(uint16_t dst[], const char src[], int len)
{
    size_t ret = MultiByteToWideChar(CP_UTF8, 0, src, -1, reinterpret_cast<LPWSTR>(dst), len);
    return ret ? ret : (size_t) -1;
}

size_t
c16stombs(char dst[], const uint16_t src[], int len)
{
    size_t ret = WideCharToMultiByte(CP_UTF8, 0, reinterpret_cast<LPCWCH>(src), -1, dst, len, NULL, NULL);
    return ret ? ret : (size_t) -1;
}
#endif
}

namespace {

class IniTest : public ::testing::Test {
public:
    std::filesystem::path path;

    void
    SetUp() override
    {
        // ctest runs each test in its own process, possibly in parallel, so
        // the name must differ between processes, not just within one.
        const auto *info = ::testing::UnitTest::GetInstance()->current_test_info();
        path = std::filesystem::temp_directory_path()
            / ("86box_ini_test_" + std::to_string(getpid()) + "_" + info->test_suite_name()
               + "_" + info->name() + ".cfg");
    }

    void
    TearDown() override
    {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }

    void
    write_file(const std::string &text)
    {
        std::ofstream out(path, std::ios::binary);
        out << text;
    }

    ini_t
    read()
    {
        return ini_read(path.string().c_str());
    }

    // Write ini to disk, close it, and read it back.
    ini_t
    round_trip(ini_t ini)
    {
        ini_write(ini, path.string().c_str());
        ini_close(ini);
        return read();
    }
};

TEST_F(IniTest, LongSectionNameIsTruncatedSafely)
{
    std::string name(300, 'S');
    write_file("[" + name + "]\nkey = value\n[after]\nok = 1\n");
    ini_t ini = read();
    ASSERT_NE(ini, nullptr);

    EXPECT_NE(ini_find_section(ini, name.substr(0, 127).c_str()), nullptr);
    EXPECT_EQ(ini_section_get_int(ini_find_section(ini, "after"), "ok", 0), 1);
    ini_close(ini);
}

TEST_F(IniTest, LongEntryNameIsTruncatedSafely)
{
    std::string name(300, 'E');
    write_file("[s]\n" + name + " = value\nok = 1\n");
    ini_t ini = read();
    ASSERT_NE(ini, nullptr);

    ini_section_t sec = ini_find_section(ini, "s");
    ASSERT_NE(sec, nullptr);
    EXPECT_STREQ(ini_section_get_string(sec, name.substr(0, 127).c_str(), nullptr), "value");
    EXPECT_EQ(ini_section_get_int(sec, "ok", 0), 1);
    ini_close(ini);
}

TEST_F(IniTest, LongStringValueIsTruncatedSafely)
{
    std::string   value(600, 'v');
    ini_t         ini = ini_new();
    ini_section_t sec = ini_find_or_create_section(ini, "s");
    ini_section_set_string(sec, "key", value.c_str());
    ini_section_set_int(sec, "ok", 1);

    EXPECT_EQ(ini_section_get_string(sec, "key", nullptr), value.substr(0, 511));
    EXPECT_EQ(wcslen(ini_section_get_wstring(sec, "key", nullptr)), 511u);

    ini = round_trip(ini);
    ASSERT_NE(ini, nullptr);
    sec = ini_find_section(ini, "s");
    ASSERT_NE(sec, nullptr);
    EXPECT_EQ(ini_section_get_string(sec, "key", nullptr), value.substr(0, 511));
    EXPECT_EQ(ini_section_get_int(sec, "ok", 0), 1);
    ini_close(ini);
}

TEST_F(IniTest, LongWstringValueIsTruncatedSafely)
{
    std::wstring  value(600, L'w');
    ini_t         ini = ini_new();
    ini_section_t sec = ini_find_or_create_section(ini, "s");
    ini_section_set_wstring(sec, "key", value.data());

    EXPECT_EQ(ini_section_get_wstring(sec, "key", nullptr), value.substr(0, 511));
    EXPECT_EQ(ini_section_get_string(sec, "key", nullptr), std::string(511, 'w'));
    ini_close(ini);
}

TEST_F(IniTest, ShortWstringValueIsCopiedExactly)
{
    // Before the fix, the setter always copied 512 wide characters from val.
    wchar_t       value[] = L"abc";
    ini_t         ini     = ini_new();
    ini_section_t sec     = ini_find_or_create_section(ini, "s");
    ini_section_set_wstring(sec, "key", value);

    EXPECT_STREQ(ini_section_get_wstring(sec, "key", nullptr), L"abc");
    EXPECT_STREQ(ini_section_get_string(sec, "key", nullptr), "abc");
    ini_close(ini);
}

} // namespace
