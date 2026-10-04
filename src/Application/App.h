#pragma once

struct GLFWwindow;

namespace Application {

// Owns the window, GL context, ImGui state, and the Stage 1 simulation/UI
// loop. Kept intentionally thin — later stages add their own
// tracer/renderer objects here rather than growing this class's methods.
class App {
public:
    App();
    ~App();

    App(const App&) = delete;
    App& operator=(const App&) = delete;

    int Run();

private:
    GLFWwindow* m_window = nullptr;
};

} // namespace Application
