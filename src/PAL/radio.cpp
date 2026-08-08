// src/PAL/radio.cpp — create_radio() factory

#include "PAL/radio.h"
#ifdef HAVE_HAMLIB
#include "PAL/radios/hamlib_radio.h"
#endif
#include <memory>
#include <sstream>
#include <string>

namespace pal {

std::unique_ptr<IRadio> create_radio(const std::string& config)
{
#ifdef HAVE_HAMLIB
    // Format (serial):
    //   "hamlib:<model>:<port>[,<baud>][,dtr=on|off|auto][,rts=on|off|auto][,stab=<ms>][,ptt=normal|mic|data]"
    // Format (TCP):
    //   "hamlib:2:tcp://<host>:<port>[,ptt=normal|mic|data]"
    //
    // ptt=mic|data selects the CAT PTT audio input (Kenwood TX0/TX1 etc.) —
    // only rigs with a Mic/Data distinction in Hamlib honor it; others fall
    // back to the plain PTT-ON command. Default "normal" = plain PTT ON.
    //
    // Beispiele:
    //   "hamlib:3021:COM3,9600,dtr=on,rts=on,stab=200,ptt=data"
    //   "hamlib:3021:COM3,9600"
    //   "hamlib:2:tcp://127.0.0.1:4532,ptt=data"
    if (config.rfind("hamlib:", 0) == 0) {
        const std::string rest  = config.substr(7);
        const auto        colon = rest.find(':');
        if (colon == std::string::npos) return nullptr;

        const std::string model        = rest.substr(0, colon);
        const std::string port_and_rest = rest.substr(colon + 1);

        // TCP-Specs ("tcp://…") können ebenfalls Komma-Params tragen (z. B.
        // ptt=data); dtr/rts/stab werden dabei von apply_line_policy() über
        // is_serial_port() ignoriert.
        const auto first_comma = port_and_rest.find(',');
        const std::string port = first_comma == std::string::npos
                                  ? port_and_rest
                                  : port_and_rest.substr(0, first_comma);

        int baud = 0;
        SerialLinePolicy policy;  // Defaults: dtr=ON, rts=ON, stab=200

        if (first_comma != std::string::npos) {
            // Segmente hinter dem Port splitten: "9600,dtr=on,rts=on,stab=200"
            std::istringstream ss(port_and_rest.substr(first_comma + 1));
            std::string seg;
            bool first_seg = true;
            while (std::getline(ss, seg, ',')) {
                const auto eq = seg.find('=');
                if (eq == std::string::npos) {
                    // Kein '=' → numerische Baud-Rate (erstes Segment)
                    if (first_seg) {
                        try { baud = std::stoi(seg); } catch (...) { baud = 0; }
                    }
                } else {
                    const std::string key = seg.substr(0, eq);
                    const std::string val = seg.substr(eq + 1);
                    if (key == "dtr") {
                        if      (val == "off")  policy.dtr = SerialLinePolicy::State::OFF;
                        else if (val == "auto") policy.dtr = SerialLinePolicy::State::AUTO;
                        else                    policy.dtr = SerialLinePolicy::State::ON;
                    } else if (key == "rts") {
                        if      (val == "off")  policy.rts = SerialLinePolicy::State::OFF;
                        else if (val == "auto") policy.rts = SerialLinePolicy::State::AUTO;
                        else                    policy.rts = SerialLinePolicy::State::ON;
                    } else if (key == "stab") {
                        try { policy.stabilization_ms = static_cast<uint32_t>(std::stoul(val)); }
                        catch (...) {}
                    } else if (key == "ptt") {
                        if      (val == "mic")  policy.ptt_input = SerialLinePolicy::PttInput::MIC;
                        else if (val == "data") policy.ptt_input = SerialLinePolicy::PttInput::DATA;
                        else                    policy.ptt_input = SerialLinePolicy::PttInput::NORMAL;
                    }
                }
                first_seg = false;
            }
        }

        return std::make_unique<HamlibRadio>(model, port, baud, policy);
    }
#endif
    (void)config;
    return nullptr;
}

} // namespace pal
