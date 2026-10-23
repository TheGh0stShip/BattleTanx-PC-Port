#pragma once

#include <cmath>
#include <functional>
#include <iostream>
#include <string_view>
#include <vector>

namespace runtime_test {

struct Case {
    std::string_view name;
    std::function<bool()> body;
};

inline std::vector<Case>& registry() {
    static std::vector<Case> cases;
    return cases;
}

struct Register {
    Register(std::string_view name,std::function<bool()> body) {
        registry().push_back(Case{name,std::move(body)});
    }
};

inline bool near(double a,double b,double epsilon=1e-4) { return std::fabs(a-b)<=epsilon; }

inline int run_all() {
    int failures=0;
    for (const Case& test : registry()) {
        const bool passed=test.body();
        std::cout << (passed ? "PASS " : "FAIL ") << test.name << '\n';
        if (!passed) ++failures;
    }
    std::cout << registry().size()-static_cast<std::size_t>(failures) << '/'
              << registry().size() << " passed\n";
    return failures==0 ? 0 : 1;
}

} // namespace runtime_test

#define RT_CONCAT_INNER(a,b) a##b
#define RT_CONCAT(a,b) RT_CONCAT_INNER(a,b)
#define RUNTIME_TEST(name) \
    static bool name(); \
    static const runtime_test::Register RT_CONCAT(name,_registration){#name,&name}; \
    static bool name()

#define REQUIRE(expression) \
    do { \
        if (!(expression)) { \
            std::cerr << "check failed at " << __FILE__ << ':' << __LINE__ \
                      << ": " #expression "\n"; \
            return false; \
        } \
    } while (false)

#define RUNTIME_TEST_MAIN() int main() { return runtime_test::run_all(); }
