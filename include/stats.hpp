// Aggregated statistics and the final report.
#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <iomanip>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "flight.hpp"

namespace airport {

struct Stats {
    long long flights = 0;
    long long passengers = 0;
    long long delayed = 0;
    long long cancelled = 0;
    long long delay_minutes = 0;
    std::uint64_t checksum = 0;  // sum of process_flight() results, proves the same work was done

    std::array<long long, kNumAirlines> airline_passengers{};
    std::array<long long, kNumAirlines> airline_flights{};
    std::array<long long, kNumAirlines> airline_delayed{};
    std::array<long long, kNumGates> gate_flights{};
    std::map<int, long long> terminal_passengers;
    std::map<int, long long> terminal_flights;

    void add(const Flight& f, std::uint64_t work_checksum) {
        ++flights;
        passengers += f.passengers;
        checksum += work_checksum;  // wrapping addition: order does not matter
        airline_passengers[f.airline] += f.passengers;
        ++airline_flights[f.airline];
        ++gate_flights[f.gate];
        terminal_passengers[f.terminal] += f.passengers;
        ++terminal_flights[f.terminal];
        if (f.status == Status::Delayed) {
            ++delayed;
            ++airline_delayed[f.airline];
            delay_minutes += f.delay_minutes;
        } else if (f.status == Status::Cancelled) {
            ++cancelled;
        }
    }

    void merge(const Stats& o) {
        flights += o.flights;
        passengers += o.passengers;
        delayed += o.delayed;
        cancelled += o.cancelled;
        delay_minutes += o.delay_minutes;
        checksum += o.checksum;
        for (int i = 0; i < kNumAirlines; ++i) {
            airline_passengers[i] += o.airline_passengers[i];
            airline_flights[i] += o.airline_flights[i];
            airline_delayed[i] += o.airline_delayed[i];
        }
        for (int g = 0; g < kNumGates; ++g) gate_flights[g] += o.gate_flights[g];
        for (const auto& [t, v] : o.terminal_passengers) terminal_passengers[t] += v;
        for (const auto& [t, v] : o.terminal_flights) terminal_flights[t] += v;
    }

    // Flat encoding for MPI. Variable length, because the number of terminals
    // a rank handled is not fixed.
    std::vector<long long> serialize() const {
        std::vector<long long> out = {flights, passengers, delayed, cancelled, delay_minutes,
                                      static_cast<long long>(checksum)};
        for (int i = 0; i < kNumAirlines; ++i) {
            out.push_back(airline_passengers[i]);
            out.push_back(airline_flights[i]);
            out.push_back(airline_delayed[i]);
        }
        for (int g = 0; g < kNumGates; ++g) out.push_back(gate_flights[g]);
        out.push_back(static_cast<long long>(terminal_passengers.size()));
        for (const auto& [t, v] : terminal_passengers) {
            out.push_back(t);
            out.push_back(v);
            out.push_back(terminal_flights.at(t));
        }
        return out;
    }

    static Stats deserialize(const long long* p) {
        Stats s;
        s.flights = *p++;
        s.passengers = *p++;
        s.delayed = *p++;
        s.cancelled = *p++;
        s.delay_minutes = *p++;
        s.checksum = static_cast<std::uint64_t>(*p++);
        for (int i = 0; i < kNumAirlines; ++i) {
            s.airline_passengers[i] = *p++;
            s.airline_flights[i] = *p++;
            s.airline_delayed[i] = *p++;
        }
        for (int g = 0; g < kNumGates; ++g) s.gate_flights[g] = *p++;
        const long long n = *p++;
        for (long long k = 0; k < n; ++k) {
            const int t = static_cast<int>(*p++);
            s.terminal_passengers[t] = *p++;
            s.terminal_flights[t] = *p++;
        }
        return s;
    }
};

// The report contains no timings, so every version must print exactly the
// same text for the same seed. tests/check_same_results.sh relies on this.
inline std::string format_report(const Stats& s, int top_n) {
    std::ostringstream os;
    os << std::fixed << std::setprecision(1);
    os << "Flights processed : " << s.flights << "\n";
    os << "Passengers        : " << s.passengers << "\n";
    os << "Delayed flights   : " << s.delayed << " (avg delay "
       << (s.delayed ? static_cast<double>(s.delay_minutes) / s.delayed : 0.0) << " min)\n";
    os << "Cancelled flights : " << s.cancelled << "\n";
    os << "Work checksum     : " << s.checksum << "\n";

    if (s.flights == 0) {
        os << "(no flights processed)\n";
        return os.str();
    }

    // Busiest terminals by passengers; ties broken by terminal number so the order is stable.
    std::vector<std::pair<int, long long>> terms(s.terminal_passengers.begin(), s.terminal_passengers.end());
    std::sort(terms.begin(), terms.end(), [](const auto& a, const auto& b) {
        return a.second != b.second ? a.second > b.second : a.first < b.first;
    });
    os << "\nTop " << std::min<int>(top_n, static_cast<int>(terms.size())) << " busiest terminals\n";
    for (int i = 0; i < top_n && i < static_cast<int>(terms.size()); ++i) {
        os << "  " << i + 1 << ". Terminal " << terms[i].first << " : " << terms[i].second << " passengers, "
           << s.terminal_flights.at(terms[i].first) << " flights\n";
    }

    os << "\nAirlines (by passengers)\n";
    std::array<int, kNumAirlines> order{};
    for (int i = 0; i < kNumAirlines; ++i) order[i] = i;
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        return s.airline_passengers[a] != s.airline_passengers[b]
                   ? s.airline_passengers[a] > s.airline_passengers[b]
                   : a < b;
    });
    for (int a : order) {
        const double on_time = s.airline_flights[a]
                                   ? 100.0 * (1.0 - static_cast<double>(s.airline_delayed[a]) / s.airline_flights[a])
                                   : 0.0;
        os << "  " << std::left << std::setw(19) << kAirlines[a] << std::right << std::setw(9)
           << s.airline_passengers[a] << " passengers, " << std::setw(5) << on_time << "% not delayed\n";
    }

    int busiest_gate = 0;
    for (int g = 1; g < kNumGates; ++g)
        if (s.gate_flights[g] > s.gate_flights[busiest_gate]) busiest_gate = g;
    os << "\nBusiest gate      : " << kGates[busiest_gate] << " (" << s.gate_flights[busiest_gate]
       << " flights across all terminals)\n";
    return os.str();
}

}  // namespace airport
