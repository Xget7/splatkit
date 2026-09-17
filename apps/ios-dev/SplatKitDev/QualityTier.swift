import Foundation

/// Live quality levels. Each changes only settings that apply without reloading the world.
/// The splat limit is what moves the frame rate on a large hierarchy world; the error
/// threshold is the finest detail a level asks for when the limit leaves room.
enum QualityTier: String, CaseIterable, Identifiable {
    case ultra, high, balanced, fast

    var id: String { rawValue }

    var title: String {
        switch self {
        case .ultra: "Ultra"
        case .high: "High"
        case .balanced: "Balanced"
        case .fast: "Fast"
        }
    }

    /// Fraction of the view's resolution the splats are drawn at.
    var renderScale: Float {
        switch self {
        case .ultra, .high: 1
        case .balanced: 0.9
        case .fast: 0.75
        }
    }

    /// Requested harmonics; the view caps it at what the world carries.
    var shDegree: Int { self == .fast ? 0 : 3 }

    /// Screen error, in pixels, a hierarchy node may cover before it splits into its
    /// children. Lower draws more splats, up to the budget the world was loaded with.
    var lodErrorPixels: Float {
        switch self {
        case .ultra: 0.4
        case .high: 0.6
        case .balanced: 1
        case .fast: 1.6
        }
    }

    /// Most hierarchy splats a frame selects, 0 for everything the world loaded with.
    var lodSplatLimit: UInt32 {
        switch self {
        case .ultra: 0
        case .high: 2_800_000
        case .balanced: 2_000_000
        case .fast: 1_300_000
        }
    }

    /// 32-bit keys keep exact depth order; 16-bit halves the radix passes.
    var sortDepth: UInt32 { self == .ultra ? 32 : 16 }

    /// Smallest splat footprint kept, in pixels; lower keeps more fine splats.
    /// Applies under --metal-culling 1.
    var subpixelThreshold: Float {
        switch self {
        case .ultra: 0.2
        case .high: 0.3
        case .balanced: 0.5
        case .fast: 0.8
        }
    }

    func apply(to view: SplatMetalView) {
        view.renderScale = renderScale
        view.shDegree = shDegree
        var policy = view.renderPolicy
        policy.sortDepth = sortDepth
        policy.lodErrorPixels = lodErrorPixels
        policy.lodSplatLimit = lodSplatLimit
        policy.subpixelThreshold = subpixelThreshold
        view.renderPolicy = policy
    }
}
