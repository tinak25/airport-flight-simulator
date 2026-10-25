// Unit tests for the bounded queue, the generator and the stats.
// Plain asserts so there is nothing extra to install.
#include <atomic>
#include <cassert>
#include <iostream>
#include <set>
#include <thread>
#include <vector>

#include "bounded_queue.hpp"
#include "engine.hpp"

using namespace airport;

static void test_queue_fifo_and_close() {
    BoundedQueue<int> q(3);
    q.push(1);
    q.push(2);
    assert(q.pop().value() == 1);
    assert(q.pop().value() == 2);
    q.push(3);
    q.close();
    assert(q.pop().value() == 3);   // items left in the queue are still delivered
    assert(!q.pop().has_value());   // then pop reports "closed"
    assert(!q.push(4));             // pushing after close is refused
}

static void test_queue_never_exceeds_capacity_with_many_producers() {
    const int producers = 4, per_producer = 5000;
    BoundedQueue<int> q(8);
    std::atomic<std::size_t> max_seen{0};
    std::vector<std::thread> threads;
    for (int p = 0; p < producers; ++p)
        threads.emplace_back([&, p] {
            for (int i = 0; i < per_producer; ++i) q.push(p * per_producer + i);
        });

    std::set<int> received;
    std::thread consumer([&] {
        while (auto v = q.pop()) {
            std::size_t s = q.size();
            if (s > max_seen) max_seen = s;
            received.insert(*v);
        }
    });
    for (auto& t : threads) t.join();
    q.close();
    consumer.join();

    assert(max_seen <= q.capacity());
    assert(static_cast<int>(received.size()) == producers * per_producer);  // nothing lost or duplicated
}

static void test_generator_is_deterministic() {
    FlightGenerator a(7, 3), b(7, 3), c(8, 3);
    bool any_difference = false;
    for (int i = 0; i < 1000; ++i) {
        Flight x = a.next(), y = b.next(), z = c.next();
        assert(x.airline == y.airline && x.gate == y.gate && x.passengers == y.passengers &&
               x.status == y.status && x.delay_minutes == y.delay_minutes);
        if (x.passengers != z.passengers) any_difference = true;
        assert(x.passengers >= 0 && x.passengers <= 420);
        if (x.status == Status::Cancelled) assert(x.passengers == 0);
        if (x.status != Status::Delayed) assert(x.delay_minutes == 0);
    }
    assert(any_difference);  // a different seed gives different flights
}

static void test_serialize_round_trip() {
    Options o;
    o.terminals = 3;
    o.flights_per_terminal = 200;
    o.work = 10;
    const Stats s = run_sequential(o, {1, 2, 3});
    const auto data = s.serialize();
    const Stats back = Stats::deserialize(data.data());
    assert(format_report(s, 3) == format_report(back, 3));
}

static void test_all_lock_modes_match_sequential() {
    Options o;
    o.terminals = 5;
    o.flights_per_terminal = 300;
    o.work = 50;
    o.consumers = 3;
    const std::string expected = format_report(run_sequential(o, {1, 2, 3, 4, 5}), 3);
    for (LockMode m : {LockMode::Held, LockMode::Shared, LockMode::Local}) {
        o.lock_mode = m;
        assert(format_report(run_concurrent(o, {1, 2, 3, 4, 5}), 3) == expected);
    }
}

static void test_round_robin_covers_every_terminal_once() {
    for (int ranks = 1; ranks <= 7; ++ranks) {
        std::vector<int> seen;
        for (int r = 0; r < ranks; ++r)
            for (int t : terminals_for_rank(6, r, ranks)) seen.push_back(t);
        std::set<int> unique(seen.begin(), seen.end());
        assert(seen.size() == 6 && unique.size() == 6);
    }
}

int main() {
    test_queue_fifo_and_close();
    test_queue_never_exceeds_capacity_with_many_producers();
    test_generator_is_deterministic();
    test_serialize_round_trip();
    test_all_lock_modes_match_sequential();
    test_round_robin_covers_every_terminal_once();
    std::cout << "all 6 tests passed\n";
    return 0;
}
