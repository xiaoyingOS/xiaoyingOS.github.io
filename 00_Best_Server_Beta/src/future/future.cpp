// Future - Promise-based async programming model implementation

#include "best_server/future/future.hpp"

namespace best_server {
namespace future {

// Future specializations are header-only, this file is for any non-template implementations

// Global counter for debugging
std::atomic<int> promise_set_value_call_count{0};

} // namespace future
} // namespace best_server