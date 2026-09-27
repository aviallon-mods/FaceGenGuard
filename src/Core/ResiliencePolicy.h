#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

// FaceGenGuard -- the pure policy core of the resilience guard.
//
// WHAT THIS IS. An opt-in, explicitly behaviour-changing SKSE plugin that makes
// the game SURVIVE a known crash instead of dying. On an access violation whose
// RIP falls inside a whitelisted code range, the guard unwinds the faulting
// function to its CALLER's context, sets RAX = 0 there and continues execution.
// The whitelist is the ENTIRE scope: nothing outside it is ever caught or
// touched.
//
// THE KNOWN CRASH (Skyrim AE 1.7.104.0): a near-NULL object dereference in the
// engine function identified by Address Library ID 26938 (its scope is the
// whole recovery whitelist; see ResilienceGuard.cpp). The crash itself is at
// +0x86 into that function (`mov r15d,[rbp+0x40]` with rbp = 0x10).
//
// This file holds only the decision logic the vectored handler executes: the
// code whitelist, the fail-open suppression cap and the fixed-format log line.
// It is plain C++ with no Windows dependency, so the exact code the handler
// runs is built and RUN off-game on Linux and Windows (tests/). The
// Windows-only glue (RtlVirtualUnwind, WriteFile, AddVectoredExceptionHandler)
// lives in src/ResilienceGuard.cpp.
//
// ALLOCATION-FREE BY CONSTRUCTION. The handler path must never allocate: a
// vectored exception handler runs in a process that is already faulting, and
// allocation inside a handler is a prime suspect for hangs. Every function
// here takes fixed buffers or POD inputs and writes only through them; the
// off-game suite counts heap allocations around these calls and asserts ZERO.
//
// HONEST LIMITATIONS (also in the README):
//   * unwinding to the caller ABANDONS the faulting function's half-done side
//     effects. Whatever it already wrote to the world stays written.
//   * RAX = 0 is only a safe return for scalar, pointer and bool returns. A
//     struct-by-value return (hidden sret pointer in RCX) is NOT covered.
//   * the whitelist is computed at runtime from Address Library ID 26938, so
//     it follows the game build -- but it still covers exactly ONE function of
//     ONE build's symbolisation. Re-verify before trusting it elsewhere.

namespace hs
{
	// ------------------------------------------------------------------
	// The whitelist: [begin, end) ranges in the caller's address space.
	// The handler feeds RVAs relative to SkyrimSE.exe; the unit is not
	// the matcher's business.
	// ------------------------------------------------------------------

	inline constexpr std::size_t kResilienceMaxRanges = 8;

	struct CodeRange
	{
		std::uint64_t begin = 0;
		std::uint64_t end = 0;  // EXCLUSIVE
	};

	class ResilienceWhitelist
	{
	public:
		// The normal path: ONE range, computed at runtime from the Address
		// Library target ([entry, entry + 0x800) -- see ResilienceGuard.cpp).
		void SetSingleRange(std::uint64_t a_begin, std::uint64_t a_end) noexcept;

		// Optional ini override: "0x100000-0x100800[,; ]0x200000-0x201000...".
		// Hex, optional 0x prefix, ranges separated by commas, semicolons or
		// whitespace. Reversed, empty or malformed entries are REJECTED and
		// parsing continues with the next one; extra ranges beyond
		// kResilienceMaxRanges are ignored. A null or empty string yields an
		// EMPTY whitelist (never matches anything). Returns true when at least
		// one range was accepted.
		bool Parse(const char* a_text) noexcept;

		void Clear() noexcept;

		// The membership check. Start is inclusive, end is exclusive.
		[[nodiscard]] bool Contains(std::uint64_t a_addr) const noexcept;

		[[nodiscard]] std::size_t Size() const noexcept { return m_count; }
		[[nodiscard]] CodeRange Range(std::size_t a_index) const noexcept;

	private:
		CodeRange   m_ranges[kResilienceMaxRanges]{};
		std::size_t m_count = 0;
	};

	// ------------------------------------------------------------------
	// The fail-open suppression cap.
	// ------------------------------------------------------------------

	struct SuppressionClaim
	{
		bool          granted = false;
		std::uint32_t index = 0;  // 1-based suppression number when granted
	};

	// Counts actual suppressions. The handler must stop suppressing after the
	// cap and let the crash happen (fail OPEN): a guard that could suppress an
	// unbounded number of faults would turn one known crash into an unknown
	// corrupted state that keeps running. Refused attempts are counted too, so
	// the normal log path can report how often the cap stood in the way.
	class SuppressionCounter
	{
	public:
		// Grants while Count() < a_cap; a_cap of 0 never grants. Only granted
		// claims increment the suppression count, so Count() is exactly the
		// number of suppressed faults.
		[[nodiscard]] SuppressionClaim TryClaim(std::uint32_t a_cap) noexcept;

		[[nodiscard]] std::uint32_t Count() const noexcept { return m_count.load(std::memory_order_relaxed); }
		[[nodiscard]] std::uint32_t Declined() const noexcept { return m_declined.load(std::memory_order_relaxed); }
		void                         Reset() noexcept;

	private:
		std::atomic<std::uint32_t> m_count{ 0 };
		std::atomic<std::uint32_t> m_declined{ 0 };
	};

	struct SuppressionDecision
	{
		bool          suppress = false;    // the handler recovers from this fault
		bool          inWhitelist = false; // RIP fell inside the whitelist
		bool          capReached = false;  // refused ONLY because of the cap
		std::uint32_t index = 0;           // 1-based suppression number when suppress
	};

	// The handler's whole decision, in one pure function so "the handler must
	// decline to suppress after the cap and must count correctly" is a tested
	// claim about the exact code the handler runs. A whitelist miss consumes
	// NOTHING (it is not even a suppression attempt).
	[[nodiscard]] SuppressionDecision DecideSuppression(
		const ResilienceWhitelist& a_whitelist,
		std::uint64_t              a_ripRva,
		SuppressionCounter&        a_counter,
		std::uint32_t              a_cap) noexcept;

	// ------------------------------------------------------------------
	// The log line: fixed format, fixed buffers, zero allocation.
	// ------------------------------------------------------------------

	struct BadPointer
	{
		std::uint64_t value = 0;
		const char*   regName = "fault";  // must be a static string
		std::uint32_t displacement = 0;
		bool          fromRegister = false;
	};

	// The bad pointer "from the faulting instruction's base register": for a
	// `[base + disp8/32]` memory operand the fault address is base + disp, so
	// the base register is the register whose value lies in
	// [fault - 0x1000, fault]. This is a HEURISTIC (it decodes no instruction
	// bytes): the nearest such register wins, ties break in the caller's order,
	// and when nothing matches, the fault address itself is reported and
	// `fromRegister` is false. The known crash (`mov r15d,[rbp+0x40]` with
	// rbp = 0x10, faulting at 0x50) is the case it is built for.
	[[nodiscard]] BadPointer PickBadPointer(
		std::uint64_t        a_faultAddr,
		const std::uint64_t* a_values,
		const char* const*   a_names,
		std::size_t          a_count) noexcept;

	struct ResilienceFaultLine
	{
		std::uint32_t index = 0;        // 1-based suppression number
		std::uint64_t rip = 0;          // faulting RIP (VA)
		std::uint64_t ripRva = 0;       // ... and its RVA (what matched)
		std::uint64_t faultAddr = 0;    // ExceptionInformation[1]
		std::uint64_t callerRip = 0;    // RIP the unwind landed on
		BadPointer    bad{};
	};

	// Fixed format (one line, LF-terminated):
	//   resilience-guard: suppress #<n> rip=0x<RIP> rva=0x<RVA> fault=0x<FAULT> badPtr=0x<BASE> (<REG>+0x<DISP>) callerRip=0x<CALLER>
	// with (fault-address) instead of (<REG>+0x<DISP>) when no base register
	// matched. Hex is uppercase without leading zeros (0 -> "0").
	//
	// snprintf semantics: always NUL-terminates when a_capacity > 0 (truncating
	// the line), and returns the length the FULL line would have had, excluding
	// the NUL. Uses only the caller's buffer; no heap, no std::string, no
	// snprintf runtime.
	[[nodiscard]] std::size_t FormatSuppressLine(
		char*                      a_buffer,
		std::size_t                a_capacity,
		const ResilienceFaultLine& a_line) noexcept;
}
