// swift-tools-version: 6.2
import PackageDescription

let package = Package(
    name: "Geraetesuche",
    platforms: [.iOS("27.0"), .macOS("27.0")],
    products: [
        .library(name: "Geraetesuche", targets: ["Geraetesuche"]),
    ],
    dependencies: [
        .package(path: "../Geraeteschnittstelle"),
    ],
    targets: [
        .target(
            name: "Geraetesuche",
            dependencies: ["Geraeteschnittstelle"],
            swiftSettings: [.swiftLanguageMode(.v6)]
        ),
        .testTarget(
            name: "GeraetesucheTests",
            dependencies: ["Geraetesuche"],
            swiftSettings: [.swiftLanguageMode(.v6)]
        ),
    ]
)
