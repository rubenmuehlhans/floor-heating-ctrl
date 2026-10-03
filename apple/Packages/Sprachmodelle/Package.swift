// swift-tools-version: 6.2
import PackageDescription

let package = Package(
    name: "Sprachmodelle",
    platforms: [.iOS("27.0"), .macOS("27.0")],
    products: [
        .library(name: "Sprachmodelle", targets: ["Sprachmodelle"]),
    ],
    targets: [
        .target(
            name: "Sprachmodelle",
            swiftSettings: [.swiftLanguageMode(.v6)]
        ),
        .testTarget(
            name: "SprachmodelleTests",
            dependencies: ["Sprachmodelle"],
            swiftSettings: [.swiftLanguageMode(.v6)]
        ),
    ]
)
