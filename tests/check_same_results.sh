#!/usr/bin/env bash
# Runs all four programs on the same seed and checks they print identical
# reports (everything except the title line and the timing line).
set -euo pipefail
cd "$(dirname "$0")/.."

ARGS="--terminals 7 --flights 1500 --work 500 --seed 2024"
MPIRUN="${MPIRUN:-mpirun}"
MPI_EXTRA="${MPI_EXTRA:-}"   # e.g. MPI_EXTRA=--oversubscribe for Open MPI on small machines

report() { grep -v -e '^==' -e '^elapsed_seconds' ; }

expected=$(./bin/sequential $ARGS | report)
fail=0

compare() {
    local name="$1"; shift
    local got
    got=$("$@" | report)
    if [[ "$got" == "$expected" ]]; then
        echo "PASS  $name"
    else
        echo "FAIL  $name"
        diff <(echo "$expected") <(echo "$got") || true
        fail=1
    fi
}

for c in 1 3; do
    for lock in held shared local; do
        compare "concurrent  consumers=$c lock=$lock" ./bin/concurrent $ARGS --consumers $c --lock $lock
    done
done
for n in 1 2 3; do
    compare "mpi         ranks=$n" $MPIRUN $MPI_EXTRA -n $n ./bin/distributed_mpi $ARGS
    compare "hybrid      ranks=$n consumers=2" $MPIRUN $MPI_EXTRA -n $n ./bin/hybrid $ARGS --consumers 2
done
# more ranks than terminals: some ranks get no terminals at all
compare "hybrid      ranks=8 (more ranks than terminals)" $MPIRUN $MPI_EXTRA -n 8 ./bin/hybrid $ARGS

if [[ $fail -ne 0 ]]; then
    echo "Some versions disagree with the sequential baseline."
    exit 1
fi
echo "All versions print the same report as the sequential baseline."
