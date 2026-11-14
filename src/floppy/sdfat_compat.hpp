#include <chrono>

// SdFat's non-Arduino headers declare overloads for this Arduino type.
class __FlashStringHelper;

// SdFat uses millis() when generating short aliases for colliding long names.
inline unsigned long
millis()
{
    using namespace std::chrono;
    return static_cast<unsigned long>(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}
