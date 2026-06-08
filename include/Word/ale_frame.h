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
#include <string>
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

/**
 * Baut Frame-Objekte für die sechs Basis-Rufstrukturen nach
 * MIL-STD-188-141B Figuren A-29(a)–(f).
 *
 * Figuren-Zuordnung:
 *   (a) 1-Kanal Nonscan, 1-Wort, Einzelruf:   leading_individual  + conclusion
 *   (b) N-Kanal Scanning, 1-Wort, Einzelruf:  scanning_individual + leading_individual + conclusion
 *   (c) 1-Kanal Nonscan, 2-Wort, Einzelruf:   leading_individual  + conclusion
 *   (d) N-Kanal Scanning, 2-Wort, Einzelruf:  scanning_individual + leading_individual + conclusion
 *   (e) N-Kanal Scanning, 1-Wort, Gruppenruf: scanning_group      + leading_group      + conclusion
 *   (f) N-Kanal Scanning, 2-Wort, Gruppenruf: scanning_group      + leading_group      + conclusion
 *
 * Dauern (Tsc, Tlc, Tcc, Tx) → ale_timing.h, calc_tsc_ms / calc_tlc_ms etc.
 * 1-Wort- vs. 2-Wort-Adressierung ergibt sich automatisch aus der Adresslänge.
 */
class ALEFrameBuilder {
public:
    /**
     * Scanning-Iteration, Einzelruf (Figuren b, d).
     *
     * Liefert die vollständige Adressfolge von dest mit TO-Anker:
     *   dest ≤ 3 Zeichen  →  [TO:dest]
     *   dest ≤ 6 Zeichen  →  [TO:dest_part1, DATA:dest_part2]
     *   …
     */
    static Frame scanning_individual(const std::string& dest);

    /**
     * Scanning-Iteration, Gruppenruf (Figuren e, f).
     *
     * Immer genau zwei Wörter, erste 3 Zeichen beider Adressen:
     *   [THRU:relay_first3, REP:dest_first3]
     */
    static Frame scanning_group(const std::string& relay, const std::string& dest);

    /**
     * Leading-Call-Abschnitt, Einzelruf (Figuren a, b, c, d).
     *
     * Vollständige Adressfolge × 2 (Tlc = 2 × Tc):
     *   1-Wort: [TO:dest, TO:dest]
     *   2-Wort: [TO:dest1, DATA:dest2, TO:dest1, DATA:dest2]
     */
    static Frame leading_individual(const std::string& dest);

    /**
     * Leading-Call-Abschnitt, Gruppenruf (Figuren e, f).
     *
     * Kurze Adressen (je ≤ 3 Zeichen, Figur e) — THRU-Anker:
     *   [THRU:relay, REP:dest,  THRU:relay, REP:dest]
     *
     * Lange Adressen (mind. eine > 3 Zeichen, Figur f) — TO-Anker:
     *   [TO:relay…, TO:dest…,  TO:relay…, TO:dest…]
     */
    static Frame leading_group(const std::string& relay, const std::string& dest);

    /**
     * Abschlussrahmen — alle Figuren (a–f).
     *
     *   [TIS:self]  (mehrere Wörter bei Rufzeichen > 3 Zeichen)
     */
    static Frame conclusion(const std::string& self);
};

} // namespace ale
