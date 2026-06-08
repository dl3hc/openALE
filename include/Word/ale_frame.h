/**
 * \file ale_frame.h
 * \brief Sendebereite Folge von ALE-Wörtern (protokollseitige Ebene).
 *
 * Frame arbeitet auf ALEWord-Ebene (24-bit logische Wörter).
 * encode() überführt jeden ALEWord in ein 49-bit-Übertragungswort
 * (Golay + Interleaving per A.5.2.2.2/A.5.2.2.3).
 * ALE2GModem::enqueue_frame() ruft encode() auf und übergibt das Ergebnis
 * wortweise an den internen Sendepuffer.
 *
 * Bit-Konvention der 49-bit-Wörter (A.5.2.2.3, MSB-first):
 *   bit  0 = A1  (W1, erstes gesendetes Bit)
 *   bit 48 = S49 (Stuff Bit = 0, letztes gesendetes Bit)
 *
 * Das Modem sendet jedes 49-bit-Wort physikalisch 3× (A.5.2.2.4) —
 * diese Redundanz ist für Frame vollständig unsichtbar.
 *
 * Protokollseitige Wiederholungen (z. B. Tlc = 2 × Tc beim Leading Call)
 * werden beim Aufbau durch ALEFrameBuilder einmalig materialisiert;
 * Frame kennt keine Semantik seiner Wörter.
 *
 * Specification: MIL-STD-188-141B Appendix A
 */

#pragma once

#include "Word/ale_word.h"
#include <vector>
#include <cstdint>

namespace ale {

class Frame {
public:
    Frame() = default;
    explicit Frame(std::vector<ALEWord> words) : words_(std::move(words)) {}

    /**
     * Alle Wörter als 49-bit-Übertragungswörter kodieren.
     *
     * Für jedes ALEWord:
     *   1. 24-bit-Wort [preamble(3) | payload(21)] aus type und raw_payload
     *   2. Golay (24,12) auf obere Hälfte W1..W12 (Coder A) und
     *      untere Hälfte W13..W24 (Coder B, Parität invertiert) — A.5.2.2.2
     *   3. A/B-Interleaving [A1,B1, A2,B2, …, A24,B24] + S49=0 — A.5.2.2.3
     *
     * \return Vektor mit size() Einträgen; jeder Eintrag ist ein 49-bit-Wort
     *         (bit 0 = A1 = erstes gesendetes Bit, bit 48 = S49 = 0).
     */
    std::vector<uint64_t> encode() const;

    const std::vector<ALEWord>& words() const { return words_; }
    size_t size()  const { return words_.size(); }
    bool   empty() const { return words_.empty(); }

private:
    std::vector<ALEWord> words_;
};

} // namespace ale
