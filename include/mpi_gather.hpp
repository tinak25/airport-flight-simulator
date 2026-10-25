// Collects every rank's Stats on rank 0.
//
// Each rank's stats have a different size (it depends on how many terminals
// it handled), so rank 0 first gathers the sizes, then the data with
// MPI_Gatherv. The original coursework version received into a fixed
// char buffer[4096], which aborts with "Message truncated" as soon as a
// message grows past 4 KB.
#pragma once

#include <mpi.h>

#include <vector>

#include "stats.hpp"

namespace airport {

inline Stats gather_stats(const Stats& local, int rank, int ranks) {
    const std::vector<long long> mine = local.serialize();
    const int my_count = static_cast<int>(mine.size());

    std::vector<int> counts(rank == 0 ? ranks : 0);
    MPI_Gather(&my_count, 1, MPI_INT, counts.data(), 1, MPI_INT, 0, MPI_COMM_WORLD);

    std::vector<int> offsets;
    std::vector<long long> all;
    if (rank == 0) {
        offsets.resize(ranks);
        int total = 0;
        for (int r = 0; r < ranks; ++r) {
            offsets[r] = total;
            total += counts[r];
        }
        all.resize(total);
    }
    MPI_Gatherv(mine.data(), my_count, MPI_LONG_LONG, all.data(), counts.data(), offsets.data(),
                MPI_LONG_LONG, 0, MPI_COMM_WORLD);

    Stats merged;
    if (rank == 0) {
        for (int r = 0; r < ranks; ++r) merged.merge(Stats::deserialize(all.data() + offsets[r]));
    }
    return merged;
}

}  // namespace airport
