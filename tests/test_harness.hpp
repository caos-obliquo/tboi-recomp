#pragma once

#include <functional>
#include <string>
#include <vector>

namespace isaec::test {

struct Case {
    std::string name;
    std::function<void()> body;
};

std::vector<Case>& registry();

struct Registrar {
    Registrar(std::string name, std::function<void()> body);
};

int run_all();

void check(bool ok, const char* expr, const char* file, int line);

[[noreturn]] void fail_now(const std::string& message);

}  // namespace isaec::test

#define ISAEC_TEST(name)                                                  \
    static void name();                                                   \
    static const ::isaec::test::Registrar isaec_registrar_##name{#name, name}; \
    static void name()

#define CHECK(expr) ::isaec::test::check(static_cast<bool>(expr), #expr, __FILE__, __LINE__)

#define REQUIRE(expr)                                                          \
    do {                                                                       \
        if (!static_cast<bool>(expr)) {                                        \
            ::isaec::test::fail_now(std::string("REQUIRE failed: ") + #expr);  \
        }                                                                      \
    } while (false)
