#include "../src/LevelTime.hpp"
#include <cassert>
int main() {
    using namespace cf::leveltime;
    // Saved completion wins and is exact, whatever the live progress says.
    auto t = total(9216, 1830, 20., false);
    assert(t && t->exact && t->ticks == 9216);
    assert(format(1830, t) == "7.62 s / 38.40 s");
    // Extrapolation: 20% after 7.62 s -> ~38.1 s, shown with "~".
    t = total(0, 1830, 20., false);
    assert(t && !t->exact && t->ticks == 9150);
    assert(format(1830, t) == "7.62 s / ~38.12 s");
    // Too early, start position / test mode / platformer, or no progress: no invented total.
    assert(!total(0, 100, 4.9, false));
    assert(!total(0, 1830, 20., true));
    assert(!total(0, 0, 50., false));
    assert(!total(0, 100, std::nan(""), false));
    assert(format(1830, std::nullopt) == "7.62 s / --");
    // Corrupt saved values are ignored; the estimate never falls below elapsed time.
    assert(!validTicks(0) && !validTicks(-5) && !validTicks(maxTicks + 1));
    t = total(-1, 1000, 100., false);
    assert(t && !t->exact && t->ticks == 1000);
    return 0;
}
