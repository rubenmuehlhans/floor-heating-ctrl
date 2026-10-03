// swift-tools-version: 6.2
import PackageDescription

let package = Package(
    name: "Anlage",
    platforms: [.iOS("27.0"), .macOS("27.0")],
    products: [
        .library(name: "Anlage", targets: ["Anlage"]),
    ],
    dependencies: [
        .package(path: "../Geraeteschnittstelle"),
        .package(path: "../Verlauf"),
    ],
    targets: [
        .target(
            name: "Anlage",
            dependencies: ["Geraeteschnittstelle", "Verlauf"],
            swiftSettings: [.swiftLanguageMode(.v6)]
        ),
        .testTarget(
            name: "AnlageTests",
            dependencies: ["Anlage"],
            swiftSettings: [.swiftLanguageMode(.v6)]
        ),
    ]
)
