#include "PCH.h"

#include "ResilienceGuard.h"

#include "Config.h"
#include "Core/ResiliencePolicy.h"

namespace hs
{
	namespace
	{
		// THE TARGET. The known crash (Skyrim AE 1.7.104.0) is a near-NULL
		// object dereference inside one engine function, identified portably by
		// its Address Library id:
		//
		//   Address Library ID 26938  (crash log: "SkyrimSE.exe+0438AD6 ->
		//   26938+0x86", i.e. the fault sits 0x86 bytes into ID 26938's
		//   function, `mov r15d,[rbp+0x40]` with rbp = 0x10).
		//
		// Cross-check for SkyrimSE.exe 1.7.104.0 ONLY (NOT used at runtime):
		// ID 26938 resolves to RVA 0x438A50, whose verified prologue is
		//   mov %rcx,0x8(%rsp); push %rbp; push %rsi; push %rdi;
		//   push %r12; push %r13; push %r14; push %r15; sub $0x60,%rsp;
		//   movq $0xfffffffffffffffe,0x20(%rsp); mov %rbx,0xb0(%rsp)
		// preceded by int3 padding -- a real function entry with .pdata unwind
		// info. The prologue BYTES below are the runtime identity check; the RVA
		// deliberately is not hardcoded anywhere: the entry comes from the
		// Address Library, so the whitelist follows the game build.
		inline constexpr std::uint32_t kTargetAddressLibraryId = 26938;

		// The recovery scope: the whole function, [entry, entry + 0x800). This
		// is the ENTIRE whitelist -- the handler never catches anything outside
		// it. (0x438A50 + 0x800 = 0x439250 in 1.7.104.0.)
		inline constexpr std::uint64_t kTargetScopeSpan = 0x800;

		// First 16 prologue bytes of ID 26938's function in 1.7.104.0:
		//   48 89 4C 24 08    mov [rsp+8], rcx
		//   55                push rbp
		//   56                push rsi
		//   57                push rdi
		//   41 54             push r12
		//   41 55             push r13
		//   41 56             push r14
		//   41 57             push r15
		inline constexpr std::uint8_t kExpectedPrologue[16] = {
			0x48, 0x89, 0x4C, 0x24, 0x08,
			0x55, 0x56, 0x57,
			0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57
		};

		// Everything the handler touches is a POD global, written once at load
		// and read-only from the handler, so the handler path can never
		// allocate, lock or log through spdlog. The one sink is a file handle
		// PRE-OPENED here at load time.
		PVOID               g_handler = nullptr;
		HANDLE              g_log = INVALID_HANDLE_VALUE;
		ResilienceWhitelist g_whitelist;
		SuppressionCounter  g_counter;
		std::uint32_t       g_cap = 0;

		// Fault-safe qword read for the leaf-function fallback. No C++ objects
		// in this frame, so SEH is legal here.
		[[nodiscard]] bool SafeReadQword(std::uintptr_t a_addr, std::uint64_t& a_out)
		{
			__try {
				a_out = *reinterpret_cast<const std::uint64_t*>(a_addr);
				return true;
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				a_out = 0;
				return false;
			}
		}

		// The candidate base registers, most plausible first: the known crash
		// is `mov r15d,[rbp+0x40]` (rbp is the base), and frame- and
		// callee-saved registers are the usual [base + disp] forms. Ties in the
		// displacement heuristic break by this order.
		struct RegValue
		{
			const char*   name;
			std::uint64_t value;
		};

		[[nodiscard]] std::size_t FillRegisters(const CONTEXT& a_ctx, RegValue* a_out)
		{
			const RegValue all[] = {
				{ "rbp", a_ctx.Rbp }, { "rbx", a_ctx.Rbx }, { "rsi", a_ctx.Rsi }, { "rdi", a_ctx.Rdi },
				{ "rax", a_ctx.Rax }, { "rcx", a_ctx.Rcx }, { "rdx", a_ctx.Rdx },
				{ "r8", a_ctx.R8 },   { "r9", a_ctx.R9 },   { "r10", a_ctx.R10 }, { "r11", a_ctx.R11 },
				{ "r12", a_ctx.R12 }, { "r13", a_ctx.R13 }, { "r14", a_ctx.R14 }, { "r15", a_ctx.R15 },
			};
			const std::size_t count = sizeof(all) / sizeof(all[0]);
			for (std::size_t i = 0; i < count; ++i) {
				a_out[i] = all[i];
			}
			return count;
		}

		// The whole recovery decision. THE ONLY RECOVERY PATH: an access
		// violation whose RIP falls inside the whitelisted range. Everything
		// else -- other exception codes, other RIPs, a failed unwind, the
		// fail-open cap reached -- is passed on untouched (CONTINUE_SEARCH).
		//
		// Allocation-free: POD locals, fixed buffers, the pre-opened handle.
		LONG HandleResilience(EXCEPTION_POINTERS* a_info)
		{
			if (a_info == nullptr || a_info->ExceptionRecord == nullptr || a_info->ContextRecord == nullptr) {
				return EXCEPTION_CONTINUE_SEARCH;
			}
			if (a_info->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION) {
				return EXCEPTION_CONTINUE_SEARCH;
			}
			if (a_info->ExceptionRecord->NumberParameters < 2) {
				return EXCEPTION_CONTINUE_SEARCH;
			}

			CONTEXT* const ctx = a_info->ContextRecord;
			const auto     rip = static_cast<std::uintptr_t>(ctx->Rip);

			// RIP -> RVA relative to SkyrimSE.exe. The whitelist holds RVAs; a
			// RIP below the module base can never be inside it.
			const auto exeBase = reinterpret_cast<std::uintptr_t>(::GetModuleHandleW(nullptr));
			if (rip < exeBase) {
				return EXCEPTION_CONTINUE_SEARCH;
			}
			const auto ripRva = rip - exeBase;

			// The whitelist is the ENTIRE scope: never look at anything else.
			if (!g_whitelist.Contains(ripRva)) {
				return EXCEPTION_CONTINUE_SEARCH;
			}

			// Unwind to the CALLER's context BEFORE spending a suppression from
			// the cap: a fault we cannot unwind away must not consume one.
			//
			// UNW_FLAG_NHANDLER: we use the function's unwind info purely to
			// restore registers up the frame; no handler registration in the
			// .pdata is honoured (there is none for this function).
			CONTEXT caller = *ctx;

			DWORD64           imageBase = 0;
			PRUNTIME_FUNCTION functionEntry = ::RtlLookupFunctionEntry(rip, &imageBase, nullptr);
			if (functionEntry != nullptr) {
				void*   handlerData = nullptr;
				DWORD64 establisher = 0;
				::RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, rip, functionEntry,
					&caller, &handlerData, &establisher, nullptr);
			} else {
				// Leaf function: the return address sits at [Rsp].
				std::uint64_t returnAddress = 0;
				if (!SafeReadQword(static_cast<std::uintptr_t>(caller.Rsp), returnAddress)) {
					return EXCEPTION_CONTINUE_SEARCH;
				}
				caller.Rip = returnAddress;
				caller.Rsp += 8;
			}
			if (caller.Rip == rip) {
				// The unwind did not advance: refuse rather than loop.
				return EXCEPTION_CONTINUE_SEARCH;
			}

			// Count, cap and decide. After uMaxSuppressions this declines and
			// the crash is allowed to happen (fail OPEN).
			const auto decision = DecideSuppression(g_whitelist, ripRva, g_counter, g_cap);
			if (!decision.suppress) {
				return EXCEPTION_CONTINUE_SEARCH;
			}

			// One fixed-format line through the pre-opened handle. No
			// std::string, no spdlog, no snprintf, no heap.
			const auto faultAddr = static_cast<std::uint64_t>(a_info->ExceptionRecord->ExceptionInformation[1]);

			RegValue      registers[16]{};
			const auto    registerCount = FillRegisters(*ctx, registers);
			std::uint64_t values[16]{};
			const char*   names[16]{};
			for (std::size_t i = 0; i < registerCount; ++i) {
				values[i] = registers[i].value;
				names[i] = registers[i].name;
			}
			const auto bad = PickBadPointer(faultAddr, values, names, registerCount);

			ResilienceFaultLine line;
			line.index = decision.index;
			line.rip = rip;
			line.ripRva = ripRva;
			line.faultAddr = faultAddr;
			line.callerRip = static_cast<std::uint64_t>(caller.Rip);
			line.bad = bad;

			char        buffer[256]{};
			const auto  full = FormatSuppressLine(buffer, sizeof(buffer), line);
			const auto  length = full < sizeof(buffer) ? full : sizeof(buffer) - 1;

			if (g_log != INVALID_HANDLE_VALUE) {
				DWORD written = 0;
				::WriteFile(g_log, buffer, static_cast<DWORD>(length), &written, nullptr);
			}

			// Continue at the caller's next instruction with a zero return.
			caller.Rax = 0;
			*ctx = caller;
			return EXCEPTION_CONTINUE_EXECUTION;
		}

		LONG CALLBACK Handler(EXCEPTION_POINTERS* a_info)
		{
			__try {
				return HandleResilience(a_info);
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				// Never let the guard's own path turn one fault into two; fall
				// through to the real handler (Crash Logger, the game's own).
				return EXCEPTION_CONTINUE_SEARCH;
			}
		}
	}

	bool InstallResilienceGuard()
	{
		if (g_handler != nullptr) {
			return true;
		}

		// 1. Resolve the target through the Address Library. No hardcoded RVA:
		//    the entry is whatever this game build's versionlib maps ID 26938 to.
		REL::Relocation<std::uintptr_t> target{ REL::ID(kTargetAddressLibraryId) };
		const auto                      entry = target.address();
		if (entry == 0) {
			logger::warn("resilience-guard: Address Library ID {} resolved to 0 - not installed", kTargetAddressLibraryId);
			return false;
		}

		const auto exeBase = reinterpret_cast<std::uintptr_t>(REL::Module::get().base());
		if (entry < exeBase) {
			logger::warn("resilience-guard: Address Library ID {} resolved outside SkyrimSE.exe - not installed", kTargetAddressLibraryId);
			return false;
		}
		const auto entryRva = entry - exeBase;

		// 2. Identity check against the verified prologue. A different build (or
		//    a mod that patched the function before us) must NOT get the guard:
		//    catching faults in the wrong function is worse than crashing.
		if (std::memcmp(reinterpret_cast<const void*>(entry), kExpectedPrologue, sizeof(kExpectedPrologue)) != 0) {
			logger::warn("resilience-guard: Address Library ID {} (rva 0x{:X}) does not start with the verified prologue - "
						 "wrong game build or the function was patched before us; NOT installed",
				kTargetAddressLibraryId, entryRva);
			return false;
		}

		// 3. The whitelist: [entry, entry + 0x800) in RVA space, unless the ini
		//    explicitly overrides it.
		const auto& config = Config::Get();
		if (!config.rvaWhitelist.empty()) {
			if (!g_whitelist.Parse(config.rvaWhitelist.c_str()) || g_whitelist.Size() == 0) {
				logger::warn("resilience-guard: sRvaWhitelist={} parses to no range - not installed", config.rvaWhitelist);
				return false;
			}
			logger::info("resilience-guard: whitelist OVERRIDDEN from sRvaWhitelist ({} range(s))", g_whitelist.Size());
		} else {
			g_whitelist.SetSingleRange(entryRva, entryRva + kTargetScopeSpan);
		}
		g_cap = config.maxSuppressions;

		// 4. Pre-open the suppression log: the handler must never open a file
		//    (or allocate) mid-fault. If the log cannot be opened, NOTHING is
		//    suppressed -- a suppression that cannot be recorded is not an
		//    honest suppression.
		const auto logPath = PluginDir() / "FaceGenGuard-resilience.log";
		g_log = ::CreateFileA(logPath.string().c_str(), FILE_APPEND_DATA, FILE_SHARE_READ,
			nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (g_log == INVALID_HANDLE_VALUE) {
			logger::warn("resilience-guard: cannot open {} (error {}) - nothing will be suppressed",
				logPath.string(), ::GetLastError());
			return false;
		}

		g_handler = ::AddVectoredExceptionHandler(1, &Handler);
		if (g_handler == nullptr) {
			logger::warn("resilience-guard: AddVectoredExceptionHandler failed ({})", ::GetLastError());
			::CloseHandle(g_log);
			g_log = INVALID_HANDLE_VALUE;
			return false;
		}

		logger::info("resilience-guard: installed (Address Library ID {} entry rva 0x{:X}, scope 0x{:X} bytes, "
					 "fail-open cap {} suppressions, log {})",
			kTargetAddressLibraryId, entryRva, kTargetScopeSpan, g_cap, logPath.string());
		return true;
	}

	void RemoveResilienceGuard()
	{
		if (g_handler != nullptr) {
			::RemoveVectoredExceptionHandler(g_handler);
			g_handler = nullptr;
		}
		if (g_log != INVALID_HANDLE_VALUE) {
			::CloseHandle(g_log);
			g_log = INVALID_HANDLE_VALUE;
		}
	}

	void LogResilienceStatus()
	{
		// The normal (non-handler) path: the suppression total and the cap.
		// The handler itself cannot log through spdlog (allocation), so this is
		// where the running total becomes visible.
		logger::info("resilience-guard: {}/{} suppressions used, {} refused by the fail-open cap",
			g_counter.Count(), g_cap, g_counter.Declined());
	}

	std::uint32_t ResilienceSuppressions() noexcept
	{
		return g_counter.Count();
	}

	std::uint32_t ResilienceDeclined() noexcept
	{
		return g_counter.Declined();
	}

	std::uint32_t ResilienceCap() noexcept
	{
		return g_cap;
	}
}
