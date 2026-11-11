#include "test.hpp"

#include <colt/http.hpp>

TEST_CASE("default response has status 200 and empty body") {
    const colt::Response response;
    REQUIRE(response.status == 200);
    REQUIRE(response.body.empty());
}
