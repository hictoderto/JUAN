#include "resource_usage.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <unistd.h>

// CPU/RAM Usage View Functions
void juan::view() {
    auto memory_usage = get_memory_usage();
    if (memory_usage) {
        std::cout << "Memory Usage: " << memory_usage->used_percent << "%" << std::endl;
    } else {
        std::cout << "Memory Usage: unavailable (" << memory_usage.error().message()
                  << ")" << std::endl;
    }

    auto previous_cpu_times = get_cpu_times();
    if (!previous_cpu_times) {
        std::cout << "CPU Usage: unavailable (" << previous_cpu_times.error().message()
                  << ")" << std::endl;
        return;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    auto current_cpu_times = get_cpu_times();
    if (!current_cpu_times) {
        std::cout << "CPU Usage: unavailable (" << current_cpu_times.error().message()
                  << ")" << std::endl;
    } else if (auto cpu_usage = calculate_cpu_usage(*previous_cpu_times, *current_cpu_times)) {
        std::cout << "CPU Usage: " << cpu_usage->percent << "%" << std::endl;
    } else {
        std::cout << "CPU Usage: unavailable (could not calculate sample)" << std::endl;
    }
}

[[nodiscard]] juan::ResourceResult<juan::MemoryUsage> juan::get_memory_usage() {
    // Implementación de la función para leer el uso de memoria
   std::ifstream meminfo("/proc/meminfo");
    if (!meminfo.is_open()) {
        return std::unexpected(std::make_error_code(std::errc::io_error));
    }

    std::uint64_t total_kb = 0;
    std::uint64_t available_kb = 0;
    bool has_total = false;
    bool has_available = false;

    std::string line;
    while (std::getline(meminfo, line)) {
        std::istringstream ss(line);
        std::string key;
        std::uint64_t value = 0;
        if (!(ss >> key >> value)) {
            continue;
        }
        if (key == "MemTotal:") {
            total_kb = value;
            has_total = true;
        } else if (key == "MemAvailable:") {
            available_kb = value;
            has_available = true;
        }
        if (has_total && has_available) {
            break;
        }
    }

    if (!has_total || !has_available || total_kb == 0) {
        return std::unexpected(std::make_error_code(std::errc::bad_message));
    }

    const std::uint64_t total_bytes = total_kb * 1024;
    const std::uint64_t available_bytes = available_kb * 1024;
    const std::uint64_t used_bytes = total_bytes - available_bytes;

    return MemoryUsage{
        total_bytes,
        available_bytes,
        used_bytes,
        100.0 * static_cast<double>(used_bytes) / static_cast<double>(total_bytes)
    };
}

[[nodiscard]] juan::ResourceResult<juan::CpuTimes> juan::get_cpu_times() {
    // Implementación de la función para leer los tiempos de CPU
    std::ifstream cpustat("/proc/stat");
    if (!cpustat.is_open()) {
        return std::unexpected(std::make_error_code(std::errc::io_error));
    }

    // Primera línea: cpu user nice system idle iowait irq softirq steal guest guest_nice
    std::string label;
    cpustat >> label;
    if (label != "cpu") {
        return std::unexpected(std::make_error_code(std::errc::bad_message));
    }

    std::uint64_t v[8] = {0};
    for (auto& field : v) {
        if (!(cpustat >> field)) {
            return std::unexpected(std::make_error_code(std::errc::bad_message));
        }
    }

    // guest y guest_nice ya están incluidos en user y nice, por eso no se suman
    const std::uint64_t idle  = v[3] + v[4];  // idle + iowait
    const std::uint64_t total = v[0] + v[1] + v[2] + v[3] + v[4] + v[5] + v[6] + v[7];

    return CpuTimes{total, idle};
}

[[nodiscard]] std::optional<juan::CpuUsage> juan::calculate_cpu_usage(const juan::CpuTimes& previous, const juan::CpuTimes& current) noexcept {
    // Implementación de la función para calcular el uso de CPU basado en los tiempos anteriores y actuales
    if (current.total_ticks <= previous.total_ticks || current.idle_ticks < previous.idle_ticks) {
        return std::nullopt; // No se puede calcular el uso si los ticks actuales son menores o iguales a los anteriores
    }
    
    const std::uint64_t total_diff = current.total_ticks - previous.total_ticks;
    const std::uint64_t idle_diff = current.idle_ticks - previous.idle_ticks;


    const double usage_percent = 100.0 * (1.0 - static_cast<double>(idle_diff) / static_cast<double>(total_diff));
    
    return CpuUsage{usage_percent};
}

// Lee utime + stime acumulados del proceso (en ticks) desde /proc/PID/stat
[[nodiscard]] juan::ResourceResult<std::uint64_t> juan::get_process_cpu_ticks(pid_t pid) {
    std::ifstream stat("/proc/" + std::to_string(pid) + "/stat");
    if (!stat.is_open()) {
        return std::unexpected(std::make_error_code(std::errc::no_such_process));
    }

    std::string line;
    if (!std::getline(stat, line)) {
        return std::unexpected(std::make_error_code(std::errc::io_error));
    }

    const auto close_paren = line.rfind(')');
    if (close_paren == std::string::npos || close_paren + 2 >= line.size()) {
        return std::unexpected(std::make_error_code(std::errc::bad_message));
    }

    std::istringstream ss(line.substr(close_paren + 2));

    std::string skip;
    for (int field = 3; field <= 13; ++field) {
        if (!(ss >> skip)) {
            return std::unexpected(std::make_error_code(std::errc::bad_message));
        }
    }

    std::uint64_t utime = 0;
    std::uint64_t stime = 0;
    if (!(ss >> utime >> stime)) {
        return std::unexpected(std::make_error_code(std::errc::bad_message));
    }

    return utime + stime;
}

void juan::view_pid(pid_t pid) {
    auto process_memory = get_process_memory(pid);
    auto total_memory = get_memory_usage();
    if (!process_memory) {
        std::cout << "Memory Usage: unavailable (" << process_memory.error().message()
                  << ")" << std::endl;
    } else if (!total_memory) {
        std::cout << "Memory Usage: unavailable (" << total_memory.error().message()
                  << ")" << std::endl;
    } else {
        const double memory_percent =
            100.0 * static_cast<double>(process_memory->rss_bytes) /
            static_cast<double>(total_memory->total_bytes);
        std::cout << "Memory Usage: " << memory_percent << "%" << std::endl;
    }

    auto previous_process_ticks = get_process_cpu_ticks(pid);
    auto previous_system_times = get_cpu_times();
    if (!previous_process_ticks) {
        std::cout << "CPU Usage: unavailable (" << previous_process_ticks.error().message()
                  << ")" << std::endl;
        return;
    }
    if (!previous_system_times) {
        std::cout << "CPU Usage: unavailable (" << previous_system_times.error().message()
                  << ")" << std::endl;
        return;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    auto current_process_ticks = get_process_cpu_ticks(pid);
    auto current_system_times = get_cpu_times();
    if (!current_process_ticks) {
        std::cout << "CPU Usage: unavailable (" << current_process_ticks.error().message()
                  << ")" << std::endl;
        return;
    }
    if (!current_system_times) {
        std::cout << "CPU Usage: unavailable (" << current_system_times.error().message()
                  << ")" << std::endl;
        return;
    }

    const long cpu_count = sysconf(_SC_NPROCESSORS_ONLN);
    if (cpu_count <= 0) {
        std::cout << "CPU Usage: unavailable (could not determine CPU count)" << std::endl;
        return;
    }

    if (auto cpu_usage = calculate_process_cpu_usage(
            *previous_process_ticks, *current_process_ticks, *previous_system_times,
            *current_system_times, static_cast<unsigned>(cpu_count))) {
        std::cout << "CPU Usage: " << cpu_usage->percent << "%" << std::endl;
    } else {
        std::cout << "CPU Usage: unavailable (could not calculate sample)" << std::endl;
    }
}

// Lee la memoria residente (RSS) y virtual del proceso desde /proc/PID/status
[[nodiscard]] juan::ResourceResult<juan::ProcessMemoryUsage> juan::get_process_memory(pid_t pid) {
    std::ifstream status("/proc/" + std::to_string(pid) + "/status");
    if (!status.is_open()) {
        return std::unexpected(std::make_error_code(std::errc::no_such_process));
    }

    std::uint64_t rss_kb = 0;
    std::uint64_t virtual_kb = 0;
    bool has_rss = false;

    std::string line;
    while (std::getline(status, line)) {
        std::istringstream ss(line);
        std::string key;
        std::uint64_t value = 0;
        if (!(ss >> key >> value)) {
            continue;
        }
        if (key == "VmRSS:") {
            rss_kb = value;
            has_rss = true;
        } else if (key == "VmSize:") {
            virtual_kb = value;
        }
    }

    if (!has_rss) {
        return std::unexpected(std::make_error_code(std::errc::bad_message));
    }

    return ProcessMemoryUsage{rss_kb * 1024, virtual_kb * 1024};
}

// Calcula el % de CPU del proceso entre dos lecturas (100% = un core completo)
[[nodiscard]] std::optional<juan::CpuUsage> juan::calculate_process_cpu_usage(
    std::uint64_t previous_process_ticks,
    std::uint64_t current_process_ticks,
    const juan::CpuTimes& previous_system,
    const juan::CpuTimes& current_system,
    unsigned cpu_count) noexcept {
    if (cpu_count == 0 ||
        current_system.total_ticks <= previous_system.total_ticks ||
        current_process_ticks < previous_process_ticks) {
        return std::nullopt;
    }

    const std::uint64_t process_diff = current_process_ticks - previous_process_ticks;
    const std::uint64_t total_diff   = current_system.total_ticks - previous_system.total_ticks;

    const double usage_percent =
        100.0 * static_cast<double>(process_diff) / static_cast<double>(total_diff) * cpu_count;
    return CpuUsage{usage_percent};
}