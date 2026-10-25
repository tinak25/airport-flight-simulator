// Simulation engines shared by the four programs, plus command-line options.
#pragma once

#include <cstdlib>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "bounded_queue.hpp"
#include "flight.hpp"
#include "stats.hpp"

namespace airport {

// How consumer threads update the statistics.
//   held   : take the shared lock, then do the flight's work while holding it.
//            This is the bug from the original coursework version: the threads
//            take turns, so adding consumers adds nothing.
//   shared : do the work first, then lock only for the quick counter updates.
//   local  : each consumer keeps private stats and merges once at the end
//            (no lock in the hot loop at all).
enum class LockMode { Held, Shared, Local };

struct Options {
    int terminals = 6;
    int flights_per_terminal = 2000;
    int work = 100000;         // CPU iterations per flight
    int consumers = 2;         // consumer threads per process
    int queue_capacity = 64;
    int top_n = 3;
    std::uint64_t seed = 42;
    LockMode lock_mode = LockMode::Local;
};

inline void print_usage(const char* prog) {
    std::cerr << "Usage: " << prog
              << " [--terminals N] [--flights AVG_PER_TERMINAL] [--work N] [--consumers N]\n"
                 "       [--queue N] [--top N] [--seed N] [--lock held|shared|local]\n";
}

inline Options parse_options(int argc, char** argv) {
    Options o;
    auto need = [&](int& i) -> std::string {
        if (i + 1 >= argc) throw std::invalid_argument(std::string("missing value for ") + argv[i]);
        return argv[++i];
    };
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--terminals") o.terminals = std::stoi(need(i));
        else if (a == "--flights") o.flights_per_terminal = std::stoi(need(i));
        else if (a == "--work") o.work = std::stoi(need(i));
        else if (a == "--consumers") o.consumers = std::stoi(need(i));
        else if (a == "--queue") o.queue_capacity = std::stoi(need(i));
        else if (a == "--top") o.top_n = std::stoi(need(i));
        else if (a == "--seed") o.seed = std::stoull(need(i));
        else if (a == "--lock") {
            const std::string m = need(i);
            if (m == "held") o.lock_mode = LockMode::Held;
            else if (m == "shared") o.lock_mode = LockMode::Shared;
            else if (m == "local") o.lock_mode = LockMode::Local;
            else throw std::invalid_argument("--lock must be held, shared or local");
        } else if (a == "--help" || a == "-h") {
            print_usage(argv[0]);
            std::exit(0);
        } else {
            throw std::invalid_argument("unknown option " + a);
        }
    }
    if (o.terminals < 1 || o.flights_per_terminal < 0 || o.work < 0 || o.consumers < 1 ||
        o.queue_capacity < 1 || o.top_n < 1)
        throw std::invalid_argument("all sizes must be positive");
    return o;
}

// Terminals differ in size: terminal t schedules between 0.67x and 1.33x the
// average number of flights. This makes the "busiest terminal" ranking mean
// something and gives MPI ranks unequal amounts of work, as in a real airport.
inline int flights_for_terminal(int average, int terminal) {
    return average * (4 + (terminal * 7) % 5) / 6;
}

// Terminals are numbered 1..T. With several MPI ranks, rank r of R handles
// terminals r+1, r+1+R, r+1+2R, ... (round robin).
inline std::vector<int> terminals_for_rank(int total_terminals, int rank, int ranks) {
    std::vector<int> mine;
    for (int t = rank + 1; t <= total_terminals; t += ranks) mine.push_back(t);
    return mine;
}

// One thread does everything, terminal by terminal.
inline Stats run_sequential(const Options& o, const std::vector<int>& terminals) {
    Stats stats;
    for (int t : terminals) {
        FlightGenerator gen(o.seed, t);
        const int n = flights_for_terminal(o.flights_per_terminal, t);
        for (int i = 0; i < n; ++i) {
            const Flight f = gen.next();
            stats.add(f, process_flight(f, o.work));
        }
    }
    return stats;
}

// One producer thread per terminal pushes flights into a shared bounded queue;
// `consumers` threads pop and process them.
inline Stats run_concurrent(const Options& o, const std::vector<int>& terminals) {
    BoundedQueue<Flight> queue(static_cast<std::size_t>(o.queue_capacity));
    Stats shared;
    std::mutex shared_mutex;

    std::vector<std::thread> producers;
    for (int t : terminals) {
        producers.emplace_back([&, t] {
            FlightGenerator gen(o.seed, t);
            const int n = flights_for_terminal(o.flights_per_terminal, t);
            for (int i = 0; i < n; ++i) queue.push(gen.next());
        });
    }

    std::vector<std::thread> consumers;
    for (int c = 0; c < o.consumers; ++c) {
        consumers.emplace_back([&] {
            Stats local;
            while (auto item = queue.pop()) {
                const Flight& f = *item;
                switch (o.lock_mode) {
                    case LockMode::Held: {
                        std::lock_guard<std::mutex> lock(shared_mutex);
                        shared.add(f, process_flight(f, o.work));  // work done while holding the lock
                        break;
                    }
                    case LockMode::Shared: {
                        const std::uint64_t h = process_flight(f, o.work);  // work outside the lock
                        std::lock_guard<std::mutex> lock(shared_mutex);
                        shared.add(f, h);
                        break;
                    }
                    case LockMode::Local:
                        local.add(f, process_flight(f, o.work));
                        break;
                }
            }
            if (o.lock_mode == LockMode::Local) {
                std::lock_guard<std::mutex> lock(shared_mutex);
                shared.merge(local);
            }
        });
    }

    for (auto& p : producers) p.join();
    queue.close();  // only now: every producer has finished
    for (auto& c : consumers) c.join();
    return shared;
}

}  // namespace airport
