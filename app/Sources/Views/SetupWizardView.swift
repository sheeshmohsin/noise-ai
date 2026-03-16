import SwiftUI

struct SetupWizardView: View {
    @State private var currentStep = 0

    var body: some View {
        VStack(spacing: 20) {
            Text("Welcome to NoiseAI")
                .font(.title)

            Text("Setup wizard coming soon")
                .foregroundStyle(.secondary)

            Button("Get Started") {
                // TODO: Implement setup flow
            }
            .buttonStyle(.borderedProminent)
        }
        .frame(width: 500, height: 400)
        .padding()
    }
}

#Preview {
    SetupWizardView()
}
