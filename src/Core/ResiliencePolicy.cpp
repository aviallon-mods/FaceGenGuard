#if defined(FG_NO_PCH)
#	include "Core/ResiliencePolicy.h"
#else
#	include "PCH.h"

#	include "Core/ResiliencePolicy.h"
#endif

namespace hs
{
	namespace
	{
		// The formatter is hand-rolled on purpose. snprintf and std::string are
		// both allocation-free for these arguments in practice, but "the handler
		// path allocates nothing" must hold by CONSTRUCTION, not by trusting a C
		// runtime's internals -- so the only machinery in here is char copying
		// and hex conversion, and the off-game suite counts heap allocations
		// around these calls to prove it.

		struct Cursor
		{
			char*       buffer;
			std::size_t capacity;
			std::size_t written;  // total length the full line would have had
		};

		void PutChar(Cursor& a_c, char a_ch) noexcept
		{
			if (a_c.buffer != nullptr && a_c.written + 1 < a_c.capacity) {
				a_c.buffer[a_c.written] = a_ch;
			}
			++a_c.written;
		}

		void PutStr(Cursor& a_c, const char* a_text) noexcept
		{
			for (const char* p = a_text; *p != '\0'; ++p) {
				PutChar(a_c, *p);
			}
		}

		// Uppercase hex, no leading zeros, "0" for zero -- the %llX shape used
		// throughout the project's logs.
		void PutHex(Cursor& a_c, std::uint64_t a_value) noexcept
		{
			PutStr(a_c, "0x");
			char        digits[16];
			std::size_t count = 0;
			do {
				const auto nibble = static_cast<unsigned>(a_value & 0xF);
				digits[count++] = static_cast<char>(nibble < 10 ? ('0' + nibble) : ('A' + nibble - 10));
				a_value >>= 4;
			} while (a_value != 0);
			while (count > 0) {
				PutChar(a_c, digits[--count]);
			}
		}

		void PutDec(Cursor& a_c, std::uint32_t a_value) noexcept
		{
			char        digits[10];
			std::size_t count = 0;
			do {
				digits[count++] = static_cast<char>('0' + (a_value % 10));
				a_value /= 10;
			} while (a_value != 0);
			while (count > 0) {
				PutChar(a_c, digits[--count]);
			}
		}

		[[nodiscard]] bool IsSeparator(char a_ch) noexcept
		{
			return a_ch == ',' || a_ch == ';' || a_ch == ' ' || a_ch == '\t' ||
				   a_ch == '\r' || a_ch == '\n';
		}

		[[nodiscard]] int HexDigit(char a_ch) noexcept
		{
			if (a_ch >= '0' && a_ch <= '9') {
				return a_ch - '0';
			}
			if (a_ch >= 'a' && a_ch <= 'f') {
				return 10 + (a_ch - 'a');
			}
			if (a_ch >= 'A' && a_ch <= 'F') {
				return 10 + (a_ch - 'A');
			}
			return -1;
		}

		// Consumes an optional 0x prefix and at least one hex digit. On success
		// the cursor sits after the digits; on failure it is unchanged.
		[[nodiscard]] bool ParseHex(const char*& a_p, std::uint64_t& a_out) noexcept
		{
			const char* p = a_p;
			if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
				p += 2;
			}
			std::uint64_t value = 0;
			std::size_t   digits = 0;
			for (int d = HexDigit(*p); d >= 0; d = HexDigit(*p)) {
				value = (value << 4) | static_cast<std::uint64_t>(d);
				++p;
				++digits;
				if (digits > 16) {
					return false;  // would overflow: malformed, reject the token
				}
			}
			if (digits == 0) {
				return false;
			}
			a_p = p;
			a_out = value;
			return true;
		}
	}

	void ResilienceWhitelist::SetSingleRange(std::uint64_t a_begin, std::uint64_t a_end) noexcept
	{
		Clear();
		if (a_begin < a_end) {
			m_ranges[m_count++] = CodeRange{ a_begin, a_end };
		}
	}

	bool ResilienceWhitelist::Parse(const char* a_text) noexcept
	{
		Clear();
		if (a_text == nullptr) {
			return false;
		}

		const char* p = a_text;
		while (*p != '\0') {
			while (IsSeparator(*p)) {
				++p;
			}
			if (*p == '\0') {
				break;
			}

			// One token: HEX '-' HEX, end-exclusive. A malformed token is
			// skipped to the next separator rather than aborting the parse, so
			// one typo cannot silently empty the whitelist.
			std::uint64_t begin = 0;
			std::uint64_t end = 0;
			bool          ok = ParseHex(p, begin) && *p == '-' && (++p, ParseHex(p, end));
			if (ok) {
				// The token must end here; "0x10-0x20garbage" is malformed.
				ok = *p == '\0' || IsSeparator(*p);
			}
			if (ok) {
				ok = begin < end;
			}
			if (ok && m_count < kResilienceMaxRanges) {
				m_ranges[m_count++] = CodeRange{ begin, end };
			}

			while (*p != '\0' && !IsSeparator(*p)) {
				++p;
			}
		}
		return m_count > 0;
	}

	void ResilienceWhitelist::Clear() noexcept
	{
		m_count = 0;
	}

	bool ResilienceWhitelist::Contains(std::uint64_t a_addr) const noexcept
	{
		for (std::size_t i = 0; i < m_count; ++i) {
			if (a_addr >= m_ranges[i].begin && a_addr < m_ranges[i].end) {
				return true;
			}
		}
		return false;
	}

	CodeRange ResilienceWhitelist::Range(std::size_t a_index) const noexcept
	{
		if (a_index >= m_count) {
			return CodeRange{};
		}
		return m_ranges[a_index];
	}

	SuppressionClaim SuppressionCounter::TryClaim(std::uint32_t a_cap) noexcept
	{
		std::uint32_t current = m_count.load(std::memory_order_relaxed);
		for (;;) {
			if (current >= a_cap) {
				// Fail OPEN: the crash is allowed to happen. Only real
				// suppressions count towards the cap.
				m_declined.fetch_add(1, std::memory_order_relaxed);
				return SuppressionClaim{ false, 0 };
			}
			if (m_count.compare_exchange_weak(current, current + 1,
					std::memory_order_relaxed, std::memory_order_relaxed)) {
				return SuppressionClaim{ true, current + 1 };
			}
		}
	}

	void SuppressionCounter::Reset() noexcept
	{
		m_count.store(0, std::memory_order_relaxed);
		m_declined.store(0, std::memory_order_relaxed);
	}

	SuppressionDecision DecideSuppression(
		const ResilienceWhitelist& a_whitelist,
		std::uint64_t              a_ripRva,
		SuppressionCounter&        a_counter,
		std::uint32_t              a_cap) noexcept
	{
		SuppressionDecision decision;
		if (!a_whitelist.Contains(a_ripRva)) {
			// The whitelist is the ENTIRE scope: outside it we catch nothing
			// and consume nothing.
			return decision;
		}
		decision.inWhitelist = true;

		const auto claim = a_counter.TryClaim(a_cap);
		if (claim.granted) {
			decision.suppress = true;
			decision.index = claim.index;
		} else {
			decision.capReached = true;
		}
		return decision;
	}

	BadPointer PickBadPointer(
		std::uint64_t        a_faultAddr,
		const std::uint64_t* a_values,
		const char* const*   a_names,
		std::size_t          a_count) noexcept
	{
		BadPointer best;
		best.value = a_faultAddr;
		best.regName = "fault";
		best.displacement = 0;
		best.fromRegister = false;

		bool          have = false;
		std::uint64_t bestDisp = 0;
		for (std::size_t i = 0; i < a_count; ++i) {
			const auto base = a_values[i];
			if (a_faultAddr < base) {
				continue;
			}
			const auto disp = a_faultAddr - base;
			// A [base + disp] operand with disp < 0x1000 covers the known crash
			// (`[rbp+0x40]`) and every plausible small-displacement form.
			if (disp > 0xFFF) {
				continue;
			}
			if (!have || disp < bestDisp) {
				have = true;
				bestDisp = disp;
				best.value = base;
				best.regName = a_names[i];
				best.displacement = static_cast<std::uint32_t>(disp);
				best.fromRegister = true;
			}
		}
		return best;
	}

	std::size_t FormatSuppressLine(
		char*                      a_buffer,
		std::size_t                a_capacity,
		const ResilienceFaultLine& a_line) noexcept
	{
		Cursor cursor{ a_buffer, a_capacity, 0 };

		PutStr(cursor, "resilience-guard: suppress #");
		PutDec(cursor, a_line.index);
		PutStr(cursor, " rip=");
		PutHex(cursor, a_line.rip);
		PutStr(cursor, " rva=");
		PutHex(cursor, a_line.ripRva);
		PutStr(cursor, " fault=");
		PutHex(cursor, a_line.faultAddr);
		PutStr(cursor, " badPtr=");
		PutHex(cursor, a_line.bad.value);
		PutStr(cursor, " (");
		if (a_line.bad.fromRegister) {
			PutStr(cursor, a_line.bad.regName);
			PutChar(cursor, '+');
			PutHex(cursor, a_line.bad.displacement);
		} else {
			PutStr(cursor, "fault-address");
		}
		PutStr(cursor, ") callerRip=");
		PutHex(cursor, a_line.callerRip);
		PutChar(cursor, '\n');

		const auto full = cursor.written;
		if (a_buffer != nullptr && a_capacity > 0) {
			a_buffer[full < a_capacity ? full : a_capacity - 1] = '\0';
		}
		return full;
	}
}
