#include <gtest/gtest.h>

#include <job.h>

using namespace juan;

TEST(JobTest, DefaultInitializationHasQueuedStatusAndEmptyTimestamps) {
	Job job{};

	EXPECT_EQ(job.id, 0);
	EXPECT_TRUE(job.command.empty());
	EXPECT_EQ(job.status, QUEUED);
	EXPECT_FALSE(job.queued_at.has_value());
	EXPECT_FALSE(job.launched_at.has_value());
	EXPECT_FALSE(job.finished_at.has_value());
	EXPECT_FALSE(job.result.has_value());
}

TEST(JobTest, StoresJobDetails) {
	Job job{};
	job.id          = 42;
	job.command     = "sleep 10";
	job.status      = RUNNING;
	job.queued_at   = 100;
	job.launched_at = 110;
	job.finished_at = 120;
	job.result      = 0;

	EXPECT_EQ(job.id, 42);
	EXPECT_EQ(job.command, "sleep 10");
	EXPECT_EQ(job.status, RUNNING);
	ASSERT_TRUE(job.queued_at.has_value());
	EXPECT_EQ(*job.queued_at, 100);
	ASSERT_TRUE(job.launched_at.has_value());
	EXPECT_EQ(*job.launched_at, 110);
	ASSERT_TRUE(job.finished_at.has_value());
	EXPECT_EQ(*job.finished_at, 120);
	ASSERT_TRUE(job.result.has_value());
	EXPECT_EQ(*job.result, 0);
}

TEST(JobTest, JobStatusValuesRemainStable) {
	EXPECT_EQ(QUEUED, 0);
	EXPECT_EQ(LAUNCHED, 1);
	EXPECT_EQ(RUNNING, 2);
	EXPECT_EQ(SUCCEDED, 3);
	EXPECT_EQ(REJECTED, -1);
	EXPECT_EQ(CANCELING, -2);
	EXPECT_EQ(CANCELED, -3);
	EXPECT_EQ(FAILED, -4);
}
