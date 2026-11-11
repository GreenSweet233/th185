#include "Rng.hpp"

namespace th185 {

namespace {
Rng g_rng;
}  // namespace

Rng& global_rng() noexcept { return g_rng; }

}  // namespace th185
