// swift-tools-version: 6.2
import PackageDescription

let package = Package(
    name: "Diagnose",
    platforms: [.iOS("27.0"), .macOS("27.0")],
    products: [
        .library(name: "Diagnose", targets: ["Diagnose"]),
    ],
    dependencies: [
        .package(path: "../Anlage"),
        .package(path: "../Verlauf"),
    ],
    targets: [
        .target(
            name: "Diagnose",
            dependencies: ["Anlage", "Verlauf"],
            swiftSettings: [.swiftLanguageMode(.v6)]
        ),
        .testTarget(
            name: "DiagnoseTests",
            dependencies: ["Diagnose"],
            swiftSettings: [.swiftLanguageMode(.v6)]
        ),
    ]
)
