// swift-tools-version: 6.2
import PackageDescription

let package = Package(
    name: "Geraeteschnittstelle",
    platforms: [.iOS("27.0"), .macOS("27.0")],
    products: [
        .library(name: "Geraeteschnittstelle", targets: ["Geraeteschnittstelle"]),
    ],
    targets: [
        .target(
            name: "Geraeteschnittstelle",
            swiftSettings: [.swiftLanguageMode(.v6)]
        ),
        .testTarget(
            name: "GeraeteschnittstelleTests",
            dependencies: ["Geraeteschnittstelle"],
            resources: [.copy("Fixtures")],
            swiftSettings: [.swiftLanguageMode(.v6)]
        ),
    ]
)
