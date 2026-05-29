#include "error_handlers.h"

#include <fstream>
#include <iostream>
#include <string>

struct GlobalLogFile {
    std::fstream file;
    bool verbose = false;
    bool is_open = false;
};

GlobalLogFile global_log_file;

void openLogFile(const std::string& filename, bool verbose) {
    global_log_file.file.open(filename, std::ios::out | std::ios::trunc);
    global_log_file.verbose = verbose;
    global_log_file.is_open = global_log_file.file.is_open();
}

void closeLogFile() {
    if (global_log_file.is_open) {
        global_log_file.file.close();
        global_log_file.is_open = false;
    }
}

static std::string severityToString(Severity severity) {
    switch (severity) {
        case Severity::INFO:
            return "INFO";
        case Severity::WARNING:
            return "WARNING";
        case Severity::ERROR:
            return "ERROR";
        case Severity::CRITICAL:
            return "CRITICAL";
    }
    return "UNKNOWN";
}

void handleLogMessage(
    const std::string& message, Severity severity, int exit_code, std::fstream& output) {
    output << "[" << severityToString(severity) << "] " << message << std::endl;
    if (severity == Severity::CRITICAL) {
        std::exit(exit_code);
    }
}

void handleLogMessage(const std::string& message, Severity severity, int exit_code) {
    if (global_log_file.is_open) {
        handleLogMessage(message, severity, exit_code, global_log_file.file);
    } else {
        std::cerr << "[" << severityToString(severity) << "] " << message << std::endl;
        if (severity == Severity::CRITICAL) {
            std::exit(exit_code);
        }
    }
}

void handleLogMessage(const std::string& message) {
    handleLogMessage(message, Severity::INFO, 0);
}
