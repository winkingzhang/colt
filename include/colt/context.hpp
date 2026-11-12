#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <utility>

namespace colt {
    class RequestContext;

    class ServiceRegistry {
    public:
        // Configure before serving; registration must not race with resolution.
        template<class T, class Factory>
        void add_singleton(Factory &&factory) {
            add<T>(std::forward<Factory>(factory), true);
        }

        template<class T, class Factory>
        void add_request(Factory &&factory) {
            add<T>(std::forward<Factory>(factory), false);
        }

    private:
        struct Service {
            Service(std::function<std::shared_ptr<void>()> create, bool is_singleton)
                : factory(std::move(create)), singleton(is_singleton) {
            }

            std::function<std::shared_ptr<void>()> factory;
            bool singleton;
            std::mutex mutex;
            std::shared_ptr<void> instance;
        };

        template<class T, class Factory>
        void add(Factory &&factory, bool singleton) {
            std::function<std::shared_ptr<T>()> typed_factory{
                std::forward<Factory>(factory)
            };
            auto service = std::make_shared<Service>(
                [factory = std::move(typed_factory)] {
                    return std::static_pointer_cast<void>(factory());
                },
                singleton);

            if (!services_.emplace(std::type_index(typeid(T)), std::move(service)).second) {
                throw std::logic_error("service type is already registered");
            }
        }

        std::unordered_map<std::type_index, std::shared_ptr<Service>> services_;

        friend class RequestContext;
    };

    class RequestContext {
    public:
        explicit RequestContext(const ServiceRegistry &registry) : registry_(registry) {
        }

        template<class T>
        T &get() {
            const auto service_it = registry_.services_.find(std::type_index(typeid(T)));
            if (service_it == registry_.services_.end()) {
                throw std::out_of_range("service type is not registered");
            }

            const auto &service = service_it->second;
            std::shared_ptr<void> instance;
            if (service->singleton) {
                std::lock_guard lock(service->mutex);
                if (!service->instance) service->instance = service->factory();
                instance = service->instance;
            } else {
                std::lock_guard lock(cache_mutex_);
                const auto cached = request_cache_.find(std::type_index(typeid(T)));
                if (cached != request_cache_.end()) {
                    instance = cached->second;
                } else {
                    instance = service->factory();
                    request_cache_.emplace(std::type_index(typeid(T)), instance);
                }
            }

            if (!instance) throw std::logic_error("service factory returned an empty pointer");
            return *std::static_pointer_cast<T>(instance);
        }

    private:
        const ServiceRegistry &registry_;
        std::mutex cache_mutex_;
        std::unordered_map<std::type_index, std::shared_ptr<void>> request_cache_;
    };
} // namespace colt
