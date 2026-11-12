#include "test.hpp"

#include <colt/context.hpp>

#include <atomic>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {
    struct SingletonService {
        int value{42};
    };

    struct RequestService {
        int value{7};
    };
} // namespace

TEST_CASE(


    "context shares singleton instances across contexts"
) {
    colt::ServiceRegistry registry;
    registry.add_singleton<SingletonService>([] {
        return std::make_shared<SingletonService>();
    });

    colt::RequestContext first{registry};
    colt::RequestContext second{registry};
    REQUIRE(&first.get<SingletonService>() == &second.get<SingletonService>());
}

TEST_CASE(


    "context constructs a singleton once under concurrent resolution"
) {
    colt::ServiceRegistry registry;
    std::atomic<int> constructions{0};
    registry.add_singleton<SingletonService>([&] {
        constructions.fetch_add(1, std::memory_order_relaxed);
        return std::make_shared<SingletonService>();
    });

    constexpr std::size_t thread_count = 16;
    std::vector<std::unique_ptr<colt::RequestContext>> contexts;
    contexts.reserve(thread_count);
    for (std::size_t i = 0; i < thread_count; ++i) {
        contexts.push_back(std::make_unique<colt::RequestContext>(registry));
    }

    std::vector<SingletonService *> instances(thread_count);
    std::vector<std::thread> threads;
    threads.reserve(thread_count);
    for (std::size_t i = 0; i < thread_count; ++i) {
        threads.emplace_back([&, i] {
            instances[i] = &contexts[i]->get<SingletonService>();
        });
    }
    for (auto &thread: threads) thread.join();

    REQUIRE(constructions.load(std::memory_order_relaxed) == 1);
    for (const auto *instance: instances)
        REQUIRE(instance == instances.front());
}

TEST_CASE(


    "context caches request instances within each context only"
) {
    colt::ServiceRegistry registry;
    registry.add_request<RequestService>([] {
        return std::make_shared<RequestService>();
    });

    colt::RequestContext first{registry};
    colt::RequestContext second{registry};
    REQUIRE(&first.get<RequestService>() == &first.get<RequestService>());
    REQUIRE(&first.get<RequestService>() != &second.get<RequestService>());
}

TEST_CASE(


    "context rejects missing and duplicate service registrations"
) {
    colt::ServiceRegistry registry;
    colt::RequestContext context{registry};

    bool missing_rejected = false;
    try {
        (void) context.get<SingletonService>();
    } catch (const std::out_of_range &) {
        missing_rejected = true;
    }
    REQUIRE(missing_rejected);

    registry.add_singleton<SingletonService>([] {
        return std::make_shared<SingletonService>();
    });
    bool duplicate_rejected = false;
    try {
        registry.add_request<SingletonService>([] {
            return std::make_shared<SingletonService>();
        });
    } catch (const std::logic_error &) {
        duplicate_rejected = true;
    }
    REQUIRE(duplicate_rejected);
}
