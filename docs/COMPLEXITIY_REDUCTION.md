Globale Agentenanweisung zur Vereinfachung der gesamten Codebasis für Open-Source, Ausbildung, Lehre und Funkamateure mit rudimentären Softwarekenntnissen.

**Mission**
1. Reduziere die kognitive Komplexität der gesamten Codebasis systematisch und iterativ.
2. Erhöhe Verständlichkeit, Nachvollziehbarkeit und Lernbarkeit ohne funktionale Regression.
3. Etabliere dauerhaft verbindliche Regeln, damit neue Beiträge die Einfachheit erhalten.

**Nicht verhandelbare Leitprinzipien**
1. Verständlichkeit vor Cleverness.
2. Eine Einheit, eine Aufgabe, ein klarer Zweck.
3. Öffentliche Schnittstellen einfach, interne Komplexität gekapselt.
4. Kleine, überprüfbare Änderungen statt großer Umbauten.
5. Jede Änderung muss durch Tests und klare Begründung abgesichert sein.
6. Dokumentation erklärt warum und wie, nicht nur was.
7. Keine funktionalen Änderungen ohne explizite Kennzeichnung und Begründung.

**Globale Qualitätsregeln**
1. Begrenze Komplexität pro Funktion, pro Datei und pro Modul auf klar definierte Grenzwerte.
2. Vermeide tiefe Verschachtelung; bevorzuge Guard Clauses und frühe Rückgaben.
3. Entferne Redundanz konsequent in Logik, Kommentaren und Dokumentation.
4. Benenne so, dass Einsteiger Intention ohne Kontextsprung verstehen.
5. Trenne fachliche Logik, Infrastruktur und Hilfslogik strikt.
6. Kommentare nur für Motivation, Randbedingungen und nicht offensichtliche Entscheidungen.
7. Historie, Diskussionen und Vergleiche gehören nicht in Produktionskommentare.
8. Fehlerbehandlung ist explizit, konsistent und für Lernende nachvollziehbar.
9. Öffentliche APIs bleiben stabil oder erhalten klaren Migrationspfad.
10. Jede neue Komponente folgt denselben Struktur- und Stilregeln.

**Regeln für Kommentare und Dokumentation**
1. Kommentare sind kurz, präzise und didaktisch wertvoll.
2. Entferne überladene Langkommentare aus dem Fließcode und überführe Kontextwissen in strukturierte Projektdokumentation.
3. Ergänze pro Themenbereich eine kurze Lernzusammenfassung: Zweck, Inputs, Outputs, typische Fehler.
4. Einheitliche Begriffe, keine synonymen Fachbegriffe ohne Glossar-Eintrag.
5. Jede wesentliche Designentscheidung wird in standardisierter Form dokumentiert.

**Regeln für API- und Architekturvereinfachung**
1. Definiere pro Fachbereich eine einfache Einstiegsschnittstelle für typische Standardfälle.
2. Kapsle Spezialfälle hinter klar benannten Erweiterungspunkten.
3. Vermeide Querverweise zwischen Modulen ohne expliziten Vertrag.
4. Reduziere sichtbare Optionen für Einsteiger auf sinnvolle Standardkonfiguration.
5. Erzwinge klare Schichten und gerichtete Abhängigkeiten.

**Regeln für Testbarkeit und Verifikation**
1. Jede Vereinfachung muss durch bestehende oder neue Tests abgesichert sein.
2. Tests priorisieren Nutzerverhalten und Lernpfade, nicht interne Details.
3. Für Refactorings gilt: Verhalten unverändert, Tests grün, Komplexität messbar gesenkt.
4. Regressionsschutz für zentrale Anwendungsfälle ist Pflicht.
5. Definiere messbare Qualitätsziele und prüfe sie in jeder Iteration.

**Iterativer Arbeitsablauf für den Coding Agent**
1. Baseline erfassen: Komplexitätsmetriken, Lesbarkeitsprobleme, Redundanzen, Testabdeckung.
2. Nächstes Refactoring-Paket klein und klar zuschneiden.
3. Vor jeder Änderung Ziel, Grenzen, Erfolgskriterien und Risiken formulieren.
4. Änderungen umsetzen mit Fokus auf Vereinfachung ohne Verhaltensänderung.
5. Tests und statische Prüfungen ausführen, Ergebnisse dokumentieren.
6. Nach jeder Iteration Metriken mit Baseline vergleichen.
7. Erkenntnisse in verbindliche Regeln überführen und Wiederholfehler ausschließen.
8. Nächste Iteration auf Basis der größten verbleibenden Verständlichkeitsbarriere planen.

**Definition of Done pro Iteration**
1. Verhalten unverändert oder Änderung explizit dokumentiert.
2. Alle relevanten Prüfungen erfolgreich.
3. Komplexität in den bearbeiteten Bereichen nachweislich reduziert.
4. Kommentare gestrafft, Redundanzen entfernt, Begriffe harmonisiert.
5. Lernbarkeit verbessert durch kurze, klare Erklärungen.
6. Änderungen sind klein genug, dass Dritte sie schnell reviewen können.

**Projektweite Governance-Regeln**
1. Jede Änderung muss eine kurze didaktische Begründung enthalten: Warum ist es jetzt einfacher.
2. Pull Requests ohne messbaren Vereinfachungseffekt werden abgelehnt.
3. Neue Komplexität ist nur mit technischer Begründung und klarer Kompensation erlaubt.
4. Stil-, Struktur- und Namensregeln sind verbindlich, nicht optional.
5. Regelverstöße werden automatisch geprüft und früh blockiert.

**Priorisierungslogik für den Agent**
1. Zuerst Bereiche mit höchster Lernhürde und größtem Einfluss auf Einsteiger.
2. Danach Bereiche mit hoher Änderungsfrequenz und Wartungsaufwand.
3. Zuletzt selten genutzte Spezialpfade.
4. Immer zuerst Vereinfachung mit geringem Risiko und hohem Nutzen umsetzen.

**Kommunikationsregeln für den Agent**
1. Vor jeder Iteration: Ziel, Umfang, Erfolgskriterien.
2. Nach jeder Iteration: Was vereinfacht wurde, welche Regeln entstanden sind, welche Metriken sich verbessert haben.
3. Keine langen narrativen Berichte; stattdessen kurze, vergleichbare Iterationsprotokolle.
4. Offene Risiken und nächste Schritte klar benennen.

Wenn du möchtest, formuliere ich das im nächsten Schritt als sofort ausführbare Agenten-Richtlinie mit festen Schwellenwerten für Komplexität, Dateigröße, Funktionsgröße und Review-Gates.