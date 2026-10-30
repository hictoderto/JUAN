#include <gtest/gtest.h>

#include <job.h>
#include <validador.h>

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
	job.queued_at   = Timestamp(100);
	job.launched_at = Timestamp(110);
	job.finished_at = Timestamp(120);
	job.result      = 0;

	EXPECT_EQ(job.id, 42);
	EXPECT_EQ(job.command, "sleep 10");
	EXPECT_EQ(job.status, RUNNING);
	ASSERT_TRUE(job.queued_at.has_value());
	EXPECT_EQ(job.queued_at->as_s64(), 100);
	ASSERT_TRUE(job.launched_at.has_value());
	EXPECT_EQ(job.launched_at->as_s64(), 110);
	ASSERT_TRUE(job.finished_at.has_value());
	EXPECT_EQ(job.finished_at->as_s64(), 120);
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

TEST(CommandParserTest, SplitsExecutableAndArgumentsOnWhitespace) {
	auto parsed = parse_command("grep -n needle file.txt");

	ASSERT_TRUE(parsed.has_value());
	ASSERT_EQ(parsed->size(), 4);
	EXPECT_EQ((*parsed)[0], "grep");
	EXPECT_EQ((*parsed)[1], "-n");
	EXPECT_EQ((*parsed)[2], "needle");
	EXPECT_EQ((*parsed)[3], "file.txt");
}

TEST(CommandParserTest, PreservesQuotedAndEscapedSpaces) {
	auto parsed = parse_command("echo 'hello world' \"from JUAN\" escaped\\ value");

	ASSERT_TRUE(parsed.has_value());
	ASSERT_EQ(parsed->size(), 4);
	EXPECT_EQ((*parsed)[0], "echo");
	EXPECT_EQ((*parsed)[1], "hello world");
	EXPECT_EQ((*parsed)[2], "from JUAN");
	EXPECT_EQ((*parsed)[3], "escaped value");

    auto with_tab = parse_command("echo 'a\tb'");
	ASSERT_TRUE(with_tab.has_value());
	ASSERT_EQ(with_tab->size(), 2);
	EXPECT_EQ((*with_tab)[1], "a\tb");
}

TEST(CommandParserTest, PreservesEmptyQuotedArgumentsAndEmptyInput) {
	auto parsed = parse_command("cmd \"\" ''");
	auto empty = parse_command(" \t ");

	ASSERT_TRUE(parsed.has_value());
	ASSERT_EQ(parsed->size(), 3);
	EXPECT_TRUE((*parsed)[1].empty());
	EXPECT_TRUE((*parsed)[2].empty());
	ASSERT_TRUE(empty.has_value());
	EXPECT_TRUE(empty->empty());
}

TEST(CommandParserTest, RejectsUnclosedQuotesAndTrailingEscape) {
	EXPECT_EQ(parse_command("echo 'unfinished").error(), Status::PARSE_ERROR);
	EXPECT_EQ(parse_command("echo trailing\\").error(), Status::PARSE_ERROR);
    EXPECT_EQ(parse_command("echo hello\nworld").error(), Status::PARSE_ERROR);   
	EXPECT_EQ(parse_command("echo hello\n").error(), Status::PARSE_ERROR);        
    EXPECT_EQ(parse_command("echo 'hello\nworld'").error(), Status::PARSE_ERROR); 
	EXPECT_EQ(parse_command("echo \"hello\nworld\"").error(), Status::PARSE_ERROR);
	EXPECT_EQ(parse_command("echo escaped\\\nvalue").error(), Status::PARSE_ERROR);
}
