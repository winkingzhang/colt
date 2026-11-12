#include <colt/application.hpp>

#include <thread>

int main() {
    colt::Application app;
    std::thread worker([] {
    });
    worker.join();
}
