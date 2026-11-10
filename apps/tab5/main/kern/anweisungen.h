// Systemanweisungen des Assistenten — wie apple/Packages/Assistent/Sources/Assistent/
// Anweisungen.swift, auf die Tafel bezogen. Der Text enthält keine Messwerte; alles
// Aktuelle kommt über die Werkzeuge. So bleibt er für den Prompt-Cache gleich.
#pragma once
#include <string>

namespace anweisungen {

// Rolle, Arbeitsweise, Werkzeuge und das Wissen (Handbuch und Konzepte).
const std::string &system();
// Auftrag des Lageberichts und sein Schema (output_config.format).
const char *lagebericht_auftrag();
const char *lagebericht_schema();

}  // namespace anweisungen

// Handbuch und Konzepte als ein Text in <dokument>-Hüllen; erfüllt geraet/assist.cpp
// (eingebettet) bzw. host/plattform.cpp (aus docs/).
std::string wissen_text();
