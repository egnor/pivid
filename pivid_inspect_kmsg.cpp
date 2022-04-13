// Command line tool like 'dmesg' but with better timestamps.

#include <regex>
#include <string>

#include <CLI/App.hpp>
#include <CLI/Config.hpp>
#include <CLI/Formatter.hpp>
#include <fmt/core.h>

#include "logging_policy.h"
#include "unix_system.h"

namespace pivid {

std::string const levels[8] = {
    "💥", "🔥", "🚨", "🛑", "⚠️", "🪧", "ℹ️", "🕸️"
};

extern "C" int main(int const argc, char const* const* const argv) {
    std::string log_arg;
    CLI::App app("Read and print kernel log buffer");
    app.add_flag("--log", log_arg, "Log level/configuration");
    CLI11_PARSE(app, argc, argv);

    configure_logging(log_arg);
    auto const logger = make_logger("main");

    try {
        DEBUG(logger, "Opening /dev/kmsg");
        auto const sys = global_system();
        auto open_ret = sys->open("/dev/kmsg", O_RDWR | O_NONBLOCK);
        if (open_ret.err == EACCES)
            open_ret = sys->open("/dev/kmsg", O_RDONLY | O_NONBLOCK);

        auto const kmsg = std::move(open_ret).ex("/dev/kmsg");
        auto const now_rt = sys->clock(CLOCK_REALTIME);
        auto const now_mt = sys->clock(CLOCK_MONOTONIC_RAW);
        int64_t last_seq = -1;

        auto const banner = fmt::format(
            "=== pivid_inspect_kmsg {} (mt={:.3f}) ===",
            format_realtime(now_rt), now_mt
        );

        auto const inject = fmt::format("<5> {}\n", banner);
        (void) kmsg->write(inject.data(), inject.size());
        fmt::print("{}\n", banner);

        for (;;) {
            char record[8192];
            TRACE(logger, "Reading log record");
            auto const ret = kmsg->read(record, sizeof(record));
            if (ret.err == EPIPE) continue;  // kmsg skipped records
            if (ret.err == EAGAIN) break;

            auto const len = ret.ex("read /dev/kmsg");
            CHECK_RUNTIME(len > 0, "Bad /dev/kmsg read: {} bytes", len);
            ASSERT(len <= int(sizeof(record)));

            int tag = 0, prefix = 0;
            int64_t seq = 0, micros = 0;
            char flag = '\0';
            int const scanned = sscanf(
                record, "%d,%ld,%ld,%c;%n",
                &tag, &seq, &micros, &flag, &prefix
            );

            CHECK_RUNTIME(scanned == 4, "Bad kmsg record \"{}\"", record);
            CHECK_RUNTIME(prefix > 0, "Bad kmsg prefix \"{}\"", record);
            ASSERT(prefix < len);

            auto const newline = std::find(record + prefix, record + len, '\n');
            CHECK_RUNTIME(newline != record + len, "No newline \"{}\"", record);
            *newline = '\0';

            if (last_seq >= 0 && seq != last_seq + 1)
                fmt::print("*** skipped {} records ***\n", seq - last_seq - 1);
            last_seq = seq;

            double const mt = micros * 1e-6;
            double const rt = now_rt - now_mt + mt;
            static std::regex const tab{"\\\\x09"};
            fmt::print(
                "{} {} {}\n",
                (mt < now_mt - 43200 ? format_realtime : abbrev_realtime)(rt),
                levels[tag % 8],
                std::regex_replace(record + prefix, tab, "»   ")
            );
        }

        fmt::print("\n");
    } catch (std::exception const& e) {
        logger->critical("{}", e.what());
        return 1;
    }

    return 0;
}

}  // namespace pivid
