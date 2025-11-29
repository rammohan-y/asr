#include "GlobalLogger.h"
#include <cstdlib>

std::shared_ptr<Logger> getGlobalLogger(const std::string& filenameBase) {
    static std::shared_ptr<Logger> logger;
    if (!logger) {
        const char* logFolderEnv = std::getenv("LOG_FOLDER");
        std::string folder = (logFolderEnv && *logFolderEnv) ? logFolderEnv : std::string(".");
        logger = std::make_shared<Logger>(folder);
        logger->setLogFile(filenameBase);
    }
    return logger;
}


