// Bootmenü für den Betrieb neben einer zweiten Firmware (nicht der Standardweg).
//
// Nur aktiv, wenn die Partitionstabelle eine factory-Partition mit dem Label „bootmenu“
// trägt (per Kabel geflasht). Ohne sie verhält sich die
// Tafel wie bisher. Erfüllt von geraet/bootmenu.cpp, in der Vorschau von host/plattform.cpp.
#pragma once

namespace bootmenu {

bool vorhanden();
// Beim Start: Startpartition auf das Menü setzen, damit jeder Neustart wieder dort landet.
void beim_start();
// Neustart ins Menü (Knopf „Bootmenü“ in den Einstellungen).
void neustart();

}  // namespace bootmenu
