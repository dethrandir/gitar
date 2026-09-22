#define DOCTEST_CONFIG_IMPLEMENT
#include <cstdio>

#include "doctest/doctest.h"

namespace {

// Prints each test case as it starts and flushes, so a hang in CI points at the
// exact test instead of a silent timeout.
class StartReporter : public doctest::IReporter {
   public:
    explicit StartReporter(const doctest::ContextOptions& options) : options_(options) {}

    void report_query(const doctest::QueryData&) override {}
    void test_run_start() override {}
    void test_run_end(const doctest::TestRunStats&) override {}
    void test_case_start(const doctest::TestCaseData& test_case) override {
        std::fprintf(stdout, "[ RUN  ] %s\n", test_case.m_name);
        std::fflush(stdout);
    }
    void test_case_reenter(const doctest::TestCaseData&) override {}
    void test_case_end(const doctest::CurrentTestCaseStats&) override {}
    void test_case_exception(const doctest::TestCaseException&) override {}
    void subcase_start(const doctest::SubcaseSignature&) override {}
    void subcase_end() override {}
    void log_assert(const doctest::AssertData&) override {}
    void log_message(const doctest::MessageData&) override {}
    void test_case_skipped(const doctest::TestCaseData&) override {}

   private:
    const doctest::ContextOptions& options_;
};

}  // namespace

DOCTEST_REGISTER_REPORTER("startlog", 1, StartReporter);

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    doctest::Context context;
    context.applyCommandLine(argc, argv);
    context.setOption("reporters", "console,startlog");
    context.setOption("no-version", true);
    return context.run();
}
