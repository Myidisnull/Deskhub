#include "Tests.h"
#include "support/TestSupport.h"

#include "deskhub/ui/Brand.h"
#include "deskhubp/diag/LogFile.h"

#include <cstdio>
#include <string>

using deskhubp::LocalTimeHms;
using deskhubp::LogFileName;

namespace {

bool AllDigits(const std::string& s, size_t from, size_t count) {
    if (from + count > s.size()) return false;
    for (size_t i = from; i < from + count; ++i)
        if (s[i] < '0' || s[i] > '9') return false;
    return true;
}

int Number(const std::string& s, size_t from, size_t count) {
    return std::stoi(s.substr(from, count));
}

void TestTheTimestampIsReadableAtAGlance() {
    std::printf("[log] every log line is stamped with a wall-clock time...\n");
    const std::string hms = LocalTimeHms();
    Check(hms.size() == 8, "the stamp is exactly HH:MM:SS wide, so lines stay aligned");
    Check(hms[2] == ':' && hms[5] == ':', "with colons where they belong");
    Check(AllDigits(hms, 0, 2) && AllDigits(hms, 3, 2) && AllDigits(hms, 6, 2),
        "and digits everywhere else");

    Check(Number(hms, 0, 2) <= 23, "the hour is a real hour");
    Check(Number(hms, 3, 2) <= 59, "the minute is a real minute");
    Check(Number(hms, 6, 2) <= 60, "the second is a real second, leap second included");
}

void TestTheLogFileNameIsOnePerDay() {
    std::printf("[log] log file names are one per calendar day, so restarts append...\n");
    const std::string name = LogFileName();

    const std::string prefix = std::string(deskhub::brand::kLogFilePrefix) + "-";
    Check(name.rfind(prefix, 0) == 0, "every log is recognisably ours");
    Check(name == prefix + name.substr(prefix.size(), 8) + ".log",
        "the active name is exactly <prefix>-YYYYMMDD.log");
    Check(name.size() == prefix.size() + std::string("YYYYMMDD.log").size(),
        "with no time or pid suffix on the daily file");

    const size_t dateAt = prefix.size();
    Check(AllDigits(name, dateAt, 8), "YYYYMMDD is eight digits, which is what makes it sortable");

    const int year = Number(name, dateAt, 4);
    const int month = Number(name, dateAt + 4, 2);
    const int day = Number(name, dateAt + 6, 2);
    Check(year >= 2024 && year <= 2200, "the year comes from the real clock");
    Check(month >= 1 && month <= 12, "the month is a real month");
    Check(day >= 1 && day <= 31, "the day is a real day");
}

void TestTheDailyNameIsStable() {
    std::printf("[log] asking twice on the same day gives the same file, not two...\n");
    Check(LogFileName() == LogFileName(), "restarts keep writing into today's file");
}

}

void RunLogFileTests() {
    TestTheTimestampIsReadableAtAGlance();
    TestTheLogFileNameIsOnePerDay();
    TestTheDailyNameIsStable();
}
