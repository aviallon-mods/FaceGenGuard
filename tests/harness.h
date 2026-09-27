#pragma once

// Minimal, dependency-free test harness (off-game).
//
// It deliberately does not use a framework: the whole point of tests/ is that
// it builds and runs on Linux and Windows with nothing but a compiler, so the
// handler's decision core is verified on TWO toolchains rather than assumed.
//
// Two rules, both learned the hard way elsewhere:
//
//  * A failure is counted, not just printed, and the process exits non-zero.
//    A check that only prints is a check that gets ignored.
//  * Every test that mutates state asserts the FINAL state, including the
//    counters. "It suppressed the fault" is not a check unless the count and
//    the cap that govern it are asserted too.
//
// Plus the heap probe: the resilience handler must allocate NOTHING, so the
// harness replaces the global operator new/delete overloads with counting
// wrappers (main.cpp) and exposes the count here. A test that claims "zero
// allocations" must also prove the probe can actually see an allocation --
// a counter that cannot move cannot fail.

#include <concepts>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace hstest
{
	struct TestCase
	{
		const char* name;
		void (*fn)();
	};

	[[nodiscard]] std::vector<TestCase>& Registry();
	[[nodiscard]] int&                   CheckCount();
	[[nodiscard]] int&                   FailureCount();

	void Fail(const char* a_file, int a_line, const std::string& a_message);
	void Note(const std::string& a_message);

	// Runs every registered test (or only those whose name contains a_filter).
	// Returns the process exit code: 0 only when every check passed.
	int RunAll(const char* a_filter);

	// Process-wide heap counters maintained by the replaced operator
	// new/delete overloads in main.cpp.
	[[nodiscard]] long long HeapAllocations();
	[[nodiscard]] long long HeapFrees();

	inline std::string ToString(bool a_value)
	{
		return a_value ? "true" : "false";
	}

	template <class T>
		requires(std::integral<T> && !std::same_as<T, bool>)
	inline std::string ToString(T a_value)
	{
		return std::to_string(a_value);
	}

	template <class T>
		requires(std::is_enum_v<T>)
	inline std::string ToString(T a_value)
	{
		return std::to_string(static_cast<std::underlying_type_t<T>>(a_value));
	}

	inline std::string ToString(const std::string& a_value)
	{
		return a_value;
	}

	inline std::string ToString(std::string_view a_value)
	{
		return std::string(a_value);
	}

	inline std::string ToString(const char* a_value)
	{
		return a_value == nullptr ? "(null)" : std::string(a_value);
	}

	template <class A, class B>
	void CheckEq(const char* a_file, int a_line, const A& a_actual, const B& a_expected, const char* a_actualExpr, const char* a_expectedExpr)
	{
		++CheckCount();
		if (!(a_actual == a_expected)) {
			Fail(a_file, a_line,
				std::string(a_actualExpr) + " == " + a_expectedExpr +
					" (got " + ToString(a_actual) + ", expected " + ToString(a_expected) + ")");
		}
	}

	template <class A, class B>
	void CheckNe(const char* a_file, int a_line, const A& a_actual, const B& a_unexpected, const char* a_actualExpr, const char* a_unexpectedExpr)
	{
		++CheckCount();
		if (a_actual == a_unexpected) {
			Fail(a_file, a_line,
				std::string(a_actualExpr) + " != " + a_unexpectedExpr +
					" (both are " + ToString(a_actual) + ")");
		}
	}
}

// The declaration, the registration and the definition must all name the SAME
// function (see the harness history of silently-never-run tests).
#define HS_TEST(name)                                              \
	void name();                                                   \
	namespace                                                      \
	{                                                              \
		const bool name##_registered = [] {                        \
			::hstest::Registry().push_back({ #name, &name });      \
			return true;                                           \
		}();                                                       \
	}                                                              \
	void name()

#define HS_CHECK(expr)                                             \
	do {                                                           \
		++::hstest::CheckCount();                                  \
		if (!(expr)) {                                             \
			::hstest::Fail(__FILE__, __LINE__, "expected: " #expr); \
		}                                                          \
	} while (false)

#define HS_CHECK_EQ(actual, expected) \
	::hstest::CheckEq(__FILE__, __LINE__, (actual), (expected), #actual, #expected)

#define HS_CHECK_NE(actual, unexpected) \
	::hstest::CheckNe(__FILE__, __LINE__, (actual), (unexpected), #actual, #unexpected)

// Like HS_CHECK, but the failure message carries extra context. Only the
// failing path builds the string.
#define HS_CHECK_MSG(expr, message)                                                        \
	do {                                                                                    \
		++::hstest::CheckCount();                                                            \
		if (!(expr)) {                                                                       \
			::hstest::Fail(__FILE__, __LINE__, std::string{ #expr } + " -- " + (message));    \
		}                                                                                    \
	} while (false)
