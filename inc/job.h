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
