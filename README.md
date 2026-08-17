# Web-Flasher

Inhalt der GitHub-Pages-Seite unter
<https://rubenmuehlhans.github.io/floor-heating-ctrl/>.

Dieser Zweig traegt nur, was der Browser laedt: die Seite, die beiden Manifeste
und die zusammengefuehrten Abbilder. Die Abbilder liegen hier und nicht bei der
Veroeffentlichung, weil deren Dateien ohne CORS-Kopfzeile ausgeliefert werden --
der Browser duerfte sie von dieser Seite aus nicht lesen.

Erzeugt aus dem Stand von `main`:

    idf.py -C apps/manifold   merge-bin -o firmware/floor-heating-ctrl-<version>-merged.bin
    idf.py -C apps/heatsource merge-bin -o firmware/heat-source-ctrl-<version>-merged.bin

Danach die Pfade in `manifest-verteiler.json` und `manifest-heizungsgeraet.json`
sowie die Versionsangabe in `index.html` nachziehen.
