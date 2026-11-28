// SPDX-License-Identifier: GPL-2.0-only
#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>

#include <string>

#include "imgui.h"
#include "imgui_impl_metal.h"
#include "imgui_impl_osx.h"

#include "emu51_imgui_app.h"
#include "emu51_imgui_codicons.h"
#include "emu51_imgui_paths.h"

namespace {

std::string gImguiIniPath;
constexpr NSTimeInterval kInputRedrawDuration = 0.12;
constexpr NSTimeInterval kScrollRedrawDuration = 0.35;

NSString* functionKeyEquivalent(unichar key)
{
	return [NSString stringWithCharacters:&key length:1];
}

NSString* nsString(const std::string& value)
{
	return [NSString stringWithUTF8String:value.c_str()];
}

}  // namespace

@interface Emu51AppViewController : NSViewController <MTKViewDelegate, NSWindowDelegate>
- (IBAction)openRecentProject:(id)sender;
- (IBAction)clearRecentProjects:(id)sender;
- (IBAction)openProject:(id)sender;
- (IBAction)addProjectFiles:(id)sender;
- (IBAction)newProjectAssemblyFile:(id)sender;
- (IBAction)saveProject:(id)sender;
- (IBAction)saveProjectAs:(id)sender;
- (IBAction)openProgram:(id)sender;
- (IBAction)loadProgram:(id)sender;
- (IBAction)reloadProgram:(id)sender;
- (IBAction)openRecentProgram:(id)sender;
- (IBAction)clearRecentPrograms:(id)sender;
- (IBAction)saveAssemblyEditor:(id)sender;
- (IBAction)saveAssemblyEditorAs:(id)sender;
- (IBAction)editUndo:(id)sender;
- (IBAction)editRedo:(id)sender;
- (IBAction)editCut:(id)sender;
- (IBAction)editCopy:(id)sender;
- (IBAction)editPaste:(id)sender;
- (IBAction)editSelectAll:(id)sender;
- (IBAction)formatCode:(id)sender;
- (IBAction)compileAssemblyEditor:(id)sender;
- (IBAction)loadCompiledAssemblyEditorOutput:(id)sender;
- (IBAction)compileAndLoadAssemblyEditor:(id)sender;
- (IBAction)performProjectBuild:(NSMenuItem*)sender;
- (IBAction)showDebugger:(id)sender;
- (IBAction)showSerialTerminal:(id)sender;
- (IBAction)showHardwareModule:(id)sender;
- (IBAction)showPeripherals:(id)sender;
- (IBAction)newDisassemblyView:(id)sender;
- (IBAction)toggleRegisters:(id)sender;
- (IBAction)setEdgeDocksCollapsed:(NSMenuItem*)sender;
- (IBAction)toggleAssemblyEditor:(id)sender;
- (IBAction)toggleProjectPanel:(id)sender;
- (IBAction)toggleInternalRamMonitor:(id)sender;
- (IBAction)newDataMemoryMonitor:(id)sender;
- (IBAction)newProgramMemoryMonitor:(id)sender;
- (IBAction)toggleRun:(id)sender;
- (IBAction)resetExecution:(id)sender;
- (IBAction)setProgramCounter:(id)sender;
- (IBAction)stepOver:(id)sender;
- (IBAction)stepIn:(id)sender;
- (IBAction)stepOut:(id)sender;
- (IBAction)openExecutionSettings:(id)sender;
- (IBAction)toggleUnifiedMode:(id)sender;
- (IBAction)clearMemory:(id)sender;
- (IBAction)clearRam:(id)sender;
- (IBAction)clearRom:(id)sender;
- (IBAction)fillRamRange:(id)sender;
- (IBAction)fillRomRange:(id)sender;
- (BOOL)isUnifiedMode;
- (BOOL)prepareToQuit;
@end

@interface Emu51AppDelegate : NSObject <NSApplicationDelegate>
@property(nonatomic, assign) Emu51AppViewController* viewController;
- (IBAction)openProject:(id)sender;
- (IBAction)addProjectFiles:(id)sender;
- (IBAction)newProjectAssemblyFile:(id)sender;
- (IBAction)saveProject:(id)sender;
- (IBAction)saveProjectAs:(id)sender;
- (IBAction)openProgram:(id)sender;
- (IBAction)loadProgram:(id)sender;
- (IBAction)reloadProgram:(id)sender;
- (IBAction)openRecentProgram:(id)sender;
- (IBAction)clearRecentPrograms:(id)sender;
- (IBAction)saveAssemblyEditor:(id)sender;
- (IBAction)saveAssemblyEditorAs:(id)sender;
- (IBAction)editUndo:(id)sender;
- (IBAction)editRedo:(id)sender;
- (IBAction)editCut:(id)sender;
- (IBAction)editCopy:(id)sender;
- (IBAction)editPaste:(id)sender;
- (IBAction)editSelectAll:(id)sender;
- (IBAction)formatCode:(id)sender;
- (IBAction)compileAssemblyEditor:(id)sender;
- (IBAction)loadCompiledAssemblyEditorOutput:(id)sender;
- (IBAction)compileAndLoadAssemblyEditor:(id)sender;
- (IBAction)performProjectBuild:(NSMenuItem*)sender;
- (IBAction)showDebugger:(id)sender;
- (IBAction)showSerialTerminal:(id)sender;
- (IBAction)showHardwareModule:(id)sender;
- (IBAction)showPeripherals:(id)sender;
- (IBAction)newDisassemblyView:(id)sender;
- (IBAction)toggleRegisters:(id)sender;
- (IBAction)setEdgeDocksCollapsed:(NSMenuItem*)sender;
- (IBAction)toggleAssemblyEditor:(id)sender;
- (IBAction)toggleProjectPanel:(id)sender;
- (IBAction)toggleInternalRamMonitor:(id)sender;
- (IBAction)newDataMemoryMonitor:(id)sender;
- (IBAction)newProgramMemoryMonitor:(id)sender;
- (IBAction)toggleRun:(id)sender;
- (IBAction)resetExecution:(id)sender;
- (IBAction)setProgramCounter:(id)sender;
- (IBAction)stepOver:(id)sender;
- (IBAction)stepIn:(id)sender;
- (IBAction)stepOut:(id)sender;
- (IBAction)openExecutionSettings:(id)sender;
- (IBAction)toggleUnifiedMode:(id)sender;
- (IBAction)clearMemory:(id)sender;
- (IBAction)clearRam:(id)sender;
- (IBAction)clearRom:(id)sender;
- (IBAction)fillRamRange:(id)sender;
- (IBAction)fillRomRange:(id)sender;
@end

@interface Emu51AppViewController ()
@property(nonatomic, readonly) MTKView* mtkView;
@property(nonatomic, strong) id<MTLDevice> device;
@property(nonatomic, strong) id<MTLCommandQueue> commandQueue;
@property(nonatomic) Emu51ImguiApp* app;
@property(nonatomic, strong) id eventMonitor;
@property(nonatomic, strong) NSTimer* executionTimer;
@property(nonatomic) NSTimeInterval interactiveRedrawDeadline;
@property(nonatomic, strong) NSMenuItem* reloadProgramMenuItem;
@property(nonatomic, strong) NSMenuItem* openRecentMenuItem;
@property(nonatomic, strong) NSMenu* recentProgramsMenu;
@property(nonatomic, strong) NSMenuItem* openRecentProjectsMenuItem;
@property(nonatomic, strong) NSMenu* recentProjectsMenu;
@property(nonatomic, strong) NSMenuItem* saveAssemblyMenuItem;
@property(nonatomic, strong) NSMenuItem* saveAsAssemblyMenuItem;
@property(nonatomic, strong) NSMenuItem* editUndoMenuItem;
@property(nonatomic, strong) NSMenuItem* editRedoMenuItem;
@property(nonatomic, strong) NSMenuItem* editCutMenuItem;
@property(nonatomic, strong) NSMenuItem* editCopyMenuItem;
@property(nonatomic, strong) NSMenuItem* editPasteMenuItem;
@property(nonatomic, strong) NSMenuItem* editSelectAllMenuItem;
@property(nonatomic, strong) NSMenuItem* editFormatCodeMenuItem;
@property(nonatomic, strong) NSMenuItem* buildCompileMenuItem;
@property(nonatomic, strong) NSMenuItem* buildLoadMenuItem;
@property(nonatomic, strong) NSMenuItem* buildCompileAndLoadMenuItem;
@property(nonatomic, strong) NSMenuItem* buildProjectMenuItem;
@property(nonatomic, strong) NSMenuItem* buildAndLoadProjectMenuItem;
@property(nonatomic, strong) NSMenuItem* unifiedModeMenuItem;
@property(nonatomic, strong) NSMenuItem* clearMemoryMenuItem;
@property(nonatomic, strong) NSMenuItem* clearDataMemoryMenuItem;
@property(nonatomic, strong) NSMenuItem* clearProgramMemoryMenuItem;
@property(nonatomic, strong) NSMenuItem* fillMemoryMenuItem;
@property(nonatomic, strong) NSMenuItem* fillProgramMemoryMenuItem;
@property(nonatomic, strong) NSMenuItem* runMenuItem;
@property(nonatomic, strong) NSMenuItem* setProgramCounterMenuItem;
@property(nonatomic, strong) NSMenuItem* assemblyEditorMenuItem;
@property(nonatomic, strong) NSMenuItem* projectPanelMenuItem;
@property(nonatomic, strong) NSMenuItem* registersMenuItem;
@property(nonatomic, strong) NSMenuItem* internalRamMonitorMenuItem;
@property(nonatomic, strong) NSMenuItem* externalMemoryMonitorMenuItem;
@property(nonatomic, strong) NSMenuItem* dataMemoryMonitorMenuItem;
@property(nonatomic, strong) NSMenuItem* programMemoryMonitorMenuItem;
- (void)updateFileMenuItems;
- (void)updateEditMenuItems;
- (void)updateBuildMenuItems;
- (void)updateMemoryMenuItems;
- (void)updateExecutionMenuItems;
- (void)updateWindowMenuItems;
- (void)updateWindowTitle;
- (void)requestInteractiveRedrawForDuration:(NSTimeInterval)duration;
- (BOOL)shouldKeepDrawingForInteraction;
@end

@implementation Emu51AppViewController

- (void)updateFileMenuItems
{
	self.reloadProgramMenuItem.enabled = self.app->access()->hasLoadedProgram();
	const int recentCount = self.app->access()->recentProgramFileCount();
	self.openRecentMenuItem.enabled = recentCount > 0;
	[self.recentProgramsMenu removeAllItems];
	if (recentCount == 0) {
		NSMenuItem* emptyItem = [[NSMenuItem alloc] initWithTitle:@"No Recent Files"
			action:nil keyEquivalent:@""];
		emptyItem.enabled = NO;
		[self.recentProgramsMenu addItem:emptyItem];
	} else {
		for (int i = 0; i < recentCount; ++i) {
			NSString* path = nsString(self.app->access()->recentProgramFilePath(i));
			NSString* filename = path.lastPathComponent.length > 0 ? path.lastPathComponent : path;
			NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:[NSString stringWithFormat:@"%d %@", i + 1, filename]
				action:@selector(openRecentProgram:) keyEquivalent:@""];
			item.target = self;
			item.tag = i;
			item.toolTip = path;
			[self.recentProgramsMenu addItem:item];
		}
		[self.recentProgramsMenu addItem:[NSMenuItem separatorItem]];
		NSMenuItem* clearItem = [[NSMenuItem alloc] initWithTitle:@"Clear Menu"
			action:@selector(clearRecentPrograms:) keyEquivalent:@""];
		clearItem.target = self;
		[self.recentProgramsMenu addItem:clearItem];
	}
	const int recentProjectCount = self.app->access()->recentProjectFileCount();
	self.openRecentProjectsMenuItem.enabled = recentProjectCount > 0;
	[self.recentProjectsMenu removeAllItems];
	if (recentProjectCount == 0) {
		NSMenuItem* emptyItem = [[NSMenuItem alloc] initWithTitle:@"No Recent Projects"
			action:nil keyEquivalent:@""];
		emptyItem.enabled = NO;
		[self.recentProjectsMenu addItem:emptyItem];
	} else {
		for (int i = 0; i < recentProjectCount; ++i) {
			NSString* path = nsString(self.app->access()->recentProjectFilePath(i));
			NSString* filename = path.lastPathComponent.length > 0 ? path.lastPathComponent : path;
			NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:[NSString stringWithFormat:@"%d %@", i + 1, filename]
				action:@selector(openRecentProject:) keyEquivalent:@""];
			item.target = self;
			item.tag = i;
			item.toolTip = path;
			[self.recentProjectsMenu addItem:item];
		}
		[self.recentProjectsMenu addItem:[NSMenuItem separatorItem]];
		NSMenuItem* clearItem = [[NSMenuItem alloc] initWithTitle:@"Clear Menu"
			action:@selector(clearRecentProjects:) keyEquivalent:@""];
		clearItem.target = self;
		[self.recentProjectsMenu addItem:clearItem];
	}
	const BOOL canSaveAssembly = self.app->access()->canSaveAssemblyEditorFile();
	self.saveAssemblyMenuItem.enabled = canSaveAssembly;
	self.saveAsAssemblyMenuItem.enabled = canSaveAssembly;
}

- (void)updateEditMenuItems
{
	const BOOL canUseTextCommands = self.app->access()->canUseAssemblyEditorTextCommands();
	self.editUndoMenuItem.enabled = self.app->access()->canUndoAssemblyEditorText();
	self.editRedoMenuItem.enabled = self.app->access()->canRedoAssemblyEditorText();
	self.editCutMenuItem.enabled = canUseTextCommands;
	self.editCopyMenuItem.enabled = canUseTextCommands;
	self.editPasteMenuItem.enabled = canUseTextCommands;
	self.editSelectAllMenuItem.enabled = canUseTextCommands;
	self.editFormatCodeMenuItem.enabled = self.app->access()->canFormatAssemblyEditorText();
}

- (void)updateBuildMenuItems
{
	self.buildCompileMenuItem.enabled = self.app->access()->hasAssemblyEditorSource();
	self.buildLoadMenuItem.enabled = self.app->access()->hasCompiledAssemblyEditorOutput();
	self.buildCompileAndLoadMenuItem.enabled = self.app->access()->hasAssemblyEditorSource();
	const BOOL hasProjectFiles = self.app->access()->hasProjectFiles();
	self.buildProjectMenuItem.enabled = hasProjectFiles;
	self.buildAndLoadProjectMenuItem.enabled = hasProjectFiles;
}

- (void)updateMemoryMenuItems
{
	const BOOL unifiedMode = self.app->access()->unifiedMode();
	self.unifiedModeMenuItem.state = unifiedMode ? NSControlStateValueOn : NSControlStateValueOff;
	self.clearMemoryMenuItem.hidden = !unifiedMode;
	self.clearDataMemoryMenuItem.hidden = unifiedMode;
	self.clearProgramMemoryMenuItem.hidden = unifiedMode;
	self.fillMemoryMenuItem.title = unifiedMode ? @"Fill memory range" : @"Fill data memory range";
	self.fillProgramMemoryMenuItem.hidden = unifiedMode;
}

- (void)updateWindowMenuItems
{
    self.registersMenuItem.state = self.app->access()->registersVisible() ? NSControlStateValueOn : NSControlStateValueOff;
	const BOOL unifiedMode = self.app->access()->unifiedMode();
	self.assemblyEditorMenuItem.state = self.app->access()->assemblyEditorVisible() ? NSControlStateValueOn : NSControlStateValueOff;
	self.projectPanelMenuItem.state = self.app->access()->projectPanelVisible() ? NSControlStateValueOn : NSControlStateValueOff;
	self.internalRamMonitorMenuItem.state = self.app->access()->internalRamMonitorVisible() ? NSControlStateValueOn : NSControlStateValueOff;
	self.externalMemoryMonitorMenuItem.hidden = !unifiedMode;
	self.dataMemoryMonitorMenuItem.hidden = unifiedMode;
	self.programMemoryMonitorMenuItem.hidden = unifiedMode;
}

- (void)updateExecutionMenuItems
{
	self.runMenuItem.state = self.app->access(false)->running() ? NSControlStateValueOn : NSControlStateValueOff;
	self.setProgramCounterMenuItem.enabled = !self.app->access(false)->running();
}

- (void)updateWindowTitle
{
	if (self.view.window != nil)
		self.view.window.title = nsString(self.app->access()->windowTitle());
}

- (void)requestInteractiveRedrawForDuration:(NSTimeInterval)duration
{
	const NSTimeInterval deadline = [NSDate timeIntervalSinceReferenceDate] + duration;
	if (deadline > self.interactiveRedrawDeadline)
		self.interactiveRedrawDeadline = deadline;
	[self.mtkView setNeedsDisplay:YES];
}

- (BOOL)shouldKeepDrawingForInteraction
{
	return self.interactiveRedrawDeadline > [NSDate timeIntervalSinceReferenceDate];
}

- (instancetype)init
{
	self = [super initWithNibName:nil bundle:nil];
	if (!self)
		return nil;

	_device = MTLCreateSystemDefaultDevice();
	_commandQueue = [_device newCommandQueue];
	_app = new Emu51ImguiApp();

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	gImguiIniPath = emu51GetImguiIniPath();
	io.IniFilename = gImguiIniPath.empty() ? nullptr : gImguiIniPath.c_str();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	ImGui::StyleColorsDark();
	io.Fonts->AddFontDefault();
	emu51LoadCodiconsFont(io);
	ImGui_ImplMetal_Init(_device);
    _app->startBackgroundExecution();

	return self;
}

- (IBAction)openRecentProject:(id)sender
{
	self.app->access()->openRecentProjectFile((int)[sender tag]);
	[self updateFileMenuItems];
	[self updateEditMenuItems];
	[self updateBuildMenuItems];
	[self updateWindowTitle];
	[self.mtkView setNeedsDisplay:YES];
}

- (IBAction)clearRecentProjects:(id)sender
{
	self.app->access()->clearRecentProjectFiles();
	[self updateFileMenuItems];
	[self.mtkView setNeedsDisplay:YES];
}

- (IBAction)openProject:(id)sender
{
	self.app->access()->requestOpenProject();
	[self updateFileMenuItems];
	[self updateEditMenuItems];
	[self updateBuildMenuItems];
	[self updateWindowTitle];
	[self.mtkView setNeedsDisplay:YES];
}

- (IBAction)addProjectFiles:(id)sender
{
	self.app->access()->requestAddProjectFiles();
	[self updateBuildMenuItems];
	[self updateWindowMenuItems];
	[self.mtkView setNeedsDisplay:YES];
}

- (IBAction)newProjectAssemblyFile:(id)sender
{
	self.app->access()->newProjectAssemblyFile();
	[self updateFileMenuItems];
	[self updateEditMenuItems];
	[self updateBuildMenuItems];
	[self updateWindowMenuItems];
	[self.mtkView setNeedsDisplay:YES];
}

- (IBAction)saveProject:(id)sender
{
	self.app->access()->saveProject();
	[self updateFileMenuItems];
	[self updateWindowTitle];
	[self.mtkView setNeedsDisplay:YES];
}

- (IBAction)saveProjectAs:(id)sender
{
	self.app->access()->saveProjectAs();
	[self updateFileMenuItems];
	[self updateWindowTitle];
	[self.mtkView setNeedsDisplay:YES];
}

- (IBAction)openProgram:(id)sender
{
	self.app->access()->requestOpenProgram();
	[self updateFileMenuItems];
	[self updateEditMenuItems];
	[self updateBuildMenuItems];
	[self updateWindowTitle];
	[self.mtkView setNeedsDisplay:YES];
}

- (IBAction)loadProgram:(id)sender
{
	self.app->access()->requestLoadProgram();
	[self updateFileMenuItems];
	[self updateEditMenuItems];
	[self updateBuildMenuItems];
	[self updateWindowTitle];
	[self.mtkView setNeedsDisplay:YES];
}

- (IBAction)reloadProgram:(id)sender
{
	self.app->access()->reloadProgram();
	[self updateFileMenuItems];
	[self updateWindowTitle];
	[self.mtkView setNeedsDisplay:YES];
}

- (IBAction)openRecentProgram:(id)sender
{
	self.app->access()->openRecentProgramFile((int)[sender tag]);
	[self updateFileMenuItems];
	[self updateEditMenuItems];
	[self updateBuildMenuItems];
	[self updateWindowTitle];
	[self.mtkView setNeedsDisplay:YES];
}

- (IBAction)clearRecentPrograms:(id)sender
{
	self.app->access()->clearRecentProgramFiles();
	[self updateFileMenuItems];
	[self.mtkView setNeedsDisplay:YES];
}

- (IBAction)saveAssemblyEditor:(id)sender
{
	self.app->access()->saveAssemblyEditorFile();
	[self updateFileMenuItems];
	[self updateBuildMenuItems];
	[self updateWindowTitle];
	[self.mtkView setNeedsDisplay:YES];
}

- (IBAction)saveAssemblyEditorAs:(id)sender
{
	self.app->access()->saveAssemblyEditorFileAs();
	[self updateFileMenuItems];
	[self updateBuildMenuItems];
	[self updateWindowTitle];
	[self.mtkView setNeedsDisplay:YES];
}

- (IBAction)editUndo:(id)sender
{
	self.app->access()->undoAssemblyEditorText();
	[self updateEditMenuItems];
	[self.mtkView setNeedsDisplay:YES];
}

- (IBAction)editRedo:(id)sender
{
	self.app->access()->redoAssemblyEditorText();
	[self updateEditMenuItems];
	[self.mtkView setNeedsDisplay:YES];
}

- (IBAction)editCut:(id)sender
{
	self.app->access()->cutAssemblyEditorText();
	[self updateEditMenuItems];
	[self.mtkView setNeedsDisplay:YES];
}

- (IBAction)editCopy:(id)sender
{
	self.app->access()->copyAssemblyEditorText();
	[self updateEditMenuItems];
	[self.mtkView setNeedsDisplay:YES];
}

- (IBAction)editPaste:(id)sender
{
	self.app->access()->pasteAssemblyEditorText();
	[self updateEditMenuItems];
	[self.mtkView setNeedsDisplay:YES];
}

- (IBAction)editSelectAll:(id)sender
{
	self.app->access()->selectAllAssemblyEditorText();
	[self updateEditMenuItems];
	[self.mtkView setNeedsDisplay:YES];
}

- (IBAction)formatCode:(id)sender
{
	self.app->access()->formatAssemblyEditorText();
	[self updateEditMenuItems];
	[self updateBuildMenuItems];
	[self.mtkView setNeedsDisplay:YES];
}

- (IBAction)compileAssemblyEditor:(id)sender
{
	self.app->access()->compileAssemblyEditor();
	[self updateBuildMenuItems];
	[self.mtkView setNeedsDisplay:YES];
}

- (IBAction)loadCompiledAssemblyEditorOutput:(id)sender
{
	self.app->access()->loadCompiledAssemblyEditorOutput();
	[self updateFileMenuItems];
	[self updateBuildMenuItems];
	[self.mtkView setNeedsDisplay:YES];
}

- (IBAction)compileAndLoadAssemblyEditor:(id)sender
{
	self.app->access()->compileAndLoadAssemblyEditor();
	[self updateFileMenuItems];
	[self updateBuildMenuItems];
	[self.mtkView setNeedsDisplay:YES];
}

- (IBAction)performProjectBuild:(NSMenuItem*)sender
{
	self.app->access()->performProjectBuild(static_cast<Emu51ProjectBuildAction>(sender.tag));
	[self updateBuildMenuItems];
	[self updateWindowMenuItems];
	[self.mtkView setNeedsDisplay:YES];
}

- (IBAction)showPeripherals:(id)sender
{
    self.app->access()->showPeripherals();
    [self.mtkView setNeedsDisplay:YES];
}

- (IBAction)showHardwareModule:(id)sender
{
	self.app->access()->showHardwareModule();
	[self.mtkView setNeedsDisplay:YES];
}

- (IBAction)showSerialTerminal:(id)sender
{
	self.app->access()->showSerialTerminal();
	[self.mtkView setNeedsDisplay:YES];
}

- (IBAction)showDebugger:(id)sender
{
	self.app->access()->showDebugger();
	[self requestInteractiveRedrawForDuration:kInputRedrawDuration];
}

- (IBAction)newDisassemblyView:(id)sender
{
	self.app->access()->createDisassemblyView();
	[self.mtkView setNeedsDisplay:YES];
}

- (IBAction)toggleRegisters:(id)sender
{
    { auto access = self.app->access(); access->setRegistersVisible(!access->registersVisible()); }
    [self updateWindowMenuItems];
    [self requestInteractiveRedrawForDuration:kInputRedrawDuration];
}

- (IBAction)setEdgeDocksCollapsed:(NSMenuItem*)sender
{
	self.app->access()->setAllEdgeDocksCollapsed(sender.tag != 0);
	[self requestInteractiveRedrawForDuration:kInputRedrawDuration];
}

- (IBAction)toggleAssemblyEditor:(id)sender
{
	self.app->access()->setAssemblyEditorVisible(!self.app->access()->assemblyEditorVisible());
	[self updateEditMenuItems];
	[self updateBuildMenuItems];
	[self updateWindowMenuItems];
	[self.mtkView setNeedsDisplay:YES];
}

- (IBAction)toggleProjectPanel:(id)sender
{
	{ auto app = self.app->access(); app->setProjectPanelVisible(!app->projectPanelVisible()); }
	[self updateWindowMenuItems];
	[self requestInteractiveRedrawForDuration:kInputRedrawDuration];
}

- (IBAction)toggleInternalRamMonitor:(id)sender
{
	self.app->access()->setInternalRamMonitorVisible(!self.app->access()->internalRamMonitorVisible());
	[self updateWindowMenuItems];
	[self.mtkView setNeedsDisplay:YES];
}

- (IBAction)newDataMemoryMonitor:(id)sender
{
	self.app->access()->createDataMemoryMonitor();
	[self.mtkView setNeedsDisplay:YES];
}

- (IBAction)newProgramMemoryMonitor:(id)sender
{
	self.app->access()->createProgramMemoryMonitor();
	[self.mtkView setNeedsDisplay:YES];
}

- (IBAction)toggleRun:(id)sender
{
	self.app->access()->toggleRun();
	[self updateExecutionMenuItems];
	[self.mtkView setNeedsDisplay:YES];
}

- (IBAction)resetExecution:(id)sender
{
	const NSEventModifierFlags modifiers = NSApp.currentEvent.modifierFlags;
	self.app->access()->resetCpuState((modifiers & NSEventModifierFlagShift) != 0);
	[self updateExecutionMenuItems];
	[self.mtkView setNeedsDisplay:YES];
}

- (IBAction)setProgramCounter:(id)sender
{
	self.app->access()->requestSetProgramCounter();
	[self updateExecutionMenuItems];
	[self.mtkView setNeedsDisplay:YES];
}

- (IBAction)stepOver:(id)sender
{
	self.app->access()->stepOver();
	[self updateExecutionMenuItems];
	[self.mtkView setNeedsDisplay:YES];
}

- (IBAction)stepIn:(id)sender
{
	self.app->access()->stepIn();
	[self updateExecutionMenuItems];
	[self.mtkView setNeedsDisplay:YES];
}

- (IBAction)stepOut:(id)sender
{
	self.app->access()->stepOut();
	[self updateExecutionMenuItems];
	[self.mtkView setNeedsDisplay:YES];
}

- (IBAction)openExecutionSettings:(id)sender
{
	self.app->access()->requestExecutionSettings();
	// ImGui needs subsequent frames to size and reveal a newly opened modal.
	[self requestInteractiveRedrawForDuration:kInputRedrawDuration];
}

- (IBAction)toggleUnifiedMode:(id)sender
{
	self.app->access()->setUnifiedMode(!self.app->access()->unifiedMode());
	[self updateMemoryMenuItems];
	[self updateWindowMenuItems];
	[self.mtkView setNeedsDisplay:YES];
}

- (BOOL)isUnifiedMode
{
	return self.app->access()->unifiedMode();
}

- (IBAction)clearMemory:(id)sender
{
	self.app->access()->clearAllMemory();
	[self.mtkView setNeedsDisplay:YES];
}

- (IBAction)clearRam:(id)sender
{
	self.app->access()->clearRamOnly();
	[self.mtkView setNeedsDisplay:YES];
}

- (IBAction)clearRom:(id)sender
{
	self.app->access()->clearRomOnly();
	[self.mtkView setNeedsDisplay:YES];
}

- (IBAction)fillRamRange:(id)sender
{
	self.app->access()->requestFillRamRange();
	[self.mtkView setNeedsDisplay:YES];
}

- (IBAction)fillRomRange:(id)sender
{
	self.app->access()->requestFillRomRange();
	[self.mtkView setNeedsDisplay:YES];
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wobjc-missing-super-calls"
- (void)dealloc
{
	[self.executionTimer invalidate];
	delete _app;
	_app = nullptr;
}
#pragma clang diagnostic pop

- (MTKView*)mtkView
{
	return (MTKView*)self.view;
}

- (void)loadView
{
	self.view = [[MTKView alloc] initWithFrame:NSMakeRect(0, 0, 1280, 800)];
}

- (void)viewDidLoad
{
	[super viewDidLoad];

	self.mtkView.device = self.device;
	self.mtkView.delegate = self;
	self.mtkView.clearColor = MTLClearColorMake(0.09, 0.10, 0.12, 1.0);
	self.mtkView.colorPixelFormat = MTLPixelFormatBGRA8Unorm;
	self.mtkView.paused = YES;
	self.mtkView.enableSetNeedsDisplay = YES;

	ImGui_ImplOSX_Init(self.view);
	__weak Emu51AppViewController* weakSelf = self;
	self.executionTimer = [NSTimer timerWithTimeInterval:1.0 / 240.0 repeats:YES block:^(NSTimer*) {
		Emu51AppViewController* controller = weakSelf;
		if (controller != nil && controller.app != nullptr && controller.app->access(false)->running()) {
			controller.app->access()->tick();
			// An unpaused MTKView already draws at its display cadence. Forcing
			// additional draws at the 240 Hz CPU service rate can exhaust Metal
			// drawables and block this same thread before its next execution tick.
			if (controller.mtkView.paused)
				[controller.mtkView setNeedsDisplay:YES];
		}
	}];
	[[NSRunLoop mainRunLoop] addTimer:self.executionTimer forMode:NSRunLoopCommonModes];
	self.eventMonitor = [NSEvent addLocalMonitorForEventsMatchingMask:
		(NSEventMaskKeyDown |
		 NSEventMaskKeyUp |
		 NSEventMaskFlagsChanged |
		 NSEventMaskLeftMouseDown |
		 NSEventMaskLeftMouseUp |
		 NSEventMaskRightMouseDown |
		 NSEventMaskRightMouseUp |
		 NSEventMaskOtherMouseDown |
		 NSEventMaskOtherMouseUp |
		 NSEventMaskMouseMoved |
		 NSEventMaskLeftMouseDragged |
		 NSEventMaskRightMouseDragged |
		 NSEventMaskOtherMouseDragged |
		 NSEventMaskScrollWheel)
		handler:^NSEvent* (NSEvent* event) {
			const NSTimeInterval redrawDuration =
				event.type == NSEventTypeScrollWheel ? kScrollRedrawDuration : kInputRedrawDuration;
			[self requestInteractiveRedrawForDuration:redrawDuration];
			return event;
		}];
	[self.mtkView setNeedsDisplay:YES];
	[NSApp activateIgnoringOtherApps:YES];
	[self updateFileMenuItems];
	[self updateEditMenuItems];
	[self updateBuildMenuItems];
	[self updateMemoryMenuItems];
	[self updateWindowMenuItems];
	[self updateExecutionMenuItems];
	[self updateWindowTitle];
}

- (void)viewWillAppear
{
	[super viewWillAppear];
	self.view.window.delegate = self;
	[self updateWindowTitle];
}

- (void)windowWillClose:(NSNotification*)notification
{
	[self.executionTimer invalidate];
	if (self.eventMonitor != nil) {
		[NSEvent removeMonitor:self.eventMonitor];
		self.eventMonitor = nil;
	}
    self.app->stopBackgroundExecution();
	ImGui_ImplMetal_Shutdown();
	ImGui_ImplOSX_Shutdown();
	ImGui::DestroyContext();
	[NSApp terminate:nil];
}

- (BOOL)prepareToQuit
{
	const bool allowed = self.app == nullptr || self.app->access()->prepareToQuit();
	if (!allowed)
		[self requestInteractiveRedrawForDuration:kInputRedrawDuration];
	return allowed;
}

- (BOOL)windowShouldClose:(NSWindow*)sender
{
	return [self prepareToQuit];
}

- (void)drawInMTKView:(MTKView*)view
{
	ImGuiIO& io = ImGui::GetIO();
	io.DisplaySize = ImVec2(view.bounds.size.width, view.bounds.size.height);
	const CGFloat windowScale = view.window.screen.backingScaleFactor;
	const CGFloat scale = windowScale != 0 ? windowScale : NSScreen.mainScreen.backingScaleFactor;
	io.DisplayFramebufferScale = ImVec2(scale, scale);

	id<MTLCommandBuffer> commandBuffer = [self.commandQueue commandBuffer];
	MTLRenderPassDescriptor* renderPassDescriptor = view.currentRenderPassDescriptor;
	if (renderPassDescriptor == nil) {
		[commandBuffer commit];
		return;
	}

	ImGui_ImplMetal_NewFrame(renderPassDescriptor);
	ImGui_ImplOSX_NewFrame(view);
	ImGui::NewFrame();

	self.app->access()->draw();
	[self updateFileMenuItems];
	[self updateEditMenuItems];
	[self updateBuildMenuItems];
	[self updateExecutionMenuItems];
	[self updateWindowMenuItems];
	[self updateWindowTitle];
	if (self.app->access()->consumeQuitRequested()) {
		[NSApp terminate:nil];
		return;
	}
	view.paused = !(self.app->access(false)->shouldAnimate() || [self shouldKeepDrawingForInteraction]);

	ImGui::Render();
	id<MTLRenderCommandEncoder> renderEncoder = [commandBuffer renderCommandEncoderWithDescriptor:renderPassDescriptor];
	ImGui_ImplMetal_RenderDrawData(ImGui::GetDrawData(), commandBuffer, renderEncoder);
	[renderEncoder endEncoding];
	[commandBuffer presentDrawable:view.currentDrawable];
	[commandBuffer commit];
}

- (void)mtkView:(MTKView*)view drawableSizeWillChange:(CGSize)size
{
}

@end

@implementation Emu51AppDelegate

- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication*)sender
{
	return [self.viewController prepareToQuit] ? NSTerminateNow : NSTerminateCancel;
}

- (IBAction)openProject:(id)sender
{
	[self.viewController openProject:sender];
}

- (IBAction)addProjectFiles:(id)sender
{
	[self.viewController addProjectFiles:sender];
}

- (IBAction)newProjectAssemblyFile:(id)sender
{
	[self.viewController newProjectAssemblyFile:sender];
}

- (IBAction)saveProject:(id)sender
{
	[self.viewController saveProject:sender];
}

- (IBAction)saveProjectAs:(id)sender
{
	[self.viewController saveProjectAs:sender];
}

- (IBAction)openProgram:(id)sender
{
	[self.viewController openProgram:sender];
}

- (IBAction)loadProgram:(id)sender
{
	[self.viewController loadProgram:sender];
}

- (IBAction)reloadProgram:(id)sender
{
	[self.viewController reloadProgram:sender];
}

- (IBAction)openRecentProgram:(id)sender
{
	[self.viewController openRecentProgram:sender];
}

- (IBAction)clearRecentPrograms:(id)sender
{
	[self.viewController clearRecentPrograms:sender];
}

- (IBAction)saveAssemblyEditor:(id)sender
{
	[self.viewController saveAssemblyEditor:sender];
}

- (IBAction)saveAssemblyEditorAs:(id)sender
{
	[self.viewController saveAssemblyEditorAs:sender];
}

- (IBAction)editUndo:(id)sender
{
	[self.viewController editUndo:sender];
}

- (IBAction)editRedo:(id)sender
{
	[self.viewController editRedo:sender];
}

- (IBAction)editCut:(id)sender
{
	[self.viewController editCut:sender];
}

- (IBAction)editCopy:(id)sender
{
	[self.viewController editCopy:sender];
}

- (IBAction)editPaste:(id)sender
{
	[self.viewController editPaste:sender];
}

- (IBAction)editSelectAll:(id)sender
{
	[self.viewController editSelectAll:sender];
}

- (IBAction)formatCode:(id)sender
{
	[self.viewController formatCode:sender];
}

- (IBAction)compileAssemblyEditor:(id)sender
{
	[self.viewController compileAssemblyEditor:sender];
}

- (IBAction)loadCompiledAssemblyEditorOutput:(id)sender
{
	[self.viewController loadCompiledAssemblyEditorOutput:sender];
}

- (IBAction)compileAndLoadAssemblyEditor:(id)sender
{
	[self.viewController compileAndLoadAssemblyEditor:sender];
}

- (IBAction)performProjectBuild:(NSMenuItem*)sender
{
	[self.viewController performProjectBuild:sender];
}

- (IBAction)showPeripherals:(id)sender
{
    [self.viewController showPeripherals:sender];
}

- (IBAction)showHardwareModule:(id)sender
{
	[self.viewController showHardwareModule:sender];
}

- (IBAction)showSerialTerminal:(id)sender
{
	[self.viewController showSerialTerminal:sender];
}

- (IBAction)showDebugger:(id)sender
{
	[self.viewController showDebugger:sender];
}

- (IBAction)newDisassemblyView:(id)sender
{
	[self.viewController newDisassemblyView:sender];
}

- (IBAction)toggleRegisters:(id)sender
{
    [self.viewController toggleRegisters:sender];
}

- (IBAction)setEdgeDocksCollapsed:(NSMenuItem*)sender
{
	[self.viewController setEdgeDocksCollapsed:sender];
}

- (IBAction)toggleAssemblyEditor:(id)sender
{
	[self.viewController toggleAssemblyEditor:sender];
}

- (IBAction)toggleProjectPanel:(id)sender
{
	[self.viewController toggleProjectPanel:sender];
}

- (IBAction)toggleInternalRamMonitor:(id)sender
{
	[self.viewController toggleInternalRamMonitor:sender];
}

- (IBAction)newDataMemoryMonitor:(id)sender
{
	[self.viewController newDataMemoryMonitor:sender];
}

- (IBAction)newProgramMemoryMonitor:(id)sender
{
	[self.viewController newProgramMemoryMonitor:sender];
}

- (IBAction)toggleRun:(id)sender
{
	[self.viewController toggleRun:sender];
}

- (IBAction)resetExecution:(id)sender
{
	[self.viewController resetExecution:sender];
}

- (IBAction)setProgramCounter:(id)sender
{
	[self.viewController setProgramCounter:sender];
}

- (IBAction)stepOver:(id)sender
{
	[self.viewController stepOver:sender];
}

- (IBAction)stepIn:(id)sender
{
	[self.viewController stepIn:sender];
}

- (IBAction)stepOut:(id)sender
{
	[self.viewController stepOut:sender];
}

- (IBAction)openExecutionSettings:(id)sender
{
	[self.viewController openExecutionSettings:sender];
}

- (IBAction)toggleUnifiedMode:(id)sender
{
	[self.viewController toggleUnifiedMode:sender];
}

- (IBAction)clearMemory:(id)sender
{
	[self.viewController clearMemory:sender];
}

- (IBAction)clearRam:(id)sender
{
	[self.viewController clearRam:sender];
}

- (IBAction)clearRom:(id)sender
{
	[self.viewController clearRom:sender];
}

- (IBAction)fillRamRange:(id)sender
{
	[self.viewController fillRamRange:sender];
}

- (IBAction)fillRomRange:(id)sender
{
	[self.viewController fillRomRange:sender];
}

@end

int main()
{
	@autoreleasepool {
		[NSApplication sharedApplication];
		[NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
		Emu51AppDelegate* appDelegate = [[Emu51AppDelegate alloc] init];
		[NSApp setDelegate:appDelegate];

		NSMenu* menubar = [[NSMenu alloc] init];
		[NSApp setMainMenu:menubar];
		Emu51AppViewController* viewController = [[Emu51AppViewController alloc] init];
		appDelegate.viewController = viewController;

		NSMenuItem* appMenuItem = [[NSMenuItem alloc] init];
		[menubar addItem:appMenuItem];
		NSMenu* appMenu = [[NSMenu alloc] init];
		NSString* appName = [[NSProcessInfo processInfo] processName];
		NSMenuItem* quitMenuItem = [[NSMenuItem alloc] initWithTitle:[@"Quit " stringByAppendingString:appName]
			action:@selector(terminate:) keyEquivalent:@"q"];
		[appMenu addItem:quitMenuItem];
		[appMenuItem setSubmenu:appMenu];

		NSMenuItem* fileMenuItem = [[NSMenuItem alloc] init];
		[menubar addItem:fileMenuItem];
		NSMenu* fileMenu = [[NSMenu alloc] initWithTitle:@"File"];
		NSMenuItem* openProjectItem = [[NSMenuItem alloc] initWithTitle:@"Open project..."
			action:@selector(openProject:) keyEquivalent:@""];
		openProjectItem.target = appDelegate;
		[fileMenu addItem:openProjectItem];
		NSMenuItem* recentProjectsItem = [[NSMenuItem alloc] initWithTitle:@"Open Recent Projects"
			action:nil keyEquivalent:@""];
		NSMenu* recentProjectsMenu = [[NSMenu alloc] initWithTitle:@"Open Recent Projects"];
		viewController.openRecentProjectsMenuItem = recentProjectsItem;
		viewController.recentProjectsMenu = recentProjectsMenu;
		[recentProjectsItem setSubmenu:recentProjectsMenu];
		[fileMenu addItem:recentProjectsItem];
		NSMenuItem* saveProjectItem = [[NSMenuItem alloc] initWithTitle:@"Save project"
			action:@selector(saveProject:) keyEquivalent:@"s"];
		saveProjectItem.target = appDelegate;
		[fileMenu addItem:saveProjectItem];
		NSMenuItem* saveProjectAsItem = [[NSMenuItem alloc] initWithTitle:@"Save project as..."
			action:@selector(saveProjectAs:) keyEquivalent:@""];
		saveProjectAsItem.target = appDelegate;
		[fileMenu addItem:saveProjectAsItem];
		NSMenuItem* addProjectFilesItem = [[NSMenuItem alloc] initWithTitle:@"Add file..."
			action:@selector(addProjectFiles:) keyEquivalent:@""];
		addProjectFilesItem.target = appDelegate;
		[fileMenu addItem:addProjectFilesItem];
		NSMenuItem* newProjectAssemblyFileItem = [[NSMenuItem alloc] initWithTitle:@"New ASM File"
			action:@selector(newProjectAssemblyFile:) keyEquivalent:@""];
		newProjectAssemblyFileItem.target = appDelegate;
		[fileMenu addItem:newProjectAssemblyFileItem];
		[fileMenu addItem:[NSMenuItem separatorItem]];
		NSMenuItem* openProgramItem = [[NSMenuItem alloc] initWithTitle:@"Open program..."
			action:@selector(openProgram:) keyEquivalent:@"o"];
		openProgramItem.target = appDelegate;
		[fileMenu addItem:openProgramItem];
		NSMenuItem* loadProgramItem = [[NSMenuItem alloc] initWithTitle:@"Load program..."
			action:@selector(loadProgram:) keyEquivalent:@"l"];
		loadProgramItem.target = appDelegate;
		[fileMenu addItem:loadProgramItem];
		NSMenuItem* reloadProgramItem = [[NSMenuItem alloc] initWithTitle:@"Reload program"
			action:@selector(reloadProgram:) keyEquivalent:@"r"];
		reloadProgramItem.target = appDelegate;
		reloadProgramItem.keyEquivalentModifierMask = NSEventModifierFlagCommand | NSEventModifierFlagOption;
		viewController.reloadProgramMenuItem = reloadProgramItem;
		[fileMenu addItem:reloadProgramItem];
		NSMenuItem* openRecentItem = [[NSMenuItem alloc] initWithTitle:@"Open Recent"
			action:nil keyEquivalent:@""];
		NSMenu* recentProgramsMenu = [[NSMenu alloc] initWithTitle:@"Open Recent"];
		viewController.openRecentMenuItem = openRecentItem;
		viewController.recentProgramsMenu = recentProgramsMenu;
		[openRecentItem setSubmenu:recentProgramsMenu];
		[fileMenu addItem:openRecentItem];
		[fileMenu addItem:[NSMenuItem separatorItem]];
		NSMenuItem* saveAssemblyItem = [[NSMenuItem alloc] initWithTitle:@"Save source"
			action:@selector(saveAssemblyEditor:) keyEquivalent:@"s"];
		saveAssemblyItem.target = appDelegate;
		saveAssemblyItem.keyEquivalentModifierMask = NSEventModifierFlagCommand | NSEventModifierFlagShift;
		viewController.saveAssemblyMenuItem = saveAssemblyItem;
		[fileMenu addItem:saveAssemblyItem];
		NSMenuItem* saveAsAssemblyItem = [[NSMenuItem alloc] initWithTitle:@"Save source as..."
			action:@selector(saveAssemblyEditorAs:) keyEquivalent:@""];
		saveAsAssemblyItem.target = appDelegate;
		viewController.saveAsAssemblyMenuItem = saveAsAssemblyItem;
		[fileMenu addItem:saveAsAssemblyItem];
		[fileMenu addItem:[NSMenuItem separatorItem]];
		NSMenuItem* fileQuitItem = [[NSMenuItem alloc] initWithTitle:@"Quit"
			action:@selector(terminate:) keyEquivalent:@"q"];
		[fileMenu addItem:fileQuitItem];
		[viewController updateFileMenuItems];
		[fileMenuItem setSubmenu:fileMenu];

		NSMenuItem* editMenuItem = [[NSMenuItem alloc] init];
		[menubar addItem:editMenuItem];
		NSMenu* editMenu = [[NSMenu alloc] initWithTitle:@"Edit"];
		NSMenuItem* undoItem = [[NSMenuItem alloc] initWithTitle:@"Undo"
			action:@selector(editUndo:) keyEquivalent:@"z"];
		undoItem.target = appDelegate;
		viewController.editUndoMenuItem = undoItem;
		[editMenu addItem:undoItem];
		NSMenuItem* redoItem = [[NSMenuItem alloc] initWithTitle:@"Redo"
			action:@selector(editRedo:) keyEquivalent:@"z"];
		redoItem.target = appDelegate;
		redoItem.keyEquivalentModifierMask = NSEventModifierFlagCommand | NSEventModifierFlagShift;
		viewController.editRedoMenuItem = redoItem;
		[editMenu addItem:redoItem];
		[editMenu addItem:[NSMenuItem separatorItem]];
		NSMenuItem* cutItem = [[NSMenuItem alloc] initWithTitle:@"Cut"
			action:@selector(editCut:) keyEquivalent:@"x"];
		cutItem.target = appDelegate;
		viewController.editCutMenuItem = cutItem;
		[editMenu addItem:cutItem];
		NSMenuItem* copyItem = [[NSMenuItem alloc] initWithTitle:@"Copy"
			action:@selector(editCopy:) keyEquivalent:@"c"];
		copyItem.target = appDelegate;
		viewController.editCopyMenuItem = copyItem;
		[editMenu addItem:copyItem];
		NSMenuItem* pasteItem = [[NSMenuItem alloc] initWithTitle:@"Paste"
			action:@selector(editPaste:) keyEquivalent:@"v"];
		pasteItem.target = appDelegate;
		viewController.editPasteMenuItem = pasteItem;
		[editMenu addItem:pasteItem];
		NSMenuItem* selectAllItem = [[NSMenuItem alloc] initWithTitle:@"Select All"
			action:@selector(editSelectAll:) keyEquivalent:@"a"];
		selectAllItem.target = appDelegate;
		viewController.editSelectAllMenuItem = selectAllItem;
		[editMenu addItem:selectAllItem];
		[editMenu addItem:[NSMenuItem separatorItem]];
		NSMenuItem* formatCodeItem = [[NSMenuItem alloc] initWithTitle:@"Format Code"
			action:@selector(formatCode:) keyEquivalent:@""];
		formatCodeItem.target = appDelegate;
		viewController.editFormatCodeMenuItem = formatCodeItem;
		[editMenu addItem:formatCodeItem];
		[viewController updateEditMenuItems];
		[editMenuItem setSubmenu:editMenu];

		NSMenuItem* memoryMenuItem = [[NSMenuItem alloc] init];
		[menubar addItem:memoryMenuItem];
		NSMenu* memoryMenu = [[NSMenu alloc] initWithTitle:@"Memory"];
		NSMenuItem* unifiedModeItem = [[NSMenuItem alloc] initWithTitle:@"Unified mode"
			action:@selector(toggleUnifiedMode:) keyEquivalent:@""];
		unifiedModeItem.target = appDelegate;
		viewController.unifiedModeMenuItem = unifiedModeItem;
		[memoryMenu addItem:unifiedModeItem];
		[memoryMenu addItem:[NSMenuItem separatorItem]];
		NSMenuItem* clearMemoryItem = [[NSMenuItem alloc] initWithTitle:@"Clear memory"
			action:@selector(clearMemory:) keyEquivalent:@""];
		clearMemoryItem.target = appDelegate;
		viewController.clearMemoryMenuItem = clearMemoryItem;
		[memoryMenu addItem:clearMemoryItem];
		NSMenuItem* clearRamItem = [[NSMenuItem alloc] initWithTitle:@"Clear data memory"
			action:@selector(clearRam:) keyEquivalent:@""];
		clearRamItem.target = appDelegate;
		viewController.clearDataMemoryMenuItem = clearRamItem;
		[memoryMenu addItem:clearRamItem];
		NSMenuItem* clearRomItem = [[NSMenuItem alloc] initWithTitle:@"Clear program memory"
			action:@selector(clearRom:) keyEquivalent:@""];
		clearRomItem.target = appDelegate;
		viewController.clearProgramMemoryMenuItem = clearRomItem;
		[memoryMenu addItem:clearRomItem];
		[memoryMenu addItem:[NSMenuItem separatorItem]];
		NSMenuItem* fillRamItem = [[NSMenuItem alloc] initWithTitle:@"Fill memory range"
			action:@selector(fillRamRange:) keyEquivalent:@""];
		fillRamItem.target = appDelegate;
		viewController.fillMemoryMenuItem = fillRamItem;
		[memoryMenu addItem:fillRamItem];
		NSMenuItem* fillRomItem = [[NSMenuItem alloc] initWithTitle:@"Fill program memory range"
			action:@selector(fillRomRange:) keyEquivalent:@""];
		fillRomItem.target = appDelegate;
		viewController.fillProgramMemoryMenuItem = fillRomItem;
		[memoryMenu addItem:fillRomItem];
		[viewController updateMemoryMenuItems];
		[memoryMenuItem setSubmenu:memoryMenu];

		NSMenuItem* buildMenuItem = [[NSMenuItem alloc] init];
		[menubar addItem:buildMenuItem];
		NSMenu* buildMenu = [[NSMenu alloc] initWithTitle:@"Build"];
		NSMenuItem* compileItem = [[NSMenuItem alloc] initWithTitle:@"Compile"
			action:@selector(compileAssemblyEditor:) keyEquivalent:@"b"];
		compileItem.target = appDelegate;
		viewController.buildCompileMenuItem = compileItem;
		[buildMenu addItem:compileItem];
		NSMenuItem* loadCompiledItem = [[NSMenuItem alloc] initWithTitle:@"Load"
			action:@selector(loadCompiledAssemblyEditorOutput:) keyEquivalent:@"l"];
		loadCompiledItem.target = appDelegate;
		loadCompiledItem.keyEquivalentModifierMask = NSEventModifierFlagCommand | NSEventModifierFlagShift;
		viewController.buildLoadMenuItem = loadCompiledItem;
		[buildMenu addItem:loadCompiledItem];
		NSMenuItem* compileAndLoadItem = [[NSMenuItem alloc] initWithTitle:@"Compile and Load"
			action:@selector(compileAndLoadAssemblyEditor:) keyEquivalent:@"b"];
		compileAndLoadItem.target = appDelegate;
		compileAndLoadItem.keyEquivalentModifierMask = NSEventModifierFlagCommand | NSEventModifierFlagShift;
		viewController.buildCompileAndLoadMenuItem = compileAndLoadItem;
		[buildMenu addItem:compileAndLoadItem];
		[buildMenu addItem:[NSMenuItem separatorItem]];
		const auto addBuildAction = [&](NSString* title, Emu51ProjectBuildAction action) {
			NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:title
				action:@selector(performProjectBuild:) keyEquivalent:@""];
			item.target = appDelegate;
			item.tag = static_cast<NSInteger>(action);
			[buildMenu addItem:item];
			return item;
		};
		viewController.buildProjectMenuItem = addBuildAction(@"Build Project", Emu51ProjectBuildAction::Build);
		viewController.buildAndLoadProjectMenuItem = addBuildAction(@"Build and Load", Emu51ProjectBuildAction::BuildAndLoad);
		addBuildAction(@"Power Cycle", Emu51ProjectBuildAction::PowerCycle);
		[viewController updateBuildMenuItems];
		[buildMenuItem setSubmenu:buildMenu];

		NSMenuItem* executionMenuItem = [[NSMenuItem alloc] init];
		[menubar addItem:executionMenuItem];
		NSMenu* executionMenu = [[NSMenu alloc] initWithTitle:@"Execution"];
		NSMenuItem* debuggerItem = [[NSMenuItem alloc] initWithTitle:@"Debugger"
			action:@selector(showDebugger:) keyEquivalent:@""];
		debuggerItem.target = appDelegate;
		[executionMenu addItem:debuggerItem];
		NSMenuItem* serialItem = [[NSMenuItem alloc] initWithTitle:@"Serial terminal"
			action:@selector(showSerialTerminal:) keyEquivalent:@""];
		serialItem.target = appDelegate;
		[executionMenu addItem:serialItem];
		NSMenuItem* runItem = [[NSMenuItem alloc] initWithTitle:@"Run"
			action:@selector(toggleRun:) keyEquivalent:functionKeyEquivalent(NSF9FunctionKey)];
		runItem.target = appDelegate;
		runItem.keyEquivalentModifierMask = 0;
		viewController.runMenuItem = runItem;
		[executionMenu addItem:runItem];
		NSMenuItem* resetItem = [[NSMenuItem alloc] initWithTitle:@"Reset"
			action:@selector(resetExecution:) keyEquivalent:@"r"];
		resetItem.target = appDelegate;
		resetItem.keyEquivalentModifierMask = NSEventModifierFlagCommand;
		[executionMenu addItem:resetItem];
		NSMenuItem* setProgramCounterItem = [[NSMenuItem alloc] initWithTitle:@"Set PC..."
			action:@selector(setProgramCounter:) keyEquivalent:@""];
		setProgramCounterItem.target = appDelegate;
		viewController.setProgramCounterMenuItem = setProgramCounterItem;
		[executionMenu addItem:setProgramCounterItem];
		NSMenuItem* stepOverItem = [[NSMenuItem alloc] initWithTitle:@"Step over"
			action:@selector(stepOver:) keyEquivalent:functionKeyEquivalent(NSF8FunctionKey)];
		stepOverItem.target = appDelegate;
		stepOverItem.keyEquivalentModifierMask = 0;
		[executionMenu addItem:stepOverItem];
		NSMenuItem* stepInItem = [[NSMenuItem alloc] initWithTitle:@"Step in"
			action:@selector(stepIn:) keyEquivalent:functionKeyEquivalent(NSF7FunctionKey)];
		stepInItem.target = appDelegate;
		stepInItem.keyEquivalentModifierMask = 0;
		[executionMenu addItem:stepInItem];
		NSMenuItem* stepOutItem = [[NSMenuItem alloc] initWithTitle:@"Step out"
			action:@selector(stepOut:) keyEquivalent:functionKeyEquivalent(NSF8FunctionKey)];
		stepOutItem.target = appDelegate;
		stepOutItem.keyEquivalentModifierMask = NSEventModifierFlagShift;
		[executionMenu addItem:stepOutItem];
		[executionMenu addItem:[NSMenuItem separatorItem]];
		NSMenuItem* executionSettingsItem = [[NSMenuItem alloc] initWithTitle:@"Settings..."
			action:@selector(openExecutionSettings:) keyEquivalent:@""];
		executionSettingsItem.target = appDelegate;
		[executionMenu addItem:executionSettingsItem];
		[viewController updateExecutionMenuItems];
		[executionMenuItem setSubmenu:executionMenu];

		NSMenuItem* windowMenuItem = [[NSMenuItem alloc] init];
		[menubar addItem:windowMenuItem];
		NSMenu* windowMenu = [[NSMenu alloc] initWithTitle:@"Window"];
        NSMenuItem* registersItem = [[NSMenuItem alloc] initWithTitle:@"Registers"
            action:@selector(toggleRegisters:) keyEquivalent:@""];
        registersItem.target = appDelegate;
        viewController.registersMenuItem = registersItem;
        [windowMenu addItem:registersItem];
        NSMenuItem* peripheralsItem = [[NSMenuItem alloc] initWithTitle:@"Peripherals"
            action:@selector(showPeripherals:) keyEquivalent:@""];
        peripheralsItem.target = appDelegate;
        [windowMenu addItem:peripheralsItem];
		if (*viewController.app->access()->hardwareModuleName()) {
			NSMenuItem* hardwareItem = [[NSMenuItem alloc] initWithTitle:nsString(viewController.app->access()->hardwareModuleName())
				action:@selector(showHardwareModule:) keyEquivalent:@""];
			hardwareItem.target = appDelegate;
			[windowMenu addItem:hardwareItem];
		}
		NSMenuItem* newDisassemblyItem = [[NSMenuItem alloc] initWithTitle:@"New Disassembly View"
			action:@selector(newDisassemblyView:) keyEquivalent:@"d"];
		newDisassemblyItem.target = appDelegate;
		[windowMenu addItem:newDisassemblyItem];
		NSMenuItem* assemblyEditorItem = [[NSMenuItem alloc] initWithTitle:@"Assembly Editor"
			action:@selector(toggleAssemblyEditor:) keyEquivalent:@""];
		assemblyEditorItem.target = appDelegate;
		viewController.assemblyEditorMenuItem = assemblyEditorItem;
		[windowMenu addItem:assemblyEditorItem];
		NSMenuItem* projectPanelItem = [[NSMenuItem alloc] initWithTitle:@"Project"
			action:@selector(toggleProjectPanel:) keyEquivalent:@""];
		projectPanelItem.target = appDelegate;
		viewController.projectPanelMenuItem = projectPanelItem;
		[windowMenu addItem:projectPanelItem];
		NSMenuItem* internalRamMonitorItem = [[NSMenuItem alloc] initWithTitle:@"Internal RAM Monitor"
			action:@selector(toggleInternalRamMonitor:) keyEquivalent:@""];
		internalRamMonitorItem.target = appDelegate;
		viewController.internalRamMonitorMenuItem = internalRamMonitorItem;
		[windowMenu addItem:internalRamMonitorItem];
		NSMenuItem* newExternalMemoryMonitorItem = [[NSMenuItem alloc] initWithTitle:@"New External Memory Monitor"
			action:@selector(newDataMemoryMonitor:) keyEquivalent:@""];
		newExternalMemoryMonitorItem.target = appDelegate;
		viewController.externalMemoryMonitorMenuItem = newExternalMemoryMonitorItem;
		[windowMenu addItem:newExternalMemoryMonitorItem];
		NSMenuItem* newDataMemoryMonitorItem = [[NSMenuItem alloc] initWithTitle:@"New Data Memory Monitor"
			action:@selector(newDataMemoryMonitor:) keyEquivalent:@""];
		newDataMemoryMonitorItem.target = appDelegate;
		viewController.dataMemoryMonitorMenuItem = newDataMemoryMonitorItem;
		[windowMenu addItem:newDataMemoryMonitorItem];
		NSMenuItem* newProgramMemoryMonitorItem = [[NSMenuItem alloc] initWithTitle:@"New Program Memory Monitor"
			action:@selector(newProgramMemoryMonitor:) keyEquivalent:@""];
		newProgramMemoryMonitorItem.target = appDelegate;
		viewController.programMemoryMonitorMenuItem = newProgramMemoryMonitorItem;
		[windowMenu addItem:newProgramMemoryMonitorItem];
		[windowMenu addItem:[NSMenuItem separatorItem]];
		NSMenuItem* collapseDocksItem = [[NSMenuItem alloc] initWithTitle:@"Collapse All Edge Docks"
			action:@selector(setEdgeDocksCollapsed:) keyEquivalent:@""];
		collapseDocksItem.target = appDelegate;
		collapseDocksItem.tag = 1;
		[windowMenu addItem:collapseDocksItem];
		NSMenuItem* expandDocksItem = [[NSMenuItem alloc] initWithTitle:@"Expand All Edge Docks"
			action:@selector(setEdgeDocksCollapsed:) keyEquivalent:@""];
		expandDocksItem.target = appDelegate;
		[windowMenu addItem:expandDocksItem];
		[viewController updateWindowMenuItems];
		[windowMenuItem setSubmenu:windowMenu];

		NSWindow* window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 1280, 800)
			styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable
			backing:NSBackingStoreBuffered
			defer:NO];
		[window setTitle:@"emu51"];
		[window center];
		[window setContentViewController:viewController];
		[window makeKeyAndOrderFront:nil];

		[NSApp run];
	}
	return 0;
}
