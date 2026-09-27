#pragma once

#include <cstdint>

namespace hs
{
	// FaceGenGuard -- the Windows glue of the resilience guard.
	//
	// Installs a first-chance vectored exception handler that recovers from a
	// memory fault at a WHITELISTED set of code addresses and from nowhere else.
	// On EXCEPTION_ACCESS_VIOLATION with the faulting RIP inside the whitelisted
	// range, the handler unwinds to the caller's context (RtlVirtualUnwind,
	// UNW_FLAG_NHANDLER), sets RAX = 0 there and continues execution. Everything
	// outside the whitelist is passed on untouched.
	//
	// The whitelist is computed at RUNTIME from Address Library ID 26938 as
	// [entry, entry + 0x800) -- no hardcoded RVA is used at runtime.
	//
	// The handler path is ALLOCATION-FREE by construction: no std::string, no
	// spdlog, no heap. Each suppression writes one fixed-format line through a
	// file handle PRE-OPENED at load time (WriteFile), and the decision logic it
	// runs lives in src/Core/ResiliencePolicy.h so it is unit-tested off-game.
	//
	// SELF-CONTAINED: this plugin has no other feature and depends on nothing
	// else -- no ledger, no hooks, no other plugin.
	//
	// Honest limitations (see README): the unwind abandons the faulting
	// function's half-done side effects, and RAX = 0 is only a safe return for
	// scalar/pointer/bool returns, not for struct-by-value returns.
	//
	// Returns false and suppresses NOTHING (handler not installed) when the
	// target cannot be resolved/verified or the log file cannot be opened: a
	// suppression that cannot be recorded is not an honest suppression.
	[[nodiscard]] bool InstallResilienceGuard();
	void               RemoveResilienceGuard();

	// Normal (non-handler) log path: reports the suppression total and the
	// fail-open cap. This one may allocate and log freely.
	void LogResilienceStatus();

	[[nodiscard]] std::uint32_t ResilienceSuppressions() noexcept;
	[[nodiscard]] std::uint32_t ResilienceDeclined() noexcept;
	[[nodiscard]] std::uint32_t ResilienceCap() noexcept;
}
