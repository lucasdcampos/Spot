#include "log_capture.h"

#include <spot.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <memory>
#include <numeric>
#include <stdexcept>
#include <thread>
#include <vector>

TEST_CASE("Jobs run on the workers and waits return once their jobs are done", "[jobs]") {
    Spot::JobSystem jobs(4);
    CHECK(jobs.GetWorkerCount() == 4);
    CHECK_FALSE(jobs.IsWorkerThread());

    std::atomic<int> done = 0;
    std::atomic<int> onWorkers = 0;
    Spot::JobCounter counter;
    for (int i = 0; i < 100; ++i) {
        jobs.Run(
            [&] {
                // Long enough that the workers wake up before the waiting thread has run every job itself.
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                onWorkers += jobs.IsWorkerThread() ? 1 : 0;
                ++done;
            },
            &counter);
    }
    jobs.Wait(counter);
    CHECK(counter.IsDone());
    CHECK(done == 100);
    CHECK(onWorkers > 0);
}

TEST_CASE("ParallelFor covers every index once, in ranges of the grain", "[jobs]") {
    for (const std::size_t workers : {std::size_t{0}, std::size_t{3}}) {
        Spot::JobSystem jobs(workers);
        std::vector<int> visits(10'007, 0);
        std::atomic<std::size_t> largestRange = 0;
        jobs.ParallelFor(visits.size(), 64, [&](std::size_t begin, std::size_t end) {
            for (std::size_t i = begin; i < end; ++i) {
                ++visits[i];
            }
            std::size_t largest = largestRange;
            while (end - begin > largest && !largestRange.compare_exchange_weak(largest, end - begin)) {
            }
        });
        CHECK(std::accumulate(visits.begin(), visits.end(), 0) == 10'007);
        CHECK(std::ranges::all_of(visits, [](int count) { return count == 1; }));
        CHECK(largestRange == 64);
    }
}

TEST_CASE("Jobs that wait for jobs of their own never run out of threads", "[jobs]") {
    // With one worker, the outer jobs would block it if waiting did not run the inner ones.
    Spot::JobSystem jobs(1);
    std::atomic<int> inner = 0;
    jobs.ParallelFor(
        8, 1, [&](std::size_t, std::size_t) { jobs.ParallelFor(8, 1, [&](std::size_t, std::size_t) { ++inner; }); });
    CHECK(inner == 64);
}

TEST_CASE("The first error of a job is rethrown to the thread that waits", "[jobs]") {
    Spot::JobSystem jobs(2);
    Spot::JobCounter counter;
    std::atomic<int> finished = 0;
    jobs.Run([] { throw std::runtime_error("broken job"); }, &counter);
    jobs.Run([&] { ++finished; }, &counter);
    CHECK_THROWS_WITH(jobs.Wait(counter), Catch::Matchers::Equals("broken job"));
    CHECK(finished == 1);

    CHECK_THROWS_AS(jobs.ParallelFor(100, 10,
                                     [](std::size_t begin, std::size_t) {
                                         if (begin == 50) {
                                             throw std::logic_error("bad range");
                                         }
                                     }),
                    std::logic_error);
}

TEST_CASE("Errors of jobs nobody waits for are logged", "[jobs]") {
    const auto sink = std::make_shared<Spot::Tests::CaptureSink>();
    const Spot::Tests::ScopedSinks scoped({sink});
    {
        Spot::JobSystem jobs(1);
        jobs.Run([] { throw std::runtime_error("unwatched failure"); });
    }
    REQUIRE(sink->GetRecords().size() == 1);
    CHECK(sink->GetRecords()[0].message == "A job threw: unwatched failure");
}

TEST_CASE("Destroying a job system runs the jobs still queued", "[jobs]") {
    std::atomic<int> done = 0;
    {
        Spot::JobSystem jobs(0);
        for (int i = 0; i < 10; ++i) {
            jobs.Run([&] { ++done; });
        }
    }
    CHECK(done == 10);
}

TEST_CASE("The shared job system leaves a core to the thread that runs frames", "[jobs]") {
    CHECK(Spot::JobSystem::GetDefaultWorkerCount() >= 1);
    CHECK(&Spot::JobSystem::GetShared() == &Spot::JobSystem::GetShared());
    CHECK(Spot::JobSystem::GetShared().GetWorkerCount() == Spot::JobSystem::GetDefaultWorkerCount());
}
