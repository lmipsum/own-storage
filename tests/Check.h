// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <functional>
#include <string>
#include <vector>

/** A test: its name and the code that runs its checks. */
struct TestCase
{
    std::string name;
    std::function<void()> run;
};

/** Every test, in the order the files register them. */
std::vector<TestCase> &AllTests();

/** Adds a test to AllTests() when the program starts. */
struct RegisterTest
{
    RegisterTest(const char *name, const std::function<void()> &run);
};

/** Reports a failed check and counts it. */
void ReportFailure(const char *file, int line, const std::string &what);

/** Defines a test called name. */
#define TEST(name)                                                                                 \
    static void name();                                                                            \
    static const RegisterTest name##Registered(#name, name);                                       \
    static void name()

/** Fails the running test, but lets it go on, when condition is false. */
#define CHECK(condition)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if (!(condition))                                                                          \
        {                                                                                          \
            ReportFailure(__FILE__, __LINE__, #condition);                                         \
        }                                                                                          \
    } while (false)

/** Fails the running test, but lets it go on, when actual differs from expected. */
#define CHECK_EQUAL(expected, actual)                                                              \
    do                                                                                             \
    {                                                                                              \
        if (!((expected) == (actual)))                                                             \
        {                                                                                          \
            ReportFailure(__FILE__, __LINE__, #actual " != " #expected);                           \
        }                                                                                          \
    } while (false)
