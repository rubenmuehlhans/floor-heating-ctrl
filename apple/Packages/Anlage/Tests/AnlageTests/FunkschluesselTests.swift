import Testing
@testable import Anlage

@Suite("Schlüssel verschlüsselter Thermometer")
struct FunkschluesselTests {
    let schluessel = "00112233445566778899aabbccddeeff"

    @Test(arguments: [
        "00112233445566778899aabbccddeeff",
        "00112233445566778899AABBCCDDEEFF",
        "00 11 22 33 44 55 66 77 88 99 aa bb cc dd ee ff",
        "00112233-44556677-8899aabb-ccddeeff",
        "  00112233445566778899aabbccddeeff\n",
    ])
    func wirdGelesen(_ eingabe: String) {
        #expect(Funkschluessel.normalisiert(eingabe) == schluessel)
    }

    @Test(arguments: [
        "", "00112233445566778899aabbccddeef", "00112233445566778899aabbccddeeff08",
        "00112233445566778899aabbccddeexy", "Schlüssel",
    ])
    func wirdAbgewiesen(_ eingabe: String) {
        #expect(Funkschluessel.normalisiert(eingabe) == nil)
    }

    @Test func adresse() {
        #expect(Funkschluessel.adresse("c0:ff:ee:12:34:56") == "C0:FF:EE:12:34:56")
        #expect(Funkschluessel.adresse("C0FFEE123456") == "C0:FF:EE:12:34:56")
        #expect(Funkschluessel.adresse("c0-ff-ee-12-34-56 ") == "C0:FF:EE:12:34:56")
        #expect(Funkschluessel.adresse("C0:FF:EE:12:34") == nil)
        #expect(Funkschluessel.adresse("C0:FF:EE:12:34:5G") == nil)
    }

    @Test func standDerEingabe() {
        #expect(Funkschluessel.stand("0011 22") == "6 von 32 Zeichen")
        #expect(Funkschluessel.stand(schluessel + "00") == "34 Zeichen, das sind 2 zu viel.")
        #expect(Funkschluessel.stand("00fg").hasPrefix("Erlaubt sind nur"))
    }
}
