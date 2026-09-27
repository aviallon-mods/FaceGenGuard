#include "PCH.h"

#include "Config.h"

namespace hs
{
	namespace
	{
		std::filesystem::path g_pluginDir;

		[[nodiscard]] std::uint32_t ReadUInt(const char* a_section, const char* a_key, std::uint32_t a_default, const std::filesystem::path& a_ini)
		{
			return static_cast<std::uint32_t>(::GetPrivateProfileIntA(a_section, a_key, static_cast<INT>(a_default), a_ini.string().c_str()));
		}

		// The whitelist override is a hex string, so it cannot go through
		// GetPrivateProfileIntA. Bounded buffer; a longer value is truncated by
		// the API, which is acceptable for a startup-time list.
		[[nodiscard]] std::string ReadString(const char* a_section, const char* a_key, const std::string& a_default, const std::filesystem::path& a_ini)
		{
			char       buffer[512]{};
			const auto length = ::GetPrivateProfileStringA(a_section, a_key, a_default.c_str(), buffer, sizeof(buffer), a_ini.string().c_str());
			return std::string(buffer, length);
		}

		void ResolvePluginDir()
		{
			HMODULE self = nullptr;
			if (!::GetModuleHandleExA(
					GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
					reinterpret_cast<LPCSTR>(&ResolvePluginDir),
					&self)) {
				return;
			}

			char        buffer[MAX_PATH]{};
			const auto len = ::GetModuleFileNameA(self, buffer, MAX_PATH);
			if (len == 0) {
				return;
			}

			g_pluginDir = std::filesystem::path(std::string(buffer, len)).parent_path();
		}
	}

	const std::filesystem::path& PluginDir()
	{
		if (g_pluginDir.empty()) {
			ResolvePluginDir();
		}
		return g_pluginDir;
	}

	Config& Config::Get()
	{
		static Config config;
		return config;
	}

	void Config::Load()
	{
		const auto ini = PluginDir() / "FaceGenGuard.ini";
		if (!std::filesystem::exists(ini)) {
			logger::info("no {} - using defaults (bEnabled=0: nothing is installed)", ini.string());
			return;
		}

		enabled = ReadUInt("Resilience", "bEnabled", enabled ? 1u : 0u, ini) != 0;
		maxSuppressions = ReadUInt("Resilience", "uMaxSuppressions", maxSuppressions, ini);
		rvaWhitelist = ReadString("Resilience", "sRvaWhitelist", rvaWhitelist, ini);

		logger::info("config: enabled={} uMaxSuppressions={} sRvaWhitelist={}",
			enabled ? 1 : 0, maxSuppressions,
			rvaWhitelist.empty() ? "(empty: Address Library target)" : rvaWhitelist.c_str());
	}

	std::string Config::Summary() const
	{
		std::string summary = "resilience=" + std::to_string(enabled ? 1 : 0) +
			" cap=" + std::to_string(maxSuppressions) + " ";
		if (enabled) {
			// The token must read `behaviourChanging=resilience-guard` exactly:
			// this is the one line a player cannot miss, because the plugin is
			// changing what the game does instead of merely observing it.
			summary += "behaviourChanging=resilience-guard (faulting call unwound to its caller, rax=0)";
		} else {
			summary += "behaviourChanging=none (semantics-preserving)";
		}
		return summary;
	}
}
