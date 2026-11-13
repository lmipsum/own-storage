// SPDX-License-Identifier: GPL-3.0-or-later
#include "Check.h"

#include <cstdio>

namespace
{
int failures = 0;
} // namespace

std::vector<TestCase> &AllTests()
{
    static std::vector<TestCase> tests;
    return tests;
}

RegisterTest::RegisterTest(const char *name, const std::function<void()> &run)
{
    AllTests().push_back(TestCase{name, run});
}

void ReportFailure(const char *file, int line, const std::string &what)
{
    std::printf("  %s:%d: %s\n", file, line, what.c_str());
    ++failures;
}

/** Runs every test and prints which failed; the exit code is the number of failed checks. */
int main()
{
    for (const TestCase &test : AllTests())
    {
        const int before = failures;
        test.run();
        std::printf("%s %s\n", failures == before ? "ok  " : "FAIL", test.name.c_str());
    }
    std::printf("%d failed checks\n", failures);
    return failures == 0 ? 0 : 1;
}
