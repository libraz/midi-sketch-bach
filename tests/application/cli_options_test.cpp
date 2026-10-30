#include <gtest/gtest.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <vector>

namespace {

namespace fs = std::filesystem;

/// Runs bach_cli with @p args (output discarded) and returns its exit status, or -1 on abnormal
/// end.
int runCli(const std::string& args) {
  const std::string command =
      std::string("\"") + BACH_CLI_PATH + "\" " + args + " >/dev/null 2>/dev/null";
  const int raw = std::system(command.c_str());
  return WIFEXITED(raw) ? WEXITSTATUS(raw) : -1;
}

/// Runs bach_cli with @p args, storing stderr in @p stderr_path; returns the exit status or -1.
int runCliCapturingStderr(const std::string& args, const std::string& stderr_path) {
  const std::string command =
      std::string("\"") + BACH_CLI_PATH + "\" " + args + " >/dev/null 2>\"" + stderr_path + "\"";
  const int raw = std::system(command.c_str());
  return WIFEXITED(raw) ? WEXITSTATUS(raw) : -1;
}

std::string readFile(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

constexpr int kExitUsage = 2;
constexpr int kExitOutput = 4;
constexpr int kExitGeneration = 3;

}  // namespace

TEST(CliOptionsTest, ComposerPhaseRejectsSeedsAboveSignedRange) {
  EXPECT_EQ(runCli("--composer-phase CelloPrelude --seed 4294967295 -o /tmp/bach-cli-opt.mid"),
            kExitUsage);
  EXPECT_EQ(runCli("--composer-phase CelloPrelude --seed 2147483648 -o /tmp/bach-cli-opt.mid"),
            kExitUsage);
  // Order of the two flags must not matter.
  EXPECT_EQ(runCli("--seed 2147483648 --composer-phase CelloPrelude -o /tmp/bach-cli-opt.mid"),
            kExitUsage);
  std::remove("/tmp/bach-cli-opt.mid");
}

TEST(CliOptionsTest, ProductPathStillAcceptsFullSeedRange) {
  const std::string path = "/tmp/bach-cli-opt-seed.mid";
  std::remove(path.c_str());
  EXPECT_NE(runCli("--seed 4294967295 -o " + path), kExitUsage);
  std::remove(path.c_str());
}

TEST(CliOptionsTest, ComposerPhaseRejectsGeneratedJson) {
  EXPECT_EQ(runCli("--composer-phase FugueExposition3v --seed 1 --generated-json "
                   "-o /tmp/bach-cli-opt-gen.mid"),
            kExitUsage);
  EXPECT_FALSE(fs::exists("/tmp/bach-cli-opt-gen.generated.json"));
}

TEST(CliOptionsTest, BpmZeroIsTheDefaultSentinel) {
  const std::string explicit_zero = "/tmp/bach-cli-opt-bpm0.mid";
  const std::string omitted = "/tmp/bach-cli-opt-bpm-omitted.mid";
  std::remove(explicit_zero.c_str());
  std::remove(omitted.c_str());
  ASSERT_EQ(runCli("--seed 407 --bpm 0 -o " + explicit_zero), 0);
  ASSERT_EQ(runCli("--seed 407 -o " + omitted), 0);
  const std::string bytes = readFile(explicit_zero);
  EXPECT_FALSE(bytes.empty());
  EXPECT_EQ(bytes, readFile(omitted));
  std::remove(explicit_zero.c_str());
  std::remove(omitted.c_str());

  EXPECT_EQ(runCli("--seed 407 --bpm 39 -o /tmp/bach-cli-opt-bpm39.mid"), kExitUsage);
  EXPECT_EQ(runCli("--seed 407 --bpm 201 -o /tmp/bach-cli-opt-bpm201.mid"), kExitUsage);
}

TEST(CliOptionsTest, FreeCounterpointHelpStatesPassacagliaRestriction) {
  const std::string help_path = "/tmp/bach-cli-opt-help.txt";
  const std::string command =
      std::string("\"") + BACH_CLI_PATH + "\" --help >" + help_path + " 2>/dev/null";
  ASSERT_EQ(std::system(command.c_str()), 0);
  const std::string help = readFile(help_path);
  std::remove(help_path.c_str());
  const std::size_t flag = help.find("--free-counterpoint");
  ASSERT_NE(flag, std::string::npos);
  const std::size_t next_option = help.find("--composer-phase", flag);
  ASSERT_NE(next_option, std::string::npos);
  EXPECT_NE(help.substr(flag, next_option - flag).find("Passacaglia"), std::string::npos);
}

TEST(CliOptionsTest, SidecarWriteFailureExitsWithOutputError) {
  std::error_code ec;
  const fs::path dir =
      fs::temp_directory_path(ec) / ("bach-cli-opt-sidecar-" + std::to_string(::getpid()));
  ASSERT_FALSE(ec);
  fs::remove_all(dir, ec);
  ASSERT_TRUE(fs::create_directories(dir, ec)) << ec.message();
  const std::string midi = (dir / "song.mid").string();

  // A directory squatting on a sidecar path makes the open fail.
  const auto expectFailureWith = [&](const char* squatter, const std::string& args, int expected) {
    std::error_code local;
    fs::remove_all(dir / squatter, local);
    ASSERT_TRUE(fs::create_directories(dir / squatter, local)) << local.message();
    EXPECT_EQ(runCli(args + " -o " + midi), expected) << squatter;
    fs::remove_all(dir / squatter, local);
  };
  expectFailureWith("song.json", "--seed 405 --json", kExitOutput);
  expectFailureWith("song.provenance.json", "--seed 405 --generated-json", kExitOutput);
  expectFailureWith("song.generated.json", "--seed 405 --generated-json", kExitOutput);
  // Harness mode reports the same failure instead of warning and exiting 0.
  expectFailureWith("song.json", "--composer-phase FugueExposition3v --seed 1 --json", kExitOutput);

  EXPECT_EQ(runCli("--seed 405 --json --generated-json -o " + midi), 0);
  fs::remove_all(dir, ec);
}

TEST(CliOptionsTest, FailureOutputNeverPrintsTheInvalidSpanSentinel) {
  // Whether or not this request is rejected, a rejection must name the seed and
  // render a span-less rule failure as absent rather than as the raw sentinel.
  const std::string out_path = "/tmp/bach-cli-opt-fail.mid";
  const std::string err_path = "/tmp/bach-cli-opt-fail.err";
  const int status = runCliCapturingStderr(
      "--form chorale_prelude --character severe --seed 2 --bars 128 -o " + out_path, err_path);
  const std::string err = readFile(err_path);
  EXPECT_EQ(err.find("4294967295"), std::string::npos) << err;
  if (status == kExitGeneration) {
    EXPECT_NE(err.find("Seed: 2"), std::string::npos) << err;
  }
  std::remove(out_path.c_str());
  std::remove(err_path.c_str());
}
