#pragma once

#include <memory>
#include <string>
#include "Logger.h"

// Returns a process-wide logger initialized once with LOG_FOLDER (or ".")
// and the given filename base (e.g., "main"). Subsequent calls reuse it.
std::shared_ptr<Logger> getGlobalLogger(const std::string& filenameBase = "sip_caller");


