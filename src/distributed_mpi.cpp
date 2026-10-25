// MPI only: each rank simulates its share of the terminals with a single
// thread, then rank 0 merges the summaries.
//
// Rank 0 also takes a share of the terminals, so no core sits idle waiting.
// It works with any number of ranks, including 1.
#include <mpi.h>

#include <iostream>

#include "engine.hpp"
#include "mpi_gather.hpp"

int main(int argc, char** argv) {
    using namespace airport;
    MPI_Init(&argc, &argv);
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

    const Stats local = run_sequential(o, terminals_for_rank(o.terminals, rank, ranks));
    const Stats total = gather_stats(local, rank, ranks);

    const double seconds = MPI_Wtime() - start;
    if (rank == 0) {
        std::cout << "== Distributed airport simulation (MPI, " << ranks << " ranks): " << o.terminals
                  << " terminals x " << o.flights_per_terminal << " flights per terminal (average) ==\n"
                  << format_report(total, o.top_n) << "elapsed_seconds: " << seconds << "\n";
    }
    MPI_Finalize();
    return 0;
}
