#include "PCH.h"

#include "Config.h"
#include "ResilienceGuard.h"

namespace
{
	// The logging setup is the standard CommonLibSSE-NG plugin snippet: one
	// file sink next to the other SKSE logs, FaceGenGuard.log.
	void SetupLog()
	{
		auto logsFolder = SKSE::log::log_directory();
		if (!logsFolder) {
			SKSE::stl::report_and_fail("SKSE log_directory not provided, logs disabled.");
		}
		const auto logFilePath = *logsFolder / std::format("{}.log", "FaceGenGuard");
		auto       fileSink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logFilePath.string(), true);
		auto       logger = std::make_shared<spdlog::logger>("log", std::move(fileSink));
		spdlog::set_default_logger(std::move(logger));
		spdlog::set_level(spdlog::level::info);
		spdlog::flush_on(spdlog::level::info);
	}
}

SKSE_PLUGIN_LOAD(const SKSE::LoadInterface* a_skse)
{
	SKSE::Init(a_skse);
	SetupLog();

	logger::info("FaceGenGuard v{} (Skyrim AE 1.7.104, Address Library ID 26938 target, CommonLibSSE-NG) loading", FG_VERSION);

	hs::Config::Get().Load();
	// The banner line a player cannot miss: it states whether this run changes
	// game semantics (`behaviourChanging=resilience-guard`) or not.
	logger::info("config summary: {}", hs::Config::Get().Summary());

	if (!hs::Config::Get().enabled) {
		logger::info("disabled in FaceGenGuard.ini - installing nothing");
		return true;
	}

	if (!hs::InstallResilienceGuard()) {
		logger::warn("resilience-guard could not be installed - the game is left exactly as it was");
		return true;
	}

	// The suppression total and the fail-open cap, reported from the NORMAL log
	// path every 60 s (the handler itself cannot log through spdlog).
	std::thread([] {
		for (;;) {
			std::this_thread::sleep_for(std::chrono::seconds(60));
			hs::LogResilienceStatus();
		}
	}).detach();

	logger::info("...ready");
	return true;
}
