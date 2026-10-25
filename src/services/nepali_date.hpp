#pragma once

#include <glibmm/date.h>

#include <array>
#include <optional>

// Bikram Sambat (BS, the Nepali civil calendar) <-> Gregorian conversion.
// BS month lengths follow no arithmetic rule: they are published per year by
// Nepal's calendar committee, so every converter carries a table. Ours covers
// BS 2000..2090 (1943-04-14 .. 2034-04-13), the range shared by the common
// nepali-date libraries; years past 2081 are the committee's projections and
// may be revised. Conversion counts days from the epoch through Julian day
// numbers, so a date is valid only while it stays inside the table.
namespace hyprshell::nepali {

struct Date {
    int year = 0;
    int month = 0; // 1..12
    int day = 0;   // 1..32
};

constexpr int kFirstYear = 2000;
constexpr int kLastYear = 2090;

// 1 Baishakh 2000 BS == 14 April 1943 AD
constexpr int kEpochGregorianYear = 1943;
constexpr int kEpochGregorianMonth = 4;
constexpr int kEpochGregorianDay = 14;

constexpr const char* kMonths[] = {
    "BAISHAKH", "JESTHA", "ASHADH", "SHRAWAN", "BHADRA",  "ASHWIN",
    "KARTIK",   "MANGSIR", "POUSH",  "MAGH",    "FALGUN", "CHAITRA",
};

// clang-format off
constexpr std::array<std::array<int, 12>, kLastYear - kFirstYear + 1> kMonthDays{{
    {30,32,31,32,31,30,30,30,29,30,29,31}, // 2000
    {31,31,32,31,31,31,30,29,30,29,30,30}, // 2001
    {31,31,32,32,31,30,30,29,30,29,30,30}, // 2002
    {31,32,31,32,31,30,30,30,29,29,30,31}, // 2003
    {30,32,31,32,31,30,30,30,29,30,29,31}, // 2004
    {31,31,32,31,31,31,30,29,30,29,30,30}, // 2005
    {31,31,32,32,31,30,30,29,30,29,30,30}, // 2006
    {31,32,31,32,31,30,30,30,29,29,30,31}, // 2007
    {31,31,31,32,31,31,29,30,30,29,29,31}, // 2008
    {31,31,32,31,31,31,30,29,30,29,30,30}, // 2009
    {31,31,32,32,31,30,30,29,30,29,30,30}, // 2010
    {31,32,31,32,31,30,30,30,29,29,30,31}, // 2011
    {31,31,31,32,31,31,29,30,30,29,30,30}, // 2012
    {31,31,32,31,31,31,30,29,30,29,30,30}, // 2013
    {31,31,32,32,31,30,30,29,30,29,30,30}, // 2014
    {31,32,31,32,31,30,30,30,29,29,30,31}, // 2015
    {31,31,31,32,31,31,29,30,30,29,30,30}, // 2016
    {31,31,32,31,31,31,30,29,30,29,30,30}, // 2017
    {31,32,31,32,31,30,30,29,30,29,30,30}, // 2018
    {31,32,31,32,31,30,30,30,29,30,29,31}, // 2019
    {31,31,31,32,31,31,30,29,30,29,30,30}, // 2020
    {31,31,32,31,31,31,30,29,30,29,30,30}, // 2021
    {31,32,31,32,31,30,30,30,29,29,30,30}, // 2022
    {31,32,31,32,31,30,30,30,29,30,29,31}, // 2023
    {31,31,31,32,31,31,30,29,30,29,30,30}, // 2024
    {31,31,32,31,31,31,30,29,30,29,30,30}, // 2025
    {31,32,31,32,31,30,30,30,29,29,30,31}, // 2026
    {30,32,31,32,31,30,30,30,29,30,29,31}, // 2027
    {31,31,32,31,31,31,30,29,30,29,30,30}, // 2028
    {31,31,32,31,32,30,30,29,30,29,30,30}, // 2029
    {31,32,31,32,31,30,30,30,29,29,30,31}, // 2030
    {30,32,31,32,31,30,30,30,29,30,29,31}, // 2031
    {31,31,32,31,31,31,30,29,30,29,30,30}, // 2032
    {31,31,32,32,31,30,30,29,30,29,30,30}, // 2033
    {31,32,31,32,31,30,30,30,29,29,30,31}, // 2034
    {30,32,31,32,31,31,29,30,30,29,29,31}, // 2035
    {31,31,32,31,31,31,30,29,30,29,30,30}, // 2036
    {31,31,32,32,31,30,30,29,30,29,30,30}, // 2037
    {31,32,31,32,31,30,30,30,29,29,30,31}, // 2038
    {31,31,31,32,31,31,29,30,30,29,30,30}, // 2039
    {31,31,32,31,31,31,30,29,30,29,30,30}, // 2040
    {31,31,32,32,31,30,30,29,30,29,30,30}, // 2041
    {31,32,31,32,31,30,30,30,29,29,30,31}, // 2042
    {31,31,31,32,31,31,29,30,30,29,30,30}, // 2043
    {31,31,32,31,31,31,30,29,30,29,30,30}, // 2044
    {31,32,31,32,31,30,30,29,30,29,30,30}, // 2045
    {31,32,31,32,31,30,30,30,29,29,30,31}, // 2046
    {31,31,31,32,31,31,30,29,30,29,30,30}, // 2047
    {31,31,32,31,31,31,30,29,30,29,30,30}, // 2048
    {31,32,31,32,31,30,30,30,29,29,30,30}, // 2049
    {31,32,31,32,31,30,30,30,29,30,29,31}, // 2050
    {31,31,31,32,31,31,30,29,30,29,30,30}, // 2051
    {31,31,32,31,31,31,30,29,30,29,30,30}, // 2052
    {31,32,31,32,31,30,30,30,29,29,30,30}, // 2053
    {31,32,31,32,31,30,30,30,29,30,29,31}, // 2054
    {31,31,32,31,31,31,30,29,30,29,30,30}, // 2055
    {31,31,32,31,32,30,30,29,30,29,30,30}, // 2056
    {31,32,31,32,31,30,30,30,29,29,30,31}, // 2057
    {30,32,31,32,31,30,30,30,29,30,29,31}, // 2058
    {31,31,32,31,31,31,30,29,30,29,30,30}, // 2059
    {31,31,32,32,31,30,30,29,30,29,30,30}, // 2060
    {31,32,31,32,31,30,30,30,29,29,30,31}, // 2061
    {30,32,31,32,31,31,29,30,29,30,29,31}, // 2062
    {31,31,32,31,31,31,30,29,30,29,30,30}, // 2063
    {31,31,32,32,31,30,30,29,30,29,30,30}, // 2064
    {31,32,31,32,31,30,30,30,29,29,30,31}, // 2065
    {31,31,31,32,31,31,29,30,30,29,29,31}, // 2066
    {31,31,32,31,31,31,30,29,30,29,30,30}, // 2067
    {31,31,32,32,31,30,30,29,30,29,30,30}, // 2068
    {31,32,31,32,31,30,30,30,29,29,30,31}, // 2069
    {31,31,31,32,31,31,29,30,30,29,30,30}, // 2070
    {31,31,32,31,31,31,30,29,30,29,30,30}, // 2071
    {31,32,31,32,31,30,30,29,30,29,30,30}, // 2072
    {31,32,31,32,31,30,30,30,29,29,30,31}, // 2073
    {31,31,31,32,31,31,30,29,30,29,30,30}, // 2074
    {31,31,32,31,31,31,30,29,30,29,30,30}, // 2075
    {31,32,31,32,31,30,30,30,29,29,30,30}, // 2076
    {31,32,31,32,31,30,30,30,29,30,29,31}, // 2077
    {31,31,31,32,31,31,30,29,30,29,30,30}, // 2078
    {31,31,32,31,31,31,30,29,30,29,30,30}, // 2079
    {31,32,31,32,31,30,30,30,29,29,30,30}, // 2080
    {31,31,32,32,31,30,30,30,29,30,30,30}, // 2081
    {30,32,31,32,31,30,30,30,29,30,30,30}, // 2082
    {31,31,32,31,31,30,30,30,29,30,30,30}, // 2083
    {31,31,32,31,31,30,30,30,29,30,30,30}, // 2084
    {31,32,31,32,30,31,30,30,29,30,30,30}, // 2085
    {30,32,31,32,31,30,30,30,29,30,30,30}, // 2086
    {31,31,32,31,31,31,30,30,29,30,30,30}, // 2087
    {30,31,32,32,30,31,30,30,29,30,30,30}, // 2088
    {30,32,31,32,31,30,30,30,29,30,30,30}, // 2089
    {30,32,31,32,31,30,30,30,29,30,30,30}, // 2090
}};
// clang-format on

inline bool year_in_range(int year) {
    return year >= kFirstYear && year <= kLastYear;
}

// 0 for a year outside the table
inline int days_in_month(int year, int month) {
    if (!year_in_range(year) || month < 1 || month > 12)
        return 0;
    return kMonthDays[static_cast<std::size_t>(year - kFirstYear)][static_cast<std::size_t>(month - 1)];
}

inline guint32 epoch_julian() {
    return Glib::Date(kEpochGregorianDay, static_cast<Glib::Date::Month>(kEpochGregorianMonth),
                      kEpochGregorianYear)
        .get_julian();
}

// Days from 1 Baishakh 2000 to the given BS date; nullopt outside the table
// or for an invalid day-of-month.
inline std::optional<int> days_since_epoch(Date d) {
    if (!year_in_range(d.year) || d.month < 1 || d.month > 12 || d.day < 1 ||
        d.day > days_in_month(d.year, d.month))
        return std::nullopt;
    int days = 0;
    for (int y = kFirstYear; y < d.year; ++y)
        for (int m = 1; m <= 12; ++m)
            days += days_in_month(y, m);
    for (int m = 1; m < d.month; ++m)
        days += days_in_month(d.year, m);
    return days + d.day - 1;
}

inline std::optional<Glib::Date> to_gregorian(Date d) {
    const auto offset = days_since_epoch(d);
    if (!offset)
        return std::nullopt;
    Glib::Date g;
    g.set_julian(epoch_julian() + static_cast<guint32>(*offset));
    return g;
}

inline std::optional<Date> from_gregorian(const Glib::Date& g) {
    if (!g.valid())
        return std::nullopt;
    const guint32 julian = g.get_julian();
    const guint32 epoch = epoch_julian();
    if (julian < epoch)
        return std::nullopt;
    int remaining = static_cast<int>(julian - epoch);
    for (int y = kFirstYear; y <= kLastYear; ++y) {
        for (int m = 1; m <= 12; ++m) {
            const int len = days_in_month(y, m);
            if (remaining < len)
                return Date{y, m, remaining + 1};
            remaining -= len;
        }
    }
    return std::nullopt;
}

inline std::optional<Date> from_gregorian(int year, int month, int day) {
    if (!Glib::Date::valid_dmy(static_cast<Glib::Date::Day>(day),
                               static_cast<Glib::Date::Month>(month),
                               static_cast<Glib::Date::Year>(year)))
        return std::nullopt;
    return from_gregorian(Glib::Date(static_cast<Glib::Date::Day>(day),
                                     static_cast<Glib::Date::Month>(month),
                                     static_cast<Glib::Date::Year>(year)));
}

} // namespace hyprshell::nepali
