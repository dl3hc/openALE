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
    //   "hamlib:<model>:<port>[,<baud>][,dtr=on|off|auto][,rts=on|off|auto][,stab=<ms>][,ptt=normal|mic|data][,split=0|1][,ptt_type=cat|rts|dtr|none][,ptt_port=<device>]"
    // Format (TCP):
    //   "hamlib:2:tcp://<host>:<port>[,ptt=normal|mic|data][,split=0|1][,ptt_type=cat|rts|dtr|none][,ptt_port=<device>]"
    //
    // ptt_type/ptt_port select the PTT *mechanism* (hamlib ptt_type_t: CAT vs
    // a serial RTS/DTR line, possibly on a second device) — distinct from the
    // pre-existing ptt=normal|mic|data, which only selects the CAT-side audio
    // input (Mic/Data) used when ptt_type=cat. Default ptt_type=cat/ptt_port=""
    // (empty = share the CAT port's device) reproduces today's behavior exactly.
    //
    // ptt=mic|data selects the CAT PTT audio input (Kenwood TX0/TX1 etc.) —
    // only rigs with a Mic/Data distinction in Hamlib honor it; others fall
    // back to the plain PTT-ON command. Default "normal" = plain PTT ON.
    //
    // split=1 enables the relay-click workaround (SerialLinePolicy::avoid_relay_click).
    //
    // Beispiele:
    //   "hamlib:3021:COM3,9600,dtr=on,rts=on,stab=200,ptt=data,split=1"
    //   "hamlib:3021:COM3,9600"
    //   "hamlib:2:tcp://127.0.0.1:4532,ptt=data,split=1"
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
        SerialLinePolicy policy;      // Defaults: dtr=ON, rts=ON, stab=200
        PttPolicy        ptt_policy;  // Default: type=CAT, port=""

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
                    } else if (key == "split") {
                        policy.avoid_relay_click = (val == "1");
                    } else if (key == "ptt_type") {
                        if      (val == "rts")  ptt_policy.type = PttPolicy::Type::RTS;
                        else if (val == "dtr")  ptt_policy.type = PttPolicy::Type::DTR;
                        else if (val == "none") ptt_policy.type = PttPolicy::Type::NONE;
                        else                    ptt_policy.type = PttPolicy::Type::CAT;
                    } else if (key == "ptt_port") {
                        ptt_policy.port = val;
                    }
                }
                first_seg = false;
            }
        }

        return std::make_unique<HamlibRadio>(model, port, baud, policy, ptt_policy);
    }
#endif
    (void)config;
    return nullptr;
}

} // namespace pal
