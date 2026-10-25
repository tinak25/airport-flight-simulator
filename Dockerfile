# Builds everything and runs the test suite in a clean Linux container:
#   docker build -t airport-sim .
#   docker run --rm airport-sim
# Run a single program instead, e.g. the hybrid version on 3 ranks:
#   docker run --rm airport-sim mpirun --allow-run-as-root --oversubscribe -n 3 ./bin/hybrid --consumers 2
FROM ubuntu:24.04

RUN apt-get update && apt-get install -y --no-install-recommends \
        g++ make openmpi-bin libopenmpi-dev python3 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY . .
RUN make all bin/test_core

ENV MPIRUN="mpirun --allow-run-as-root" MPI_EXTRA="--oversubscribe"
CMD ["make", "check"]
