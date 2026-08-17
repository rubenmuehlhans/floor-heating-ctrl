# Web-Flasher

Inhalt der GitHub-Pages-Seite unter
<https://rubenmuehlhans.github.io/floor-heating-ctrl/>.

Dieser Zweig traegt nur, was der Browser laedt: die Seite, die beiden Manifeste,
die zusammengefuehrten Abbilder und esp-web-tools. Die Abbilder liegen hier und nicht bei der
Veroeffentlichung, weil deren Dateien ohne CORS-Kopfzeile ausgeliefert werden --
der Browser duerfte sie von dieser Seite aus nicht lesen.

`vendor/esp-web-tools/` ist der Inhalt von `dist/web/` aus dem npm-Paket
`esp-web-tools` 10.4.0, Apache-2.0, samt Lizenztext. Mitgeliefert statt ueber ein
CDN eingebunden: Eine Seite, die Firmware auf Hardware schreibt, sollte nicht
davon abhaengen, dass ein fremder Rechner erreichbar bleibt und dieselben Bytes
ausliefert.

Erzeugt aus dem Stand von `main`:

    idf.py -C apps/manifold   merge-bin -o firmware/floor-heating-ctrl-<version>-merged.bin
    idf.py -C apps/heatsource merge-bin -o firmware/heat-source-ctrl-<version>-merged.bin

Danach die Pfade in `manifest-verteiler.json` und `manifest-heizungsgeraet.json`
sowie die Versionsangabe in `index.html` nachziehen.
