#pragma once

#include <string>

namespace colt {

struct Response {
    int status{200};
    std::string body;
};

} // namespace colt
