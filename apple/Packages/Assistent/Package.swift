// swift-tools-version: 6.2
import PackageDescription

let package = Package(
    name: "Assistent",
    platforms: [.iOS("27.0"), .macOS("27.0")],
    products: [
        .library(name: "Assistent", targets: ["Assistent"]),
    ],
    dependencies: [
        .package(path: "../Anlage"),
        .package(path: "../Diagnose"),
        .package(path: "../Geraeteschnittstelle"),
        .package(path: "../Sprachmodelle"),
        .package(path: "../Verlauf"),
    ],
    targets: [
        .target(
            name: "Assistent",
            dependencies: ["Anlage", "Diagnose", "Geraeteschnittstelle", "Verlauf"],
            swiftSettings: [.swiftLanguageMode(.v6)]
        ),
        .testTarget(
            name: "AssistentTests",
            dependencies: ["Assistent", "Sprachmodelle"],
            swiftSettings: [.swiftLanguageMode(.v6)]
        ),
    ]
)
