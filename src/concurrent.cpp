// One process: a producer thread per terminal feeds a bounded queue,
// a pool of consumer threads processes the flights.
#include <chrono>
#include <iostream>

#include "engine.hpp"

int main(int argc, char** argv) {
    using namespace airport;
    try {
        const Options o = parse_options(argc, argv);
        std::vector<int> all;
        for (int t = 1; t <= o.terminals; ++t) all.push_back(t);

        const auto start = std::chrono::steady_clock::now();
        const Stats stats = run_concurrent(o, all);
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();

        std::cout << "== Concurrent airport simulation: " << o.terminals << " terminals x "
                  << o.flights_per_terminal << " flights per terminal (average) ==\n"
                  << format_report(stats, o.top_n) << "elapsed_seconds: " << seconds << "\n";
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        print_usage(argv[0]);
        return 1;
    }
    return 0;
}
