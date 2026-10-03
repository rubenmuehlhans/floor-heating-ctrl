// swift-tools-version: 6.2
import PackageDescription

let package = Package(
    name: "Verlauf",
    platforms: [.iOS("27.0"), .macOS("27.0")],
    products: [
        .library(name: "Verlauf", targets: ["Verlauf"]),
    ],
    dependencies: [
        .package(path: "../Geraeteschnittstelle"),
    ],
    targets: [
        .target(
            name: "Verlauf",
            dependencies: ["Geraeteschnittstelle"],
            swiftSettings: [.swiftLanguageMode(.v6)]
        ),
        .testTarget(
            name: "VerlaufTests",
            dependencies: ["Verlauf"],
            swiftSettings: [.swiftLanguageMode(.v6)]
        ),
    ]
)
