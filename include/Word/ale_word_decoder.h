/**
 * \file ale_word_decoder.h
 * \brief Zustandsloser ALE-Wortdekoder: ALEWord → strukturiertes WordEvent.
 */

#pragma once
#include <string>
#include "Word/ale_word.h"

namespace ale {

/**
 * \struct WordEvent
 * Strukturiertes Ergebnis der Wortdekodierung.
 */
struct WordEvent {
    enum class Type {
        NONE,
        TO_SELF,          ///< TO oder TWAS, Adresse == eigene Adresse
        TIS_CALLER,       ///< Erste TIS (Conclusion-Beginn vom Gegenüber)
        DATA_EXTENSION,   ///< DATA/REP nach TIS → mehrteilige Adresse
        TWAS_REJECTION,   ///< TWAS nicht an uns → Rufablehnung
        CHANNEL_BUSY,     ///< Gültiges Wort während LBT-Phase
        ALLCALL,          ///< AllCall (A.5.5.4.4): TO an Wildcard-Adresse @?@ / @A@
    };
    Type        type    = Type::NONE;
    std::string address; ///< Bereinigtes Adressfeld (trim_ale_address)
};

/**
 * \struct WordDecodeContext
 * Kontext, den die SM vor dem Aufruf befüllt.
 * Kein Zustand — nur Schnappschuss der aktuell relevanten SM-Felder.
 */
struct WordDecodeContext {
    std::string self_address;           ///< Eigene Adresse (3 Zeichen, getrimmt)
    std::string expected_caller;        ///< Erwarteter Rufer (3-Zeichen-Prefix) — leer = beliebig
    bool        lbt_active            = false; ///< true = CHANNEL_CHECK läuft
    bool        collecting_conclusion = false; ///< true = TIS schon empfangen, DATA/REP = Verlängerung
};

/**
 * \class ALEWordDecoder
 * Reine Funktion: kein Zustand, keine Callbacks.
 * Kann in Unit-Tests ohne SM-Infrastruktur instanziiert werden.
 */
class ALEWordDecoder {
public:
    WordEvent decode(const ALEWord& word, const WordDecodeContext& ctx) const;
};

} // namespace ale
