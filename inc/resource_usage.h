#ifndef RESOURCE_USAGE_H
#define RESOURCE_USAGE_H

#include <cstdint>
#include <expected>
#include <optional>
#include <system_error>
#include <sys/types.h>

namespace juan {

    template <typename T>
    using ResourceResult = std::expected<T, std::error_code>;

    //Guarda la informacion de uso de memoria y CPU del sistema como resultado final de la funcion view(), no como bytes sino como porcentaje en double de uso de memoria y CPU
    struct ProcessUsage {
        ResourceResult<double> memory_usage_percent;
        ResourceResult<double> cpu_usage_percent;
    };

    struct MemoryUsage {
        std::uint64_t total_bytes;
        std::uint64_t available_bytes;
        std::uint64_t used_bytes;
        double used_percent;
    };

    struct CpuTimes {
        std::uint64_t total_ticks;
        std::uint64_t idle_ticks;
    };

    struct CpuUsage {
        double percent;
    };

    //Struct para almacenar la informacion de uso de memoria y CPU de un proceso
    struct ProcessMemoryUsage {
        std::uint64_t rss_bytes;
        std::uint64_t vms_bytes;
    };

    void view();
    void view_pid(pid_t pid);
    
    [[nodiscard]] ResourceResult<MemoryUsage> get_memory_usage();  //Declaracion de la funcion read_memory_usage que devuelve un ResourceResult de tipo MemoryUsage
    [[nodiscard]] ResourceResult<CpuTimes> get_cpu_times();        //Declaracion de la funcion read_cpu_times que devuelve un ResourceResult de tipo CpuTimes
    [[nodiscard]] std::optional<CpuUsage> system_cpu_percent(const CpuTimes& previous, const CpuTimes& current) noexcept;  //Declaracion de la funcion system_cpu_percent que devuelve un std::optional de tipo CpuUsage y toma dos parametros de tipo CpuTimes
    [[nodiscard]] ResourceResult<std::uint64_t> get_process_cpu_ticks(pid_t pid);
    [[nodiscard]] ResourceResult<ProcessMemoryUsage> get_process_memory(pid_t pid);
    [[nodiscard]] ProcessUsage get_process_usage(pid_t pid);
    void print_process_usage(const ProcessUsage& usage);
    [[nodiscard]] std::optional<CpuUsage> process_cpu_percent(std::uint64_t previous_process_ticks, std::uint64_t current_process_ticks, 
                                                                        const CpuTimes& previous_system, const CpuTimes& current_system, unsigned cpu_count) noexcept;

} // namespace juan

#endif /* ifndef RESOURCES_VIEW_H */