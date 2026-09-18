import SwiftUI

/// The live render card: frame rate with its recent history, then what the frame costs.
/// Frame rate and tails count frames the display showed, not frames submitted.
struct RenderStatsCard: View {
    @ObservedObject var session: SplatSession

    var body: some View {
        let s = session.stats
        // The engine skips frames while nothing moves: no frame shown is a still view, not 0 fps.
        let idle = session.isIdle
        VStack(alignment: .leading, spacing: 6) {
            HStack(alignment: .center, spacing: 8) {
                VStack(alignment: .leading, spacing: 0) {
                    HStack(alignment: .firstTextBaseline, spacing: 4) {
                        // Digits roll between readings; Idle replaces them outright, and no other
                        // text in the card animates, so an old value never shows through a new one.
                        Text(idle ? "Idle" : String(format: "%.0f", s.fps))
                            .font(.system(size: 22, weight: .semibold, design: .rounded))
                            .monospacedDigit()
                            .contentTransition(.numericText())
                            .animation(.easeOut(duration: 0.25), value: s.fps)
                            .id(idle)
                            .transition(.identity)
                        if !idle {
                            Text("FPS")
                                .font(.system(size: 9, weight: .bold, design: .rounded))
                                .foregroundStyle(.secondary)
                        }
                    }
                    HStack(spacing: 5) {
                        Circle().fill(idle ? Color.gray : fpsColor(s.fps)).frame(width: 5, height: 5)
                        Text(idle ? "no redraw" : String(format: "%.1f ms", s.frameMillis))
                            .font(.system(size: 9, weight: .medium))
                            .monospacedDigit()
                            .foregroundStyle(.secondary)
                    }
                }
                FpsSparkline(samples: session.fpsHistory)
                    .frame(width: 56, height: 24)
            }

            Divider().overlay(Color.white.opacity(0.15))

            VStack(alignment: .leading, spacing: 3) {
                StatRow(label: "p95 / 1% low", value: s.presentTiming
                        ? String(format: "%.0f ms / %.0f", s.frameMillisP95, s.lowFps) : "-")
                StatRow(label: "Drawn", value: compact(s.drawnSplatCount))
                StatRow(label: "Scene", value: compact(s.loadedSplatCount))
                StatRow(label: "GPU/sort", value: String(format: "%.0f/%.0f ms", s.gpuMillis, s.sortMillis))
                StatRow(label: "Res", value: session.resolutionText)
                StatRow(label: "Pipe", value: session.pipelineText)
                StatRow(label: "Thermal", value: session.thermalText)
            }
        }
        .foregroundStyle(.white)
        .padding(.horizontal, 9)
        .padding(.vertical, 7)
        .frame(width: 148, alignment: .leading)
        .background(.ultraThinMaterial.opacity(0.75), in: RoundedRectangle(cornerRadius: 12, style: .continuous))
        .overlay(RoundedRectangle(cornerRadius: 12, style: .continuous).strokeBorder(Color.white.opacity(0.1)))
        .environment(\.colorScheme, .dark)
        .allowsHitTesting(false)
        .accessibilityIdentifier("splat.stats")
    }

    private func fpsColor(_ fps: Float) -> Color {
        fps >= 30 ? .green : fps >= 20 ? .yellow : .red
    }

    /// 2,022,194 reads as 2.02M; the card is too narrow for full counts.
    private func compact(_ value: Int) -> String {
        value >= 1_000_000 ? String(format: "%.2fM", Double(value) / 1_000_000)
            : value >= 1_000 ? String(format: "%.0fK", Double(value) / 1_000) : "\(value)"
    }
}

private struct StatRow: View {
    let label: String
    let value: String

    var body: some View {
        HStack(spacing: 4) {
            Text(label)
                .foregroundStyle(.secondary)
            Spacer(minLength: 4)
            Text(value)
                .monospacedDigit()
                .lineLimit(1)
                .minimumScaleFactor(0.7)
        }
        .font(.system(size: 9.5, weight: .medium))
    }
}

/// Recent frame rate, scaled to 60 FPS, with a faint 30 FPS guide.
private struct FpsSparkline: View {
    let samples: [Float]

    var body: some View {
        GeometryReader { geometry in
            let size = geometry.size
            let point = { (index: Int, fps: Float) -> CGPoint in
                let x = samples.count > 1 ? size.width * CGFloat(index) / CGFloat(samples.count - 1) : 0
                let y = size.height * (1 - CGFloat(min(max(fps, 0), 60) / 60))
                return CGPoint(x: x, y: y)
            }
            ZStack {
                Path { path in
                    path.move(to: CGPoint(x: 0, y: size.height / 2))
                    path.addLine(to: CGPoint(x: size.width, y: size.height / 2))
                }
                .stroke(Color.white.opacity(0.15), style: StrokeStyle(lineWidth: 1, dash: [3, 3]))
                if samples.count > 1 {
                    Path { path in
                        path.move(to: CGPoint(x: 0, y: size.height))
                        for (index, fps) in samples.enumerated() { path.addLine(to: point(index, fps)) }
                        path.addLine(to: CGPoint(x: size.width, y: size.height))
                        path.closeSubpath()
                    }
                    .fill(LinearGradient(colors: [Color.green.opacity(0.35), Color.green.opacity(0)],
                                         startPoint: .top, endPoint: .bottom))
                    Path { path in
                        for (index, fps) in samples.enumerated() {
                            index == 0 ? path.move(to: point(index, fps)) : path.addLine(to: point(index, fps))
                        }
                    }
                    .stroke(Color.green, style: StrokeStyle(lineWidth: 1.5, lineCap: .round, lineJoin: .round))
                }
            }
        }
    }
}

/// A row of choices in one glass capsule; the selection slides between them.
struct CapsulePicker<Item: Identifiable & Hashable>: View {
    let items: [Item]
    @Binding var selection: Item?
    let title: (Item) -> String
    @Namespace private var highlight

    var body: some View {
        HStack(spacing: 2) {
            ForEach(items) { item in
                let selected = item == selection
                Button {
                    withAnimation(.spring(response: 0.32, dampingFraction: 0.82)) { selection = item }
                } label: {
                    Text(title(item))
                        .font(.system(size: 11, weight: .semibold, design: .rounded))
                        .foregroundStyle(selected ? Color.black : Color.white)
                        .padding(.horizontal, 10)
                        .padding(.vertical, 5)
                        .background {
                            if selected {
                                Capsule().fill(Color.white).matchedGeometryEffect(id: "highlight", in: highlight)
                            }
                        }
                        .contentShape(Capsule())
                }
                .buttonStyle(.plain)
                .accessibilityIdentifier("picker.\(title(item).lowercased())")
            }
        }
        .padding(3)
        .background(.ultraThinMaterial.opacity(0.75), in: Capsule())
        .overlay(Capsule().strokeBorder(Color.white.opacity(0.12)))
        .environment(\.colorScheme, .dark)
        .sensoryFeedback(.selection, trigger: selection)
    }
}
