#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace hs
{
	// Directory containing FaceGenGuard.dll (Data/SKSE/Plugins), resolved from
	// the module handle rather than from the current working directory.
	[[nodiscard]] const std::filesystem::path& PluginDir();

	// Configuration, read once from <PluginDir>/FaceGenGuard.ini.
	struct Config
	{
		// [Resilience] bEnabled -- DEFAULT OFF, and the whole plugin is inert
		// without it. This feature is explicitly behaviour-changing: it turns a
		// crash into continued execution by discarding a half-done function
		// call. A diagnostic must not change semantics unless the operator
		// explicitly asks, so the startup summary prints
		// `behaviourChanging=resilience-guard` whenever it is on, and
		// `behaviourChanging=none (semantics-preserving)` whenever it is off.
		bool enabled = false;

		// [Resilience] uMaxSuppressions -- fail-open cap. After this many
		// suppressions the guard stops suppressing and lets the crash happen,
		// so one known crash cannot become an unknown, repeatedly-corrupted
		// state that keeps running.
		std::uint32_t maxSuppressions = 64;

		// [Resilience] sRvaWhitelist -- OPTIONAL OVERRIDE, EMPTY by default.
		// Empty = the Address Library target only (ID 26938, [entry, entry +
		// 0x800) computed at runtime; see ResilienceGuard.cpp). Non-empty =
		// exactly these hex RVA ranges instead ("0xAAAAAA-0xBBBBBB, ...", end
		// exclusive), for a game build where the built-in target does not
		// apply. The RVAs belong to a SPECIFIC SkyrimSE.exe build; the shipped
		// default is deliberately not a hardcoded range.
		std::string rvaWhitelist;

		static Config& Get();
		void           Load();

		// One-line summary for the startup banner. It names the setting that
		// changes game semantics so a player reading their log can always see
		// that this run was NOT behaviour-preserving.
		[[nodiscard]] std::string Summary() const;
	};
}
