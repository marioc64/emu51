// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <string>

enum class Emu51ImguiSnapTarget {
	None,
	Left,
	Right,
	Top,
	Bottom,
	Center,
};

class Emu51ImguiMenuTarget {
public:
	virtual ~Emu51ImguiMenuTarget() = default;

	virtual void requestOpenProgram() = 0;
	virtual void requestOpenProject() = 0;
	virtual void saveProject() = 0;
	virtual void saveProjectAs() = 0;
	virtual int recentProjectFileCount() const = 0;
	virtual const std::string& recentProjectFilePath(int index) const = 0;
	virtual void openRecentProjectFile(int index) = 0;
	virtual void clearRecentProjectFiles() = 0;
	virtual void requestLoadProgram() = 0;
	virtual bool hasLoadedProgram() const = 0;
	virtual void reloadProgram() = 0;
	virtual int recentProgramFileCount() const = 0;
	virtual const std::string& recentProgramFilePath(int index) const = 0;
	virtual void openRecentProgramFile(int index) = 0;
	virtual void clearRecentProgramFiles() = 0;
	virtual bool canSaveAssemblyEditorFile() const = 0;
	virtual void saveAssemblyEditorFile() = 0;
	virtual void saveAssemblyEditorFileAs() = 0;
	virtual void requestQuit() = 0;
	virtual bool canUseAssemblyEditorTextCommands() const = 0;
	virtual bool canUndoAssemblyEditorText() const = 0;
	virtual bool canRedoAssemblyEditorText() const = 0;
	virtual bool canFormatAssemblyEditorText() const = 0;
	virtual void selectAllAssemblyEditorText() = 0;
	virtual void undoAssemblyEditorText() = 0;
	virtual void redoAssemblyEditorText() = 0;
	virtual void copyAssemblyEditorText() = 0;
	virtual void cutAssemblyEditorText() = 0;
	virtual void pasteAssemblyEditorText() = 0;
	virtual void formatAssemblyEditorText() = 0;
	virtual bool hasAssemblyEditorSource() const = 0;
	virtual bool hasCompiledAssemblyEditorOutput() const = 0;
	virtual void compileAssemblyEditor() = 0;
	virtual void loadCompiledAssemblyEditorOutput() = 0;
	virtual void compileAndLoadAssemblyEditor() = 0;
	virtual bool running() const = 0;
	virtual std::string executionTimingStatus() const = 0;
	virtual void toggleRun() = 0;
	virtual void resetCpuState(bool stopRunning) = 0;
	virtual void requestSetProgramCounter() = 0;
	virtual void stepIn() = 0;
	virtual void stepOver() = 0;
	virtual void stepOut() = 0;
	virtual void requestExecutionSettings() = 0;
	virtual void showDebugger() = 0;
	virtual bool unifiedMode() const = 0;
	virtual void setUnifiedMode(bool unifiedMode) = 0;
	virtual bool internalRamMonitorVisible() const = 0;
	virtual void setInternalRamMonitorVisible(bool visible) = 0;
	virtual bool assemblyEditorVisible() const = 0;
	virtual void setAssemblyEditorVisible(bool visible) = 0;
	virtual void createDisassemblyView() = 0;
	virtual void createDataMemoryMonitor() = 0;
	virtual void createProgramMemoryMonitor() = 0;
	virtual void clearAllMemory() = 0;
	virtual void clearRamOnly() = 0;
	virtual void clearRomOnly() = 0;
	virtual void requestFillRamRange() = 0;
	virtual void requestFillRomRange() = 0;
	virtual bool hasActivePane() const = 0;
	virtual void snapActivePane(Emu51ImguiSnapTarget target) = 0;
	virtual void resetSnappedPanes() = 0;
};

void emu51DrawImguiMainMenuBar(Emu51ImguiMenuTarget& target);
float emu51DrawImguiToolbar(Emu51ImguiMenuTarget& target);
