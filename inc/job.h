#ifndef JUAN_JOB_H
#define JUAN_JOB_H

#include <types.h>
#include <timestamp.h>

namespace juan {

enum JobStatus : s8 {
	QUEUED    = 0,
	LAUNCHED  = 1,
	RUNNING   = 2,
	SUCCEDED  = 3,
	REJECTED  = -1,
	CANCELING = -2,
	CANCELED  = -3,
	FAILED    = -4,
};

constexpr std::string_view Jobs_tatus_str(JobStatus status) noexcept {
    switch (status) {
        case QUEUED:
            return "Encolado";
        case LAUNCHED:
            return "lanzado";
        case RUNNING:
            return "RUNNING";
        case SUCCEDED:
            return "corriedno";
        case REJECTED:
            return "rechado";
        case CANCELING:
            return "CANCELING";
        case CANCELED:
            return "cancelado";
        case FAILED:
            return "fallido";
    }

    return "INVALID_STATUS";
}

using JobID = s64;

struct Job {
	JobID id;
	str command;
	JobStatus status;
	opt<Timestamp> queued_at;
	opt<Timestamp> launched_at;
	opt<Timestamp> finished_at;
	opt<u8> result;
};

} // namespace juan

#endif /* ifndef JUAN_JOB_H */
