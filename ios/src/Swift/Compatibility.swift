import SwiftUI

/// Keep newer decoration behind availability checks. The iOS 16 fallback uses
/// system materials and controls, including their accessibility behavior.
extension View {
    @ViewBuilder
    func compatibleGlass<S: Shape>(in shape: S, tint: Color? = nil) -> some View {
        if #available(iOS 26.0, *) {
            if let tint {
                glassEffect(.regular.tint(tint), in: shape)
            } else {
                glassEffect(.regular, in: shape)
            }
        } else {
            background(.regularMaterial, in: shape)
                .background {
                    if let tint { shape.fill(tint) }
                }
        }
    }

    @ViewBuilder
    func compatibleGlassButton(prominent: Bool = false) -> some View {
        if #available(iOS 26.0, *) {
            if prominent {
                buttonStyle(.glassProminent)
            } else {
                buttonStyle(.glass)
            }
        } else {
            if prominent {
                buttonStyle(.borderedProminent)
            } else {
                buttonStyle(.bordered)
            }
        }
    }

    @ViewBuilder
    func compatibleBounce<Value: Equatable>(value: Value) -> some View {
        if #available(iOS 17.0, *) {
            symbolEffect(.bounce, value: value)
        } else {
            self
        }
    }
}

extension Animation {
    static func compatibilitySnappy(duration: Double = 0.5) -> Animation {
        if #available(iOS 17.0, *) {
            return .snappy(duration: duration)
        }
        return .easeInOut(duration: duration)
    }
}
