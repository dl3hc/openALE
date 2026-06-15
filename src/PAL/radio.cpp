// src/PAL/radio.cpp — create_radio() factory

#include "PAL/radio.h"
#ifdef HAVE_HAMLIB
#include "PAL/radios/hamlib_radio.h"
#endif
#include <memory>
#include <string>

namespace pal {

std::unique_ptr<IRadio> create_radio(const std::string& config)
{
#ifdef HAVE_HAMLIB
    // Format: "hamlib:<model_id>:<port>"
    // Examples: "hamlib:229:COM3"  "hamlib:229:tcp://127.0.0.1:4532"
    if (config.rfind("hamlib:", 0) == 0) {
        const std::string rest  = config.substr(7);
        const auto        colon = rest.find(':');
        if (colon == std::string::npos) return nullptr;
        return std::make_unique<HamlibRadio>(rest.substr(0, colon),
                                             rest.substr(colon + 1));
    }
#endif
    (void)config;
    return nullptr;
}

} // namespace pal
