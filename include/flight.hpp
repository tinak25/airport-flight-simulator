// Flight records and a deterministic flight generator.
//
// Every version of the simulator (sequential, threaded, MPI, hybrid) generates
// exactly the same flights for a given seed, so their reports can be compared
// line by line. The random number generator is written out here (splitmix64)
// instead of using <random> distributions, because std::uniform_int_distribution
// gives different numbers on libstdc++ (Linux) and libc++ (macOS).
#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace airport {

inline constexpr std::array<const char*, 5> kAirlines = {
    "Qantas", "Emirates", "Singapore Airlines", "Etihad", "Virgin Australia"};
inline constexpr std::array<const char*, 6> kGates = {"G1", "G2", "G3", "G4", "G5", "G6"};

inline constexpr int kNumAirlines = static_cast<int>(kAirlines.size());
inline constexpr int kNumGates = static_cast<int>(kGates.size());

enum class Status : std::uint8_t { Departed, Arrived, Delayed, Cancelled };

struct Flight {
    int terminal = 0;
    int index = 0;           // position within its terminal's schedule
    int airline = 0;         // index into kAirlines
    int gate = 0;            // index into kGates
    int passengers = 0;
    Status status = Status::Departed;
    int delay_minutes = 0;   // only non-zero for delayed flights
};

// Small, fast, platform-independent PRNG.
class SplitMix64 {
public:
    explicit SplitMix64(std::uint64_t seed) : state_(seed) {}
    std::uint64_t next() {
        std::uint64_t z = (state_ += 0x9E3779B97F4A7C15ULL);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }
    // Integer in [lo, hi]. The tiny modulo bias is irrelevant for a simulation.
    int between(int lo, int hi) {
        return lo + static_cast<int>(next() % static_cast<std::uint64_t>(hi - lo + 1));
    }

private:
    std::uint64_t state_;
};

// Generates one terminal's flights in schedule order.
//
// Delay chain: if an airline's previous flight at this terminal was delayed,
// its next flight is delayed with 45% probability instead of the base 15%
// (a late inbound aircraft makes the next departure late too).
class FlightGenerator {
public:
    FlightGenerator(std::uint64_t seed, int terminal)
        : rng_(seed * 1000003ULL + static_cast<std::uint64_t>(terminal) * 7919ULL + 1),
          terminal_(terminal) {}

    Flight next() {
        Flight f;
        f.terminal = terminal_;
        f.index = count_++;
        f.airline = rng_.between(0, kNumAirlines - 1);
        f.gate = rng_.between(0, kNumGates - 1);
        f.passengers = rng_.between(80, 420);

        const int delay_chance = last_delayed_[f.airline] ? 45 : 15;
        const int roll = rng_.between(0, 99);
        if (roll < delay_chance) {
            f.status = Status::Delayed;
            f.delay_minutes = rng_.between(5, 90);
        } else if (roll < delay_chance + 4) {
            f.status = Status::Cancelled;
            f.passengers = 0;  // nobody flies on a cancelled flight
        } else {
            f.status = rng_.between(0, 1) == 0 ? Status::Departed : Status::Arrived;
        }
        last_delayed_[f.airline] = (f.status == Status::Delayed);
        return f;
    }

private:
    SplitMix64 rng_;
    int terminal_;
    int count_ = 0;
    std::array<bool, kNumAirlines> last_delayed_{};
};

// Stand-in for the real work of handling a flight (checking manifests, updating
// the departures board, rebooking passengers...). It burns `work` iterations of
// CPU and returns a checksum so the compiler cannot optimise it away.
//
// The original coursework version used sleep_for() here. Sleeping makes threads
// look faster than they are, because sleeping threads do not compete for CPU.
inline std::uint64_t process_flight(const Flight& f, int work) {
    std::uint64_t h = 1469598103934665603ULL ^ static_cast<std::uint64_t>(f.terminal * 100003 + f.index);
    for (int i = 0; i < work; ++i) {
        h ^= static_cast<std::uint64_t>(f.passengers + i);
        h *= 1099511628211ULL;
    }
    return h;
}

}  // namespace airport
