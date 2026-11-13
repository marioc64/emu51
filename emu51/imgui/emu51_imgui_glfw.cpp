// SPDX-License-Identifier: GPL-2.0-only
#include "emu51_imgui_glfw.h"

#include <chrono>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_opengl3_loader.h"

#include "emu51_imgui_app.h"
#include "emu51_imgui_codicons.h"
#include "emu51_imgui_paths.h"
#include "emu51_app_icon.h"

namespace {

constexpr int kWindowWidth = 1280;
constexpr int kWindowHeight = 800;
constexpr const char* kWindowTitle = "emu51";
constexpr const char* kGlslVersion = "#version 130";

}  // namespace

int runEmu51ImguiGlfwApp()
{
	if (!glfwInit())
		return 1;

	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
#if defined(GLFW_X11_CLASS_NAME)
	glfwWindowHintString(GLFW_X11_CLASS_NAME, "emu51");
	glfwWindowHintString(GLFW_X11_INSTANCE_NAME, "emu51");
#endif
#if defined(GLFW_WAYLAND_APP_ID)
	glfwWindowHintString(GLFW_WAYLAND_APP_ID, "emu51");
#endif

	GLFWwindow* window = glfwCreateWindow(kWindowWidth, kWindowHeight, kWindowTitle, nullptr, nullptr);
	if (window == nullptr) {
		glfwTerminate();
		return 1;
	}

	const GLFWimage icons[] = {{32, 32, emu51_icon_32}, {64, 64, emu51_icon_64}};
	glfwSetWindowIcon(window, 2, icons);

	glfwMakeContextCurrent(window);
	glfwSwapInterval(0); // Redraw cadence is independent of CPU service cadence.

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	std::string iniPath = emu51GetImguiIniPath();
	io.IniFilename = iniPath.empty() ? nullptr : iniPath.c_str();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	ImGui::StyleColorsDark();
	io.Fonts->AddFontDefault();
	emu51LoadCodiconsFont(io);

	ImGui_ImplGlfw_InitForOpenGL(window, true);
	ImGui_ImplOpenGL3_Init(kGlslVersion);

	Emu51ImguiApp app;
	std::string windowTitle = app.windowTitle();
	glfwSetWindowTitle(window, windowTitle.c_str());
	bool firstFrame = true;

	using Clock = std::chrono::steady_clock;
	auto nextDraw = Clock::now();
	bool quit = false;
	while (!quit) {
		if (!firstFrame) {
			if (app.shouldAnimate())
				glfwWaitEventsTimeout(app.running() ? 1.0 / 240.0 : 1.0 / 60.0);
			else
				glfwWaitEvents();
			if (glfwWindowShouldClose(window)) {
				glfwSetWindowShouldClose(window, GLFW_FALSE);
				app.requestQuit();
			}
		}
		// Service CPU work independently of drawing, including minimized windows.
		app.tick();
		const auto now = Clock::now();
		if (!firstFrame && app.running() && now < nextDraw)
			continue;
		firstFrame = false;
		nextDraw = now + std::chrono::microseconds(16667);

		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();

		app.draw();
		const std::string newWindowTitle = app.windowTitle();
		if (newWindowTitle != windowTitle) {
			windowTitle = newWindowTitle;
			glfwSetWindowTitle(window, windowTitle.c_str());
		}
		if (app.consumeQuitRequested())
			quit = true;

		ImGui::Render();
		int displayWidth = 0;
		int displayHeight = 0;
		glfwGetFramebufferSize(window, &displayWidth, &displayHeight);
		glViewport(0, 0, displayWidth, displayHeight);
		glClearColor(0.09f, 0.10f, 0.12f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
		glfwSwapBuffers(window);
	}

	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();
	glfwDestroyWindow(window);
	glfwTerminate();
	return 0;
}
