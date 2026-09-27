#pragma once

// CommonLibSSE-NG first (REX/W32/BASE.h refuses to build if <Windows.h> was
// included before it), then the Windows headers, then the standard library.

#include "RE/Skyrim.h"
#include "SKSE/SKSE.h"

#include "REL/Relocation.h"

#include <Windows.h>

#include <spdlog/sinks/basic_file_sink.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>
#include <string_view>
#include <thread>

// The generated __FaceGenGuardPlugin.cpp (add_commonlibsse_plugin) spells its
// string_view literals with the sv suffix; they resolve through this using
// directive, exactly like the CommonLibSSE-NG plugin template.
using namespace std::literals;

namespace logger = SKSE::log;
