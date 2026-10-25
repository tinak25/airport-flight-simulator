// Hybrid: MPI across terminals, producer-consumer threads inside each rank.
//
// Each rank owns a group of terminals. Inside the rank, one producer thread
// per terminal feeds a bounded queue and `--consumers` threads process the
// flights. Rank 0 then merges every rank's summary.
#include <mpi.h>

#include <iostream>

#include "engine.hpp"
#include "mpi_gather.hpp"

int main(int argc, char** argv) {
    using namespace airport;
    int provided = 0;
    // Only the main thread calls MPI, so FUNNELED is enough.
    MPI_Init_thread(&argc, &argv, MPI_THREAD_FUNNELED, &provided);
    int rank = 0, ranks = 1;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &ranks);

    Options o;
    try {
        o = parse_options(argc, argv);
    } catch (const std::exception& e) {
        if (rank == 0) {
            std::cerr << "error: " << e.what() << "\n";
            print_usage(argv[0]);
        }
        MPI_Finalize();
        return 1;
    }

    MPI_Barrier(MPI_COMM_WORLD);
    const double start = MPI_Wtime();

    const std::vector<int> mine = terminals_for_rank(o.terminals, rank, ranks);
    const Stats local = mine.empty() ? Stats{} : run_concurrent(o, mine);
    const Stats total = gather_stats(local, rank, ranks);

    const double seconds = MPI_Wtime() - start;
    if (rank == 0) {
        std::cout << "== Hybrid airport simulation (MPI " << ranks << " ranks x " << o.consumers
                  << " consumer threads): " << o.terminals << " terminals x " << o.flights_per_terminal
                  << " flights per terminal (average) ==\n"
                  << format_report(total, o.top_n) << "elapsed_seconds: " << seconds << "\n";
    }
    MPI_Finalize();
    return 0;
}
