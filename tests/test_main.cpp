#include "test_harness.hpp"

#include <cstdio>
#include <stdexcept>

namespace isaec::test {

std::vector<Case>& registry() {
    static std::vector<Case> cases;
    return cases;
}

Registrar::Registrar(std::string name, std::function<void()> body) {
    registry().push_back(Case{std::move(name), std::move(body)});
}

void check(bool ok, const char* expr, const char* file, int line) {
    if (!ok) {
        std::string message = std::string("CHECK failed: ") + expr + "  at " + file + ":" +
                              std::to_string(line);
        throw std::runtime_error(message);
    }
}

void fail_now(const std::string& message) {
    throw std::runtime_error(message);
}

int run_all() {
    int failed = 0;
    for (const Case& item : registry()) {
        try {
            item.body();
            std::printf("ok    %s\n", item.name.c_str());
        } catch (const std::exception& error) {
            std::printf("FAIL  %s\n        %s\n", item.name.c_str(), error.what());
            ++failed;
        }
    }
    std::printf("\n%zu cases, %d failed\n", registry().size(), failed);
    return failed == 0 ? 0 : 1;
}

}  // namespace isaec::test

int main() {
    return isaec::test::run_all();
}
