// Off-game tests for FaceGenGuard's handler policy core: the whitelist RIP
// matcher (the guard's ENTIRE scope), the suppression counter and its fail-open
// cap (the handler must decline to suppress after the cap and count exactly),
// and the fixed-format log line built from fixed buffers with ZERO heap
// allocation. These are the exact functions the vectored exception handler
// calls, so a claim tested here is a claim about the handler.

#include "harness.h"

#include "Core/ResiliencePolicy.h"

#include <string>

using namespace hs;

namespace
{
	// The verified crash shape: `mov r15d,[rbp+0x40]` with rbp = 0x10 faults
	// reading 0x50. The other candidate registers hold values ABOVE the fault
	// address, so they cannot be the base of the faulting operand.
	const std::uint64_t kCrashValues[] = { 0x10, 0x7FF612AB0000, 0x51, 0x1000 };
	const char*         kCrashNames[] = { "rbp", "rbx", "rcx", "rdx" };

	ResilienceFaultLine SampleLine()
	{
		ResilienceFaultLine line;
		line.index = 3;
		line.rip = 0x7FF612AB34CD;
		line.ripRva = 0x12AB34CD;
		line.faultAddr = 0x50;
		line.callerRip = 0x7FF612AB9010;
		line.bad = PickBadPointer(line.faultAddr, kCrashValues, kCrashNames, 4);
		return line;
	}
}

// ---------------------------------------------------------------------------
// 1. The whitelist RIP matcher: inside, at the edges, outside, and null.
// ---------------------------------------------------------------------------

HS_TEST(resilience_whitelist_matches_only_inside_its_range)
{
	ResilienceWhitelist whitelist;
	HS_CHECK(whitelist.Parse("0x1000-0x1800"));
	HS_CHECK_EQ(whitelist.Size(), 1u);

	HS_CHECK(!whitelist.Contains(0xFFF));    // below the range: outside
	HS_CHECK(whitelist.Contains(0x1000));    // BEGIN is inclusive (edge)
	HS_CHECK(whitelist.Contains(0x1001));
	HS_CHECK(whitelist.Contains(0x1400));    // inside
	HS_CHECK(whitelist.Contains(0x17FF));    // last address inside (edge)
	HS_CHECK(!whitelist.Contains(0x1800));   // END is exclusive (edge)
	HS_CHECK(!whitelist.Contains(0x1801));
	HS_CHECK(!whitelist.Contains(0x0));
	HS_CHECK(!whitelist.Contains(0xFFFFFFFFFFFFFFFFull));
}

HS_TEST(resilience_whitelist_single_range_from_the_address_library_target)
{
	// The normal construction path: [entry, entry + span), computed at runtime.
	ResilienceWhitelist whitelist;
	whitelist.SetSingleRange(0x438000, 0x438000 + 0x800);
	HS_CHECK_EQ(whitelist.Size(), 1u);
	HS_CHECK(!whitelist.Contains(0x437FFF));
	HS_CHECK(whitelist.Contains(0x438000));
	HS_CHECK(whitelist.Contains(0x4387FF));
	HS_CHECK(!whitelist.Contains(0x438800));
	HS_CHECK(!whitelist.Contains(0x439000));

	// A reversed or empty range is rejected outright.
	ResilienceWhitelist bad;
	bad.SetSingleRange(0x2000, 0x1000);
	HS_CHECK_EQ(bad.Size(), 0u);
	HS_CHECK(!bad.Contains(0x1800));
	bad.SetSingleRange(0x1000, 0x1000);
	HS_CHECK_EQ(bad.Size(), 0u);
}

HS_TEST(resilience_whitelist_parses_multiple_ranges_and_separators)
{
	ResilienceWhitelist whitelist;
	HS_CHECK(whitelist.Parse("0x1000-0x2000, 3000-4000;0x5000-0x6000 \t0x7000-0x8000"));
	HS_CHECK_EQ(whitelist.Size(), 4u);
	HS_CHECK(whitelist.Contains(0x1000));
	HS_CHECK(whitelist.Contains(0x1FFF));
	HS_CHECK(!whitelist.Contains(0x2000));
	HS_CHECK(whitelist.Contains(0x3500));  // bare hex, no 0x prefix
	HS_CHECK(!whitelist.Contains(0x4000));
	HS_CHECK(whitelist.Contains(0x5ABC));
	HS_CHECK(whitelist.Contains(0x7000));
	HS_CHECK(!whitelist.Contains(0x8000));
	// The gaps between ranges are NOT covered.
	HS_CHECK(!whitelist.Contains(0x2800));
	HS_CHECK(!whitelist.Contains(0x6800));
}

HS_TEST(resilience_whitelist_rejects_malformed_entries_and_keeps_the_rest)
{
	ResilienceWhitelist whitelist;

	// Null and empty are a NULL whitelist: accepted as nothing, never matching.
	HS_CHECK(!whitelist.Parse(nullptr));
	HS_CHECK_EQ(whitelist.Size(), 0u);
	HS_CHECK(!whitelist.Contains(0x1000));
	HS_CHECK(!whitelist.Parse(""));
	HS_CHECK_EQ(whitelist.Size(), 0u);

	// Malformed tokens are rejected as a whole.
	HS_CHECK(!whitelist.Parse("garbage"));
	HS_CHECK(!whitelist.Parse("0x2000-0x1000"));   // reversed
	HS_CHECK(!whitelist.Parse("0x1000-0x1000"));   // empty range
	HS_CHECK(!whitelist.Parse("0x1000"));          // not a range at all
	HS_CHECK(!whitelist.Parse("-0x1000"));         // no begin
	HS_CHECK(!whitelist.Parse("0x10-0x20garbage"));  // trailing garbage poisons the token

	// ... and one bad token never poisons the good ones around it.
	HS_CHECK(whitelist.Parse("nonsense, 0x2000-0x1000, 0x3000-0x4000"));
	HS_CHECK_EQ(whitelist.Size(), 1u);
	HS_CHECK(whitelist.Contains(0x3500));
	HS_CHECK(!whitelist.Contains(0x1500));
	HS_CHECK(whitelist.Parse("0x10-0x20garbage 0x30-0x40"));
	HS_CHECK_EQ(whitelist.Size(), 1u);
	HS_CHECK(whitelist.Contains(0x35));
}

HS_TEST(resilience_whitelist_stops_at_max_ranges)
{
	ResilienceWhitelist whitelist;
	HS_CHECK(whitelist.Parse("0x0-0x10,0x10-0x20,0x20-0x30,0x30-0x40,0x40-0x50,"
							 "0x50-0x60,0x60-0x70,0x70-0x80,0x80-0x90,0x90-0xA0"));
	HS_CHECK_EQ(whitelist.Size(), kResilienceMaxRanges);
	HS_CHECK(whitelist.Contains(0x05));
	HS_CHECK(whitelist.Contains(0x75));   // 8th range kept
	HS_CHECK(!whitelist.Contains(0x85));  // 9th range dropped
	HS_CHECK(!whitelist.Contains(0x95));  // 10th range dropped
}

HS_TEST(resilience_whitelist_that_is_null_never_matches)
{
	ResilienceWhitelist whitelist;  // default-constructed: NULL, matches nothing
	HS_CHECK(!whitelist.Contains(0x0));
	HS_CHECK(!whitelist.Contains(0x1000));
	HS_CHECK(!whitelist.Contains(0xFFFFFFFFFFFFFFFFull));

	whitelist.SetSingleRange(0x1000, 0x1800);
	whitelist.Clear();
	HS_CHECK_EQ(whitelist.Size(), 0u);
	HS_CHECK(!whitelist.Contains(0x1400));
}

// ---------------------------------------------------------------------------
// 2. The suppression counter and the fail-open cap.
// ---------------------------------------------------------------------------

HS_TEST(resilience_suppression_counter_grants_up_to_the_cap_then_fails_open)
{
	SuppressionCounter counter;
	HS_CHECK_EQ(counter.Count(), 0u);

	for (std::uint32_t i = 1; i <= 3; ++i) {
		const auto claim = counter.TryClaim(3);
		HS_CHECK(claim.granted);
		HS_CHECK_EQ(claim.index, i);
		HS_CHECK_EQ(counter.Count(), i);
		HS_CHECK_EQ(counter.Declined(), 0u);
	}

	for (int attempt = 0; attempt < 5; ++attempt) {
		const auto claim = counter.TryClaim(3);
		HS_CHECK_MSG(!claim.granted, "after the cap the handler must decline to suppress");
		HS_CHECK_EQ(claim.index, 0u);
	}

	// The final state: exactly the cap was suppressed, every refusal counted.
	HS_CHECK_EQ(counter.Count(), 3u);
	HS_CHECK_EQ(counter.Declined(), 5u);

	counter.Reset();
	HS_CHECK_EQ(counter.Count(), 0u);
	HS_CHECK_EQ(counter.Declined(), 0u);
}

HS_TEST(resilience_suppression_counter_with_zero_cap_never_suppresses)
{
	SuppressionCounter counter;
	const auto         claim = counter.TryClaim(0);
	HS_CHECK(!claim.granted);
	HS_CHECK_EQ(counter.Count(), 0u);
	HS_CHECK_EQ(counter.Declined(), 1u);
}

HS_TEST(resilience_decision_consumes_nothing_outside_the_whitelist)
{
	ResilienceWhitelist whitelist;
	whitelist.SetSingleRange(0x1000, 0x1800);
	SuppressionCounter counter;

	const auto miss = DecideSuppression(whitelist, 0x2000, counter, 8);
	HS_CHECK(!miss.suppress);
	HS_CHECK(!miss.inWhitelist);
	HS_CHECK(!miss.capReached);
	HS_CHECK_EQ(miss.index, 0u);
	HS_CHECK_EQ(counter.Count(), 0u);
	HS_CHECK_EQ(counter.Declined(), 0u);

	// The edges go through the decision function too.
	HS_CHECK(DecideSuppression(whitelist, 0x1000, counter, 8).suppress);   // begin inclusive
	HS_CHECK(DecideSuppression(whitelist, 0x17FF, counter, 8).suppress);   // last inside
	HS_CHECK(!DecideSuppression(whitelist, 0x1800, counter, 8).suppress);  // end exclusive
	HS_CHECK(!DecideSuppression(whitelist, 0xFFF, counter, 8).suppress);   // just below
}

HS_TEST(resilience_decision_declines_after_the_cap_and_counts_correctly)
{
	ResilienceWhitelist whitelist;
	whitelist.SetSingleRange(0x1000, 0x1800);
	SuppressionCounter counter;

	constexpr std::uint32_t cap = 4;
	std::uint32_t           suppressed = 0;
	for (int i = 0; i < 10; ++i) {
		const auto decision = DecideSuppression(whitelist, 0x1086, counter, cap);
		HS_CHECK(decision.inWhitelist);
		if (decision.suppress) {
			++suppressed;
			HS_CHECK_EQ(decision.index, suppressed);
			HS_CHECK(!decision.capReached);
		} else {
			HS_CHECK_MSG(decision.capReached, "in-whitelist refusals must be the cap, nothing else");
			HS_CHECK_EQ(decision.index, 0u);
		}
	}
	HS_CHECK_EQ(suppressed, cap);
	HS_CHECK_EQ(counter.Count(), cap);
	HS_CHECK_EQ(counter.Declined(), 10u - cap);
}

HS_TEST(resilience_decision_with_a_null_whitelist_never_suppresses)
{
	ResilienceWhitelist whitelist;  // NULL
	SuppressionCounter  counter;
	const auto          decision = DecideSuppression(whitelist, 0x1086, counter, 8);
	HS_CHECK(!decision.suppress);
	HS_CHECK(!decision.inWhitelist);
	HS_CHECK_EQ(counter.Count(), 0u);
}

// ---------------------------------------------------------------------------
// 3. The log line: fixed format, fixed buffers, zero heap allocation.
// ---------------------------------------------------------------------------

HS_TEST(resilience_bad_pointer_is_the_base_register_of_the_faulting_operand)
{
	// The known crash: `mov r15d,[rbp+0x40]` with rbp = 0x10 faults at 0x50.
	const auto bad = PickBadPointer(0x50, kCrashValues, kCrashNames, 4);
	HS_CHECK(bad.fromRegister);
	HS_CHECK_EQ(std::string(bad.regName), std::string("rbp"));
	HS_CHECK_EQ(bad.value, 0x10u);
	HS_CHECK_EQ(bad.displacement, 0x40u);
}

HS_TEST(resilience_bad_pointer_nearest_base_wins_regardless_of_order)
{
	const std::uint64_t values[] = { 0x10, 0x30, 0x4F };
	const char*         names[] = { "rbp", "rbx", "rcx" };
	const auto          bad = PickBadPointer(0x50, values, names, 3);
	HS_CHECK(bad.fromRegister);
	HS_CHECK_EQ(std::string(bad.regName), std::string("rcx"));
	HS_CHECK_EQ(bad.value, 0x4Fu);
	HS_CHECK_EQ(bad.displacement, 0x1u);

	const std::uint64_t reversed[] = { 0x4F, 0x30, 0x10 };
	const char*         reversedNames[] = { "rcx", "rbx", "rbp" };
	const auto          badReversed = PickBadPointer(0x50, reversed, reversedNames, 3);
	HS_CHECK_EQ(badReversed.value, 0x4Fu);
	HS_CHECK_EQ(std::string(badReversed.regName), std::string("rcx"));
}

HS_TEST(resilience_bad_pointer_falls_back_to_the_fault_address)
{
	// No candidate within the [fault - 0x1000, fault] window.
	const std::uint64_t values[] = { 0x10, 0x7FF612AB0000 };
	const char*         names[] = { "rbp", "rbx" };
	const auto          bad = PickBadPointer(0x2000, values, names, 2);
	HS_CHECK(!bad.fromRegister);
	HS_CHECK_EQ(bad.value, 0x2000u);
	HS_CHECK_EQ(std::string(bad.regName), std::string("fault"));

	// An empty candidate list is the null case: still safe, still a fallback.
	const auto empty = PickBadPointer(0x50, nullptr, nullptr, 0);
	HS_CHECK(!empty.fromRegister);
	HS_CHECK_EQ(empty.value, 0x50u);
}

HS_TEST(resilience_log_line_is_the_fixed_format)
{
	const auto line = SampleLine();
	char       buffer[256]{};
	const auto full = FormatSuppressLine(buffer, sizeof(buffer), line);

	const std::string expected =
		"resilience-guard: suppress #3 rip=0x7FF612AB34CD rva=0x12AB34CD fault=0x50 "
		"badPtr=0x10 (rbp+0x40) callerRip=0x7FF612AB9010\n";
	HS_CHECK_EQ(std::string(buffer), expected);
	HS_CHECK_EQ(full, expected.size());

	// The no-base-register form is part of the format too.
	auto         fallback = line;
	fallback.bad = BadPointer{ 0x50, "fault", 0, false };
	fallback.index = 1;
	char       zero[256]{};
	const auto fullFallback = FormatSuppressLine(zero, sizeof(zero), fallback);
	const std::string expectedFallback =
		"resilience-guard: suppress #1 rip=0x7FF612AB34CD rva=0x12AB34CD fault=0x50 "
		"badPtr=0x50 (fault-address) callerRip=0x7FF612AB9010\n";
	HS_CHECK_EQ(std::string(zero), expectedFallback);
	HS_CHECK_EQ(fullFallback, expectedFallback.size());
}

HS_TEST(resilience_log_line_formats_zero_values_without_case_drift)
{
	ResilienceFaultLine line;  // all-zero POD
	char                buffer[256]{};
	const auto          full = FormatSuppressLine(buffer, sizeof(buffer), line);
	const std::string   expected =
		"resilience-guard: suppress #0 rip=0x0 rva=0x0 fault=0x0 badPtr=0x0 (fault-address) callerRip=0x0\n";
	HS_CHECK_EQ(std::string(buffer), expected);
	HS_CHECK_EQ(full, expected.size());

	// Hex digits are uppercase (the %llX shape the project logs use).
	ResilienceFaultLine upper;
	upper.rip = 0xABCDEF;
	char       upperBuf[256]{};
	const auto upperFull = FormatSuppressLine(upperBuf, sizeof(upperBuf), upper);
	HS_CHECK(upperFull > 0);
	HS_CHECK(std::string(upperBuf).find("0xABCDEF") != std::string::npos);
}

HS_TEST(resilience_log_line_truncates_safely_and_stays_terminated)
{
	const auto line = SampleLine();

	struct Padded
	{
		char          buffer[16];
		unsigned char canary[32];
	} padded{};
	for (auto& byte : padded.canary) {
		byte = 0xAB;
	}

	const auto full = FormatSuppressLine(padded.buffer, sizeof(padded.buffer), line);

	// snprintf semantics: the FULL length is reported even when truncated...
	const std::string expected =
		"resilience-guard: suppress #3 rip=0x7FF612AB34CD rva=0x12AB34CD fault=0x50 "
		"badPtr=0x10 (rbp+0x40) callerRip=0x7FF612AB9010\n";
	HS_CHECK_EQ(full, expected.size());
	HS_CHECK(full >= sizeof(padded.buffer));

	// ... the buffer is NUL-terminated within bounds, holds the line's prefix...
	HS_CHECK_EQ(std::string(padded.buffer), expected.substr(0, sizeof(padded.buffer) - 1));

	// ... and nothing was written past the buffer.
	for (std::size_t i = 0; i < sizeof(padded.canary); ++i) {
		HS_CHECK_MSG(padded.canary[i] == 0xAB, "FormatSuppressLine wrote past the buffer");
	}

	// A zero-capacity call writes nothing at all and still reports the length.
	HS_CHECK_EQ(FormatSuppressLine(nullptr, 0, line), expected.size());

	// A one-byte buffer is just the terminator.
	char        one[1]{ 'X' };
	const auto  oneFull = FormatSuppressLine(one, 1, line);
	HS_CHECK_EQ(oneFull, expected.size());
	HS_CHECK_EQ(one[0], '\0');
}

HS_TEST(resilience_handler_path_makes_no_heap_allocation)
{
	// The hard requirement: the handler path must allocate NOTHING. This runs
	// the exact sequence HandleResilience performs for an in-scope fault, with
	// the heap probe active around it.
	ResilienceWhitelist whitelist;
	whitelist.SetSingleRange(0x1000, 0x1800);
	SuppressionCounter counter;

	ResilienceFaultLine line;
	char                buffer[256]{};

	const long long before = hstest::HeapAllocations();

	const auto decision = DecideSuppression(whitelist, 0x1086, counter, 64);
	const auto bad = PickBadPointer(0x50, kCrashValues, kCrashNames, 4);
	line.index = decision.index;
	line.rip = 0x7FF612AB34CD;
	line.ripRva = 0x1086;
	line.faultAddr = 0x50;
	line.callerRip = 0x7FF612AB9010;
	line.bad = bad;
	const auto length = FormatSuppressLine(buffer, sizeof(buffer), line);

	const long long after = hstest::HeapAllocations();

	HS_CHECK(decision.suppress);
	HS_CHECK(length > 0);
	HS_CHECK_MSG(after == before, "the handler-path functions must not allocate");

	// The probe itself must be able to SEE an allocation: a counter that cannot
	// move cannot fail, so "zero allocations" would be vacuous. The allocation
	// is made OBSERVABLE (its bytes reach a volatile sink) because GCC elides
	// unused new/delete pairs even for replaceable operator new (-fallocation-dse)
	// -- and an elided allocation is exactly the vacuous pass this guards against.
	static volatile char sink = 0;
	const long long     probeBefore = hstest::HeapAllocations();
	{
		std::string noise(1000, 'x');
		noise[0] = 'y';
		sink = noise[0];
		HS_CHECK_EQ(noise.size(), 1000u);
	}
	HS_CHECK_MSG(hstest::HeapAllocations() > probeBefore, "the heap probe must observe allocations");
	HS_CHECK_EQ(static_cast<char>(sink), 'y');
}
