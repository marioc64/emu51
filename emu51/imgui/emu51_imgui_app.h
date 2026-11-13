// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <fstream>
#include <iosfwd>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "imgui.h"
#include "emu51.h"
#include "assembler.h"
#include "emu51_cycle_timing.h"
#include "emu51_execution_settings.h"
#include "emu51_project.h"
#include "emu51_imgui_menu.h"

class TextEditor;
class Emu51EditorRecovery;
namespace emu51_asm {
struct Diagnostic;
}

class Emu51ImguiApp : public Emu51ImguiMenuTarget {
public:
	Emu51ImguiApp();
	~Emu51ImguiApp();

	void draw();
	void tick();
	bool shouldAnimate() const;
	bool consumeQuitRequested();
	bool prepareToQuit();
	void requestOpenProgram() override;
	void requestOpenProject() override;
	void saveProject() override;
	void saveProjectAs() override;
	int recentProjectFileCount() const override;
	const std::string& recentProjectFilePath(int index) const override;
	void openRecentProjectFile(int index) override;
	void clearRecentProjectFiles() override;
	void requestLoadProgram() override;
	bool hasLoadedProgram() const override;
	void reloadProgram() override;
	int recentProgramFileCount() const override;
	const std::string& recentProgramFilePath(int index) const override;
	void openRecentProgramFile(int index) override;
	void clearRecentProgramFiles() override;
	bool canSaveAssemblyEditorFile() const override;
	void saveAssemblyEditorFile() override;
	void saveAssemblyEditorFileAs() override;
	void requestQuit() override;
	std::string windowTitle() const;
	bool canUseAssemblyEditorTextCommands() const override;
	bool canUndoAssemblyEditorText() const override;
	bool canRedoAssemblyEditorText() const override;
	bool canFormatAssemblyEditorText() const override;
	void selectAllAssemblyEditorText() override;
	void undoAssemblyEditorText() override;
	void redoAssemblyEditorText() override;
	void copyAssemblyEditorText() override;
	void cutAssemblyEditorText() override;
	void pasteAssemblyEditorText() override;
	void formatAssemblyEditorText() override;
	bool hasAssemblyEditorSource() const override;
	bool hasCompiledAssemblyEditorOutput() const override;
	void compileAssemblyEditor() override;
	void loadCompiledAssemblyEditorOutput() override;
	void compileAndLoadAssemblyEditor() override;
	bool running() const override;
	std::string executionTimingStatus() const override;
	void toggleRun() override;
	void resetCpuState(bool stopRunning) override;
	void requestSetProgramCounter() override;
	void stepIn() override;
	void stepOver() override;
	void stepOut() override;
	void requestExecutionSettings() override;
	void showDebugger() override;
	void showSerialTerminal() override;
	bool unifiedMode() const override;
	void setUnifiedMode(bool unifiedMode) override;
	bool internalRamMonitorVisible() const override;
	void setInternalRamMonitorVisible(bool visible) override;
	bool assemblyEditorVisible() const override;
	void setAssemblyEditorVisible(bool visible) override;
	void createDisassemblyView() override;
	void createDataMemoryMonitor() override;
	void createProgramMemoryMonitor() override;
	void clearAllMemory() override;
	void clearRamOnly() override;
	void clearRomOnly() override;
	void requestFillRamRange() override;
	void requestFillRomRange() override;
	bool hasActivePane() const override;
	void snapActivePane(Emu51ImguiSnapTarget target) override;
	void resetSnappedPanes() override;

	struct DisassemblyView {
		int id = 0;
		bool open = true;
		bool followPc = true;
		std::uint16_t start = 0;
		std::uint16_t end = 0;
		int keyScrollDirection = 0;
		float keyScrollRepeatTimer = 0.0f;
		bool openGotoPopup = false;
		char gotoAddress[128] = "";
	};

	struct MonitorView {
		enum class MemoryKind {
			InternalRam,
			ExternalData,
			ExternalProgram,
		};

		int id = 0;
		bool open = true;
		MemoryKind memoryKind = MemoryKind::InternalRam;
		std::uint16_t start = 0;
		int keyScrollDirection = 0;
		float keyScrollRepeatTimer = 0.0f;
		bool openGotoPopup = false;
		char gotoAddress[5] = "";
	};

	struct FillRangeDialog {
		bool open = false;
		bool fillRom = false;
		char start[5] = "0000";
		char end[5] = "0000";
		char value[3] = "00";
	};

	struct ExecutionSettingsDialog {
		bool open = false;
		int stepsPerFrame = 500;
		bool cycleExact = true;
		double crystalMHz = Emu51CycleTiming::kDefaultCrystalMHz;
		bool saveFailed = false;
	};

	struct SetProgramCounterDialog {
		bool open = false;
		bool focusInput = false;
		char value[5] = "0000";
	};

	struct BinaryLoadDialog {
		bool open = false;
		bool focusAddressInput = false;
		bool resetCpuBeforeLoad = false;
		std::string path;
		char address[5] = "0000";
	};

	struct AssemblyEditorState {
		bool open = false;
		bool dirty = false;
		bool buildStatusOpen = false;
		bool buildStatusSuccess = true;
		std::string sourcePath;
		std::string sourceName = "untitled.asm";
		std::optional<emu51_asm::AssemblyImage> compiledImage;
		std::string buildStatusTitle;
		std::string buildStatusDetail;
	};

public:
		struct PaneKey {
			enum class Kind {
				Registers,
				AssemblyEditor,
				Disassembly,
				InternalRamMonitor,
				Monitor,
		};

		Kind kind = Kind::Registers;
		int id = 0;

		bool operator==(const PaneKey& other) const
		{
			return kind == other.kind && id == other.id;
		}
	};

	struct PaneSnapAssignment {
		PaneKey pane;
		Emu51ImguiSnapTarget target = Emu51ImguiSnapTarget::None;
		float share = 1.0f;
	};

	struct SnapLayoutPreferences {
		float leftWidthFraction = 0.24f;
		float rightWidthFraction = 0.24f;
		float topHeightFraction = 0.22f;
		float bottomHeightFraction = 0.22f;
	};

private:
	enum class WatchSpace { Internal, Data, Program };
	struct Watchpoint {
		WatchSpace space = WatchSpace::Data;
		std::uint16_t address = 0;
		bool read = false;
		bool write = true;
		bool enabled = true;
	};
	struct TraceEntry {
		std::uint64_t instruction = 0;
		std::uint16_t pc = 0, nextPc = 0;
		std::array<std::uint8_t, 3> bytes{};
		std::uint8_t cycles = 0, acc = 0, psw = 0, sp = 0;
	};
	std::set<std::uint16_t> breakpoints_;
	std::vector<Watchpoint> watchpoints_;
	std::deque<TraceEntry> trace_;
	static constexpr std::size_t kTraceCapacity = 4096;
	bool traceEnabled_ = false;
	bool debuggerOpen_ = false;
	bool instructionExecuting_ = false;
	std::optional<std::uint16_t> skipBreakpoint_;
	std::optional<std::uint16_t> runToAddress_;
	std::string pendingWatchpointStop_;
	std::map<std::uint16_t, emu51_asm::AssemblyImage::SourceLine> sourceLines_;
	std::map<std::string, emu51_asm::Symbol> loadedSymbols_;
	std::map<std::uint16_t, std::string> loadedLabels_;
	std::map<std::string, std::string> loadedSources_;
	using SourceKey = std::pair<std::string, std::size_t>;
	std::map<SourceKey, std::uint16_t> sourceAddresses_;
	std::map<SourceKey, std::vector<std::uint16_t>> sourceLineBytes_;
	std::set<std::string> symbolWatches_;
	bool editorMatchesLoadedSource_ = false;
	std::string editorDebugSourceName_;
	std::string editorDebugSourceKey_;
	std::vector<emu51_asm::Diagnostic> editorDiagnostics_;
	char debugAddress_[128] = "0000";
	char symbolFilter_[128] = "";
	int watchSpace_ = 1;
	bool watchRead_ = false, watchWrite_ = true;
	void drawDebugger();
	void drawSerialTerminal();
	bool sendSerialTerminalInput();
	void requestSerialFileSend();
	bool startSerialFileSend(const std::filesystem::path& path);
	void serviceSerialFileSend();
	void cancelSerialFileSend();
	void collectSerialOutput();
	std::string serialTerminalDisplay() const;
	bool serialTerminalOpen_ = false, serialHexInput_ = false, serialHexOutput_ = false, serialRxNinth_ = false;
	int serialLineEnding_ = 0;
	char serialTerminalInput_[1024] = "";
	std::string serialTerminalStatus_;
	std::deque<emu51::SerialByte> serialTerminalOutput_;
	std::uint64_t serialOutputGeneration_ = 0, serialDrawnGeneration_ = 0;
	std::function<std::filesystem::path()> serialFilePathPicker_;
	std::ifstream serialFileStream_;
	std::filesystem::path serialFilePath_;
	std::uintmax_t serialFileSize_ = 0, serialFileQueued_ = 0;
	bool serialFileNinth_ = false;
	// 0: unchanged, 1: LF, 2: CR, 3: CRLF. Capture the selection per transfer.
	int serialFileLineEnding_ = 0, serialFileActiveLineEnding_ = 0;
	bool serialFileSkipLf_ = false;
	void toggleBreakpoint(std::uint16_t address);
	void runToAddress(std::uint16_t address);
	void runToEditorCursor();
	void showSource(std::uint16_t address);
	bool stopBeforeInstruction();
	void stopForDebugger(const std::string& reason);
	void noteMemoryAccess(WatchSpace space, std::uint16_t address, bool write);
	void setLoadedSourceMap(const emu51_asm::AssemblyImage& image);
	void clearLoadedDebugInfo();
	void invalidateSourceLocation(std::uint16_t address);
	std::optional<std::uint16_t> resolveDebugAddress(const char* text) const;
	std::string labelAtAddress(std::uint16_t address) const;
	std::optional<unsigned> symbolValue(const std::string& name) const;
	std::string editorSourceKey() const;
	void updateEditorSourceMatch();
	std::optional<std::uint16_t> editorLineAddress(std::size_t line) const;
	void toggleEditorBreakpoint(std::size_t line);
	void configureEditorDebugger();
	struct StepTask {
		std::uint16_t returnAddress = 0;
		std::uint8_t returnSp = 0;
		unsigned instructions = 0;
	};
	std::optional<StepTask> stepTask_;
	void tickAt(Emu51CycleTiming::Clock::time_point now);
	friend struct Emu51ImguiAppTestAccess;
	// Headless integration tests disable user preferences and recovery I/O.
	explicit Emu51ImguiApp(bool persistState);
	enum class UnsavedChoice { Save, Discard, Cancel };
	enum class RecoveryChoice { Restore, Discard, Keep };
	std::function<UnsavedChoice()> unsavedChangesPrompt_;
	std::function<RecoveryChoice(const std::string&)> recoveryPrompt_;
	std::function<std::filesystem::path()> savePathPicker_;
	bool persistState_ = true;
	bool quitApproved_ = false;
	bool recoveryCheckPending_ = false;
	bool recoverySavePending_ = false;
	Emu51CycleTiming::Clock::time_point recoverySaveDue_{};
	std::unique_ptr<Emu51EditorRecovery> editorRecovery_;
	std::string observedAssemblyEditorText_;
	std::string untitledSourceBaseDirectory_;
	std::string projectPath_;
	std::vector<std::string> recentProjectPaths_;
	void loadRecentProjectPaths();
	void loadRecentProjectPaths(std::istream& in);
	void saveRecentProjectPaths() const;
	void saveRecentProjectPaths(std::ostream& out) const;
	void rememberRecentProjectPath(const std::filesystem::path& path);
	std::vector<Emu51ProjectProgram> projectPrograms_;
	std::function<std::filesystem::path(bool save)> projectPathPicker_;
	std::filesystem::path chooseProjectPath(bool save);
	bool saveProjectToPath(const std::filesystem::path& path);
	bool openProjectFromPath(const std::filesystem::path& path);
	bool confirmAssemblyEditorChanges();
	bool saveAssemblyEditorChanges();
	void markAssemblyEditorChanged();
	void trackAssemblyEditorChanges();
	void saveEditorRecovery();
	void recoverAssemblyEditor(const std::filesystem::path& root);
	void clearEditorRecovery();
	static constexpr int kMemoryBytes = 0x10000;

	struct PaneWindowBinding {
		PaneKey pane;
		ImGuiID windowId = 0;
	};

	struct FloatingPaneState {
		PaneKey pane;
		ImVec2 size = ImVec2(0.0f, 0.0f);
		bool hasSize = false;
		bool restorePending = false;
	};

	struct SnappedPaneRect {
		PaneKey pane;
		float x = 0.0f;
		float y = 0.0f;
		float width = 0.0f;
		float height = 0.0f;
	};

	struct SnapSplitter {
		enum class Kind {
			SlotBoundary,
			PaneBoundary,
		};

		Kind kind = Kind::SlotBoundary;
		Emu51ImguiSnapTarget target = Emu51ImguiSnapTarget::None;
		PaneKey firstPane{};
		PaneKey secondPane{};
		float x = 0.0f;
		float y = 0.0f;
		float width = 0.0f;
		float height = 0.0f;
		bool vertical = false;
	};

	std::array<std::uint8_t, kMemoryBytes> ramMemory_{};
	std::array<std::uint8_t, kMemoryBytes> romMemory_{};
	std::array<std::uint8_t, kMemoryBytes> unifiedMemory_{};
	emu51 cpu_;
	std::string loadedProgramPath_;
	std::uint16_t loadedProgramBinaryAddress_ = 0;
	std::vector<std::string> recentProgramPaths_;
	std::string statusMessage_;
	std::uint64_t instructionCount_ = 0;
	std::uint64_t cycleCount_ = 0;
	int stepsPerFrame_ = 500;
	bool cycleExact_ = true;
	double crystalMHz_ = Emu51CycleTiming::kDefaultCrystalMHz;
	Emu51CycleTiming cycleTiming_;
	bool running_ = false;
	bool quitRequested_ = false;
	bool unifiedMode_ = true;
	bool virtualScrollActive_ = false;
	int nextDisassemblyViewId_ = 1;
	int nextMonitorViewId_ = 1;
	std::vector<DisassemblyView> disassemblyViews_;
	MonitorView internalRamMonitor_{};
	std::vector<MonitorView> monitorViews_;
	FillRangeDialog fillRangeDialog_;
	ExecutionSettingsDialog executionSettingsDialog_;
	SetProgramCounterDialog setProgramCounterDialog_;
	BinaryLoadDialog binaryLoadDialog_;
	std::unique_ptr<TextEditor> assemblyEditor_;
	AssemblyEditorState assemblyEditorState_;
	std::vector<PaneSnapAssignment> paneSnapAssignments_;
	std::vector<PaneWindowBinding> paneWindowBindings_;
	std::vector<FloatingPaneState> floatingPaneStates_;
	std::vector<SnappedPaneRect> snappedPaneRects_;
	std::vector<SnapSplitter> snapSplitters_;
	SnapLayoutPreferences snapLayoutPreferences_{};
	PaneKey activePane_{};
	PaneKey draggingPane_{};
	bool hasActivePane_ = false;
	bool dragSnapActive_ = false;
	float contentTop_ = 0.0f;
	Emu51ImguiSnapTarget dragSnapTarget_ = Emu51ImguiSnapTarget::None;
	bool layoutStateDirty_ = false;
	std::array<std::uint32_t, kMemoryBytes> prevInstructionCache_{};
	std::uint16_t prevInstructionCacheVersion_ = 1;
	bool prevInstructionCacheDirty_ = false;

	void drawMainMenu();
	void drawRegisters();
	void drawAssemblyEditor();
	void drawDisassemblyViews();
	void drawDisassemblyView(DisassemblyView& view);
	void drawMonitorViews();
	void drawMonitorView(MonitorView& view);
	void drawExecutionSettingsDialog();
	void drawSetProgramCounterDialog();
	void drawBinaryLoadDialog();
	void drawFillRangeDialog();
	void drawSnapSplitters();
	void drawDragSnapOverlay();
	void resetMachine();
	void requestProgramLoad(bool resetCpuBeforeLoad);
	void requestProgramLoadFromPath(const std::filesystem::path& path, bool resetCpuBeforeLoad);
	bool loadProgram(const char* path, bool resetCpuBeforeLoad, bool rememberForReload, std::uint16_t binaryLoadAddress = 0, bool rememberRecent = true);
	bool loadAssemblyFileIntoEditor(const std::filesystem::path& path);
	bool saveAssemblyEditorFileToPath(const std::filesystem::path& path);
	std::filesystem::path chooseAssemblyEditorSavePath();
	void formatAssemblyEditor();
	bool compileAssemblyEditorImage(bool updateStatus);
	bool loadCompiledAssemblyEditorOutput(bool updateStatus, bool resetBeforeLoad);
	std::filesystem::path assemblyEditorBaseDirectory() const;
	void updateAssemblyEditorDiagnostics(const std::vector<emu51_asm::Diagnostic>& diagnostics);
	void setAssemblyEditorBuildStatus(bool success, std::string title, std::string detail);
	void runSteps(int count);
	void loadExecutionSettings();
	bool saveExecutionSettings() const;
	void updateDisassemblyFollowRange();
	void addDisassemblyView(std::uint16_t start, bool followPc);
	void addMonitorView(MonitorView::MemoryKind memoryKind, std::uint16_t start);
	void configureAddressSpace();
	void updateDragSnapState();
	void updateSnappedPaneLayout();
	void applyWindowPlacement(const PaneKey& pane, const ImVec2& defaultSize, ImGuiWindowFlags* flags);
	void notePaneActivity(const PaneKey& pane, bool active);
	void registerPaneWindow(const PaneKey& pane);
	void captureFloatingPaneState(const PaneKey& pane);
	void drawPaneSnapContextMenu(const PaneKey& pane);
	bool paneExists(const PaneKey& pane) const;
	bool paneFromWindowId(ImGuiID windowId, PaneKey* pane) const;
	Emu51ImguiSnapTarget snapTargetForPane(const PaneKey& pane) const;
	PaneSnapAssignment* findPaneSnapAssignment(const PaneKey& pane);
	const PaneSnapAssignment* findPaneSnapAssignment(const PaneKey& pane) const;
	FloatingPaneState* findFloatingPaneState(const PaneKey& pane);
	const FloatingPaneState* findFloatingPaneState(const PaneKey& pane) const;
	FloatingPaneState& ensureFloatingPaneState(const PaneKey& pane);
	void markLayoutStateDirty();
	void loadLayoutState();
	void loadLayoutState(std::istream& in);
	void saveLayoutState() const;
	void saveLayoutState(std::ostream& out) const;
	void loadRecentProgramPaths();
	void saveRecentProgramPaths() const;
	void rememberRecentProgramPath(const std::filesystem::path& path);
	void snapPane(const PaneKey& pane, Emu51ImguiSnapTarget target);
	void setPaneSnapTarget(const PaneKey& pane, Emu51ImguiSnapTarget target);
	void removePaneSnapTarget(const PaneKey& pane);
	void appendSnappedPane(std::array<std::vector<PaneKey>, 5>& panesByTarget, const PaneKey& pane) const;
	void clearMemory();
	void clearRam();
	void clearRom();
	void openFillRangeDialog(bool fillRom);
	void fillRange(bool fillRom, std::uint16_t start, std::uint16_t end, std::uint8_t value);
	std::uint16_t clampMonitorAddress(const MonitorView& view, int address) const;
	std::uint8_t readMonitorByte(const MonitorView& view, std::uint16_t address);
	const char* monitorName(const MonitorView& view) const;
	int monitorAddressDigits(const MonitorView& view) const;
	std::uint8_t readProgramByte(std::uint16_t address) const;
	std::uint16_t nextInstructionAddress(std::uint16_t address);
	std::uint16_t previousInstructionAddress(std::uint16_t address);
	int backwardCandidateScore(std::uint16_t start, std::uint16_t end);
	void invalidateInstructionCache();
	std::uint8_t executeInstruction();
};
