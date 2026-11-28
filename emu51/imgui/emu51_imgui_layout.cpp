// SPDX-License-Identifier: GPL-2.0-only
#include "emu51_imgui_app.h"
#include "emu51_imgui_app_internal.h"
#include "emu51_editor_files.h"
#include "imgui.h"
#include "imgui_internal.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
namespace fs = std::filesystem;
using namespace emu51_imgui_internal;
} // namespace

void Emu51ImguiApp::drawDragSnapOverlay()
{
	if (!dragSnapActive_ || !paneExists(draggingPane_))
		return;

	const auto targets = buildSnapOverlayTargets(ImGui::GetMainViewport(), contentTop_);
	ImDrawList* drawList = ImGui::GetForegroundDrawList();
	for (const SnapOverlayTarget& target : targets) {
		const bool highlighted = target.target == dragSnapTarget_;
		const ImU32 fillColor = highlighted ? IM_COL32(96, 160, 255, 170) : IM_COL32(32, 40, 56, 150);
		const ImU32 outlineColor = highlighted ? IM_COL32(180, 220, 255, 255) : IM_COL32(150, 165, 190, 220);
		drawList->AddRectFilled(target.rect.Min, target.rect.Max, fillColor, 10.0f);
		drawList->AddRect(target.rect.Min, target.rect.Max, outlineColor, 10.0f, 0, 2.0f);
		const char* label = nullptr;
		switch (target.target) {
		case Emu51ImguiSnapTarget::Left:
			label = "Left";
			break;
		case Emu51ImguiSnapTarget::Right:
			label = "Right";
			break;
		case Emu51ImguiSnapTarget::Top:
			label = "Top";
			break;
		case Emu51ImguiSnapTarget::Bottom:
			label = "Bottom";
			break;
		case Emu51ImguiSnapTarget::Center:
			label = "Center";
			break;
		case Emu51ImguiSnapTarget::None:
		default:
			label = "";
			break;
		}
		const ImVec2 textSize = ImGui::CalcTextSize(label);
		const ImVec2 textPos(
			target.rect.GetCenter().x - textSize.x * 0.5f,
			target.rect.GetCenter().y - textSize.y * 0.5f);
		drawList->AddText(textPos, IM_COL32(245, 247, 250, 255), label);
	}
}

void Emu51ImguiApp::updateDragSnapState()
{
	ImGuiContext& g = *GImGui;
	if (ImGuiWindow* movingWindow = g.MovingWindow ? g.MovingWindow->RootWindow : nullptr) {
		PaneKey pane;
		if (paneFromWindowId(movingWindow->ID, &pane) && !paneHiddenByCollapsedDock(pane)) {
			const bool dragStarted = !dragSnapActive_ || !(draggingPane_ == pane);
			const bool dragFromTitleBar = movingWindow->TitleBarRect().Contains(g.IO.MouseClickedPos[ImGuiMouseButton_Left]);
			draggingPane_ = pane;
			dragSnapActive_ = snapTargetForPane(pane) == Emu51ImguiSnapTarget::None || dragFromTitleBar;
			dragSnapTarget_ = dragSnapActive_
				? hitTestSnapOverlayTarget(buildSnapOverlayTargets(ImGui::GetMainViewport(), contentTop_), g.IO.MousePos)
				: Emu51ImguiSnapTarget::None;
			if (dragStarted) {
				FloatingPaneState& state = ensureFloatingPaneState(pane);
				if (snapTargetForPane(pane) == Emu51ImguiSnapTarget::None) {
					state.size = movingWindow->SizeFull;
					state.hasSize = true;
					state.restorePending = false;
					markLayoutStateDirty();
				} else if (dragFromTitleBar) {
					state.restorePending = state.hasSize;
					removePaneSnapTarget(pane);
					markLayoutStateDirty();
				}
			}
			if (dragSnapActive_)
				return;
		}
	}

	if (!dragSnapActive_)
		return;

	dragSnapTarget_ = hitTestSnapOverlayTarget(buildSnapOverlayTargets(ImGui::GetMainViewport(), contentTop_), g.IO.MousePos);
	if (ImGui::IsMouseReleased(ImGuiMouseButton_Left) && paneExists(draggingPane_) && dragSnapTarget_ != Emu51ImguiSnapTarget::None)
		snapPane(draggingPane_, dragSnapTarget_);
	dragSnapActive_ = false;
	dragSnapTarget_ = Emu51ImguiSnapTarget::None;
}

void Emu51ImguiApp::drawSnapSplitters()
{
	if (snapSplitters_.empty())
		return;

	ImGuiViewport* viewport = ImGui::GetMainViewport();
	const ImRect workRect = paneSnapWorkRect(viewport, contentTop_);
	ImGui::SetNextWindowPos(viewport->Pos, ImGuiCond_Always);
	ImGui::SetNextWindowSize(viewport->Size, ImGuiCond_Always);
	ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
		ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoSavedSettings |
		ImGuiWindowFlags_NoBackground |
		ImGuiWindowFlags_NoBringToFrontOnFocus |
		ImGuiWindowFlags_NoNav;

	if (!ImGui::Begin("##snap_splitters", nullptr, flags)) {
		ImGui::End();
		return;
	}

	const auto findRect = [this](const PaneKey& pane) -> const SnappedPaneRect* {
		for (const SnappedPaneRect& rect : snappedPaneRects_) {
			if (rect.pane == pane)
				return &rect;
		}
		return nullptr;
	};

	ImDrawList* drawList = ImGui::GetWindowDrawList();
	for (size_t index = 0; index < snapSplitters_.size(); ++index) {
		SnapSplitter& splitter = snapSplitters_[index];
		ImGui::PushID(static_cast<int>(index));
		ImGui::SetCursorScreenPos(ImVec2(splitter.x, splitter.y));
		ImGui::InvisibleButton("splitter", ImVec2(splitter.width, splitter.height));
		const bool hovered = ImGui::IsItemHovered();
		const bool active = ImGui::IsItemActive();
		if (hovered || active)
			ImGui::SetMouseCursor(splitter.vertical ? ImGuiMouseCursor_ResizeEW : ImGuiMouseCursor_ResizeNS);

		if (active) {
			bool changed = false;
			if (splitter.kind == SnapSplitter::Kind::SlotBoundary) {
				if (splitter.target == Emu51ImguiSnapTarget::Left) {
					const float fraction = clampSlotFraction((ImGui::GetIO().MousePos.x - workRect.Min.x) / std::max(1.0f, workRect.GetWidth()));
					changed = snapLayoutPreferences_.leftWidthFraction != fraction;
					snapLayoutPreferences_.leftWidthFraction = fraction;
				} else if (splitter.target == Emu51ImguiSnapTarget::Right) {
					const float fraction = clampSlotFraction((workRect.Max.x - ImGui::GetIO().MousePos.x) / std::max(1.0f, workRect.GetWidth()));
					changed = snapLayoutPreferences_.rightWidthFraction != fraction;
					snapLayoutPreferences_.rightWidthFraction = fraction;
				} else if (splitter.target == Emu51ImguiSnapTarget::Top) {
					const float fraction = clampSlotFraction((ImGui::GetIO().MousePos.y - workRect.Min.y) / std::max(1.0f, workRect.GetHeight()));
					changed = snapLayoutPreferences_.topHeightFraction != fraction;
					snapLayoutPreferences_.topHeightFraction = fraction;
				} else if (splitter.target == Emu51ImguiSnapTarget::Bottom) {
					const float fraction = clampSlotFraction((workRect.Max.y - ImGui::GetIO().MousePos.y) / std::max(1.0f, workRect.GetHeight()));
					changed = snapLayoutPreferences_.bottomHeightFraction != fraction;
					snapLayoutPreferences_.bottomHeightFraction = fraction;
				}
			} else {
				PaneSnapAssignment* first = findPaneSnapAssignment(splitter.firstPane);
				PaneSnapAssignment* second = findPaneSnapAssignment(splitter.secondPane);
				const SnappedPaneRect* firstRect = findRect(splitter.firstPane);
				const SnappedPaneRect* secondRect = findRect(splitter.secondPane);
				if (first != nullptr && second != nullptr && firstRect != nullptr && secondRect != nullptr) {
					if (splitter.vertical) {
						const float total = firstRect->width + secondRect->width;
						const float newFirst = std::clamp(ImGui::GetIO().MousePos.x - firstRect->x, 80.0f, total - 80.0f);
						changed = first->share != newFirst || second->share != (total - newFirst);
						first->share = newFirst;
						second->share = total - newFirst;
					} else {
						const float total = firstRect->height + secondRect->height;
						const float newFirst = std::clamp(ImGui::GetIO().MousePos.y - firstRect->y, 40.0f, total - 40.0f);
						changed = first->share != newFirst || second->share != (total - newFirst);
						first->share = newFirst;
						second->share = total - newFirst;
					}
				}
			}
			if (changed)
				markLayoutStateDirty();
		}

		const ImU32 color = active || hovered ? IM_COL32(180, 220, 255, 255) : IM_COL32(110, 125, 145, 200);
		if (splitter.vertical) {
			const float centerX = splitter.x + splitter.width * 0.5f;
			drawList->AddLine(ImVec2(centerX, splitter.y), ImVec2(centerX, splitter.y + splitter.height), color, 2.0f);
		} else {
			const float centerY = splitter.y + splitter.height * 0.5f;
			drawList->AddLine(ImVec2(splitter.x, centerY), ImVec2(splitter.x + splitter.width, centerY), color, 2.0f);
		}
		ImGui::PopID();
	}

	ImGui::End();
}

bool Emu51ImguiApp::edgeDockCollapsed(Emu51ImguiSnapTarget target) const
{
	const int index = snapTargetIndex(target);
	return index >= 0 && index < static_cast<int>(collapsedEdgeDocks_.size()) && collapsedEdgeDocks_[index];
}

bool Emu51ImguiApp::paneHiddenByCollapsedDock(const PaneKey& pane) const
{
	return edgeDockCollapsed(snapTargetForPane(pane));
}

void Emu51ImguiApp::setEdgeDockCollapsed(Emu51ImguiSnapTarget target, bool collapsed)
{
	const int index = snapTargetIndex(target);
	if (index < 0 || index >= static_cast<int>(collapsedEdgeDocks_.size()) || collapsedEdgeDocks_[index] == collapsed)
		return;
	collapsedEdgeDocks_[index] = collapsed;
	edgeDockVisibilityChanged();
}

void Emu51ImguiApp::setAllEdgeDocksCollapsed(bool collapsed)
{
	if (std::all_of(collapsedEdgeDocks_.begin(), collapsedEdgeDocks_.end(), [collapsed](bool value) { return value == collapsed; }))
		return;
	collapsedEdgeDocks_.fill(collapsed);
	edgeDockVisibilityChanged();
}

void Emu51ImguiApp::edgeDockVisibilityChanged()
{
	if (hasActivePane_ && paneHiddenByCollapsedDock(activePane_))
		hasActivePane_ = false;
	markLayoutStateDirty();
	executionRedrawPending_.store(true, std::memory_order_release);
}

void Emu51ImguiApp::updateSnappedPaneLayout()
{
	snappedPaneRects_.clear();
	snapSplitters_.clear();

	std::array<std::vector<PaneKey>, 5> panesByTarget;
	appendSnappedPane(panesByTarget, PaneKey{PaneKey::Kind::Registers, 0});
	appendSnappedPane(panesByTarget, PaneKey{PaneKey::Kind::Peripherals, 0});
	appendSnappedPane(panesByTarget, PaneKey{PaneKey::Kind::SerialTerminal, 0});
	if (projectPanelOpen_)
		appendSnappedPane(panesByTarget, PaneKey{PaneKey::Kind::Project, 0});
	if (assemblyEditorState_.open)
		appendSnappedPane(panesByTarget, PaneKey{PaneKey::Kind::AssemblyEditor, 0});
	for (const DisassemblyView& view : disassemblyViews_)
		appendSnappedPane(panesByTarget, PaneKey{PaneKey::Kind::Disassembly, view.id});
	if (internalRamMonitor_.open)
		appendSnappedPane(panesByTarget, PaneKey{PaneKey::Kind::InternalRamMonitor, 0});
	for (const MonitorView& view : monitorViews_)
		appendSnappedPane(panesByTarget, PaneKey{PaneKey::Kind::Monitor, view.id});

	for (size_t index = 0; index < collapsedEdgeDocks_.size(); ++index)
		if (collapsedEdgeDocks_[index])
			panesByTarget[index].clear();
	int snappedPaneCount = 0;
	for (const auto& panes : panesByTarget)
		snappedPaneCount += static_cast<int>(panes.size());
	if (snappedPaneCount == 0)
		return;

	ImGuiViewport* viewport = ImGui::GetMainViewport();
	ImRect remaining(
		ImVec2(viewport->Pos.x + kSnapMargin, contentTop_ + kSnapMargin),
		ImVec2(viewport->Pos.x + viewport->Size.x - kSnapMargin, viewport->Pos.y + viewport->Size.y - kSnapMargin));
	if (remaining.GetWidth() <= 0.0f || remaining.GetHeight() <= 0.0f)
		return;

	const auto addTiledRects = [this](const std::vector<PaneKey>& panes, const ImRect& rect, bool vertical, Emu51ImguiSnapTarget target) {
		if (panes.empty() || rect.GetWidth() <= 0.0f || rect.GetHeight() <= 0.0f)
			return;

		const float paneCount = static_cast<float>(panes.size());
		const float totalGap = kSnapGap * std::max(0.0f, paneCount - 1.0f);
		float totalShare = 0.0f;
		for (const PaneKey& pane : panes) {
			const PaneSnapAssignment* assignment = findPaneSnapAssignment(pane);
			totalShare += assignment != nullptr ? std::max(0.05f, assignment->share) : 1.0f;
		}
		if (totalShare <= 0.0f)
			totalShare = paneCount;

		float cursor = vertical ? rect.Min.y : rect.Min.x;
		SnappedPaneRect previousRect;
		bool havePreviousRect = false;
		for (size_t index = 0; index < panes.size(); ++index) {
			SnappedPaneRect paneRect;
			paneRect.pane = panes[index];
			const PaneSnapAssignment* assignment = findPaneSnapAssignment(panes[index]);
			const float share = assignment != nullptr ? std::max(0.05f, assignment->share) : 1.0f;
			const float availableSpan = vertical ? rect.GetHeight() : rect.GetWidth();
			const float span = index + 1 == panes.size()
				? ((vertical ? rect.Max.y : rect.Max.x) - cursor)
				: std::max(vertical ? 40.0f : 80.0f, (availableSpan - totalGap) * (share / totalShare));
			if (vertical) {
				paneRect.x = rect.Min.x;
				paneRect.y = cursor;
				paneRect.width = rect.GetWidth();
				paneRect.height = std::max(0.0f, std::min(span, rect.Max.y - cursor));
				cursor = paneRect.y + paneRect.height + kSnapGap;
			}
			else {
				paneRect.x = cursor;
				paneRect.y = rect.Min.y;
				paneRect.width = std::max(0.0f, std::min(span, rect.Max.x - cursor));
				paneRect.height = rect.GetHeight();
				cursor = paneRect.x + paneRect.width + kSnapGap;
			}
			snappedPaneRects_.push_back(paneRect);
			if (havePreviousRect) {
				SnapSplitter splitter;
				splitter.kind = SnapSplitter::Kind::PaneBoundary;
				splitter.target = target;
				splitter.firstPane = previousRect.pane;
				splitter.secondPane = paneRect.pane;
				if (vertical) {
					const float centerY = previousRect.y + previousRect.height + kSnapGap * 0.5f;
					splitter.vertical = false;
					splitter.x = rect.Min.x;
					splitter.y = centerY - 3.0f;
					splitter.width = rect.GetWidth();
					splitter.height = 6.0f;
				} else {
					const float centerX = previousRect.x + previousRect.width + kSnapGap * 0.5f;
					splitter.vertical = true;
					splitter.x = centerX - 3.0f;
					splitter.y = rect.Min.y;
					splitter.width = 6.0f;
					splitter.height = rect.GetHeight();
				}
				snapSplitters_.push_back(splitter);
			}
			previousRect = paneRect;
			havePreviousRect = true;
		}
	};

	const auto reserveTop = [&]() {
		const auto& panes = panesByTarget[static_cast<size_t>(snapTargetIndex(Emu51ImguiSnapTarget::Top))];
		if (panes.empty())
			return ImRect();
		const float minRemaining = snappedPaneCount > static_cast<int>(panes.size()) ? kSnapMinCenterHeight : 0.0f;
		const float height = clampedSlotSpan(remaining.GetHeight(), paneSnapWorkRect(viewport, contentTop_).GetHeight() * snapLayoutPreferences_.topHeightFraction, minRemaining);
		ImRect rect(remaining.Min, ImVec2(remaining.Max.x, remaining.Min.y + height));
		remaining.Min.y = std::min(remaining.Max.y, rect.Max.y + kSnapGap);
		return rect;
	};
	const auto reserveBottom = [&]() {
		const auto& panes = panesByTarget[static_cast<size_t>(snapTargetIndex(Emu51ImguiSnapTarget::Bottom))];
		if (panes.empty())
			return ImRect();
		const float minRemaining = snappedPaneCount > static_cast<int>(panes.size()) ? kSnapMinCenterHeight : 0.0f;
		const float height = clampedSlotSpan(remaining.GetHeight(), paneSnapWorkRect(viewport, contentTop_).GetHeight() * snapLayoutPreferences_.bottomHeightFraction, minRemaining);
		ImRect rect(ImVec2(remaining.Min.x, remaining.Max.y - height), remaining.Max);
		remaining.Max.y = std::max(remaining.Min.y, rect.Min.y - kSnapGap);
		return rect;
	};
	const auto reserveLeft = [&]() {
		const auto& panes = panesByTarget[static_cast<size_t>(snapTargetIndex(Emu51ImguiSnapTarget::Left))];
		if (panes.empty())
			return ImRect();
		const float minRemaining = snappedPaneCount > static_cast<int>(panes.size()) ? kSnapMinCenterWidth : 0.0f;
		const float width = clampedSlotSpan(remaining.GetWidth(), paneSnapWorkRect(viewport, contentTop_).GetWidth() * snapLayoutPreferences_.leftWidthFraction, minRemaining);
		ImRect rect(remaining.Min, ImVec2(remaining.Min.x + width, remaining.Max.y));
		remaining.Min.x = std::min(remaining.Max.x, rect.Max.x + kSnapGap);
		return rect;
	};
	const auto reserveRight = [&]() {
		const auto& panes = panesByTarget[static_cast<size_t>(snapTargetIndex(Emu51ImguiSnapTarget::Right))];
		if (panes.empty())
			return ImRect();
		const float minRemaining = snappedPaneCount > static_cast<int>(panes.size()) ? kSnapMinCenterWidth : 0.0f;
		const float width = clampedSlotSpan(remaining.GetWidth(), paneSnapWorkRect(viewport, contentTop_).GetWidth() * snapLayoutPreferences_.rightWidthFraction, minRemaining);
		ImRect rect(ImVec2(remaining.Max.x - width, remaining.Min.y), remaining.Max);
		remaining.Max.x = std::max(remaining.Min.x, rect.Min.x - kSnapGap);
		return rect;
	};

	const ImRect topRect = reserveTop();
	const ImRect bottomRect = reserveBottom();
	const ImRect leftRect = reserveLeft();
	const ImRect rightRect = reserveRight();
	const ImRect centerRect = remaining;

	if (leftRect.GetWidth() > 0.0f) {
		SnapSplitter splitter;
		splitter.kind = SnapSplitter::Kind::SlotBoundary;
		splitter.target = Emu51ImguiSnapTarget::Left;
		splitter.vertical = true;
		splitter.x = leftRect.Max.x + kSnapGap * 0.5f - 3.0f;
		splitter.y = leftRect.Min.y;
		splitter.width = 6.0f;
		splitter.height = leftRect.GetHeight();
		snapSplitters_.push_back(splitter);
	}
	if (rightRect.GetWidth() > 0.0f) {
		SnapSplitter splitter;
		splitter.kind = SnapSplitter::Kind::SlotBoundary;
		splitter.target = Emu51ImguiSnapTarget::Right;
		splitter.vertical = true;
		splitter.x = rightRect.Min.x - kSnapGap * 0.5f - 3.0f;
		splitter.y = rightRect.Min.y;
		splitter.width = 6.0f;
		splitter.height = rightRect.GetHeight();
		snapSplitters_.push_back(splitter);
	}
	if (topRect.GetHeight() > 0.0f) {
		SnapSplitter splitter;
		splitter.kind = SnapSplitter::Kind::SlotBoundary;
		splitter.target = Emu51ImguiSnapTarget::Top;
		splitter.vertical = false;
		splitter.x = topRect.Min.x;
		splitter.y = topRect.Max.y + kSnapGap * 0.5f - 3.0f;
		splitter.width = topRect.GetWidth();
		splitter.height = 6.0f;
		snapSplitters_.push_back(splitter);
	}
	if (bottomRect.GetHeight() > 0.0f) {
		SnapSplitter splitter;
		splitter.kind = SnapSplitter::Kind::SlotBoundary;
		splitter.target = Emu51ImguiSnapTarget::Bottom;
		splitter.vertical = false;
		splitter.x = bottomRect.Min.x;
		splitter.y = bottomRect.Min.y - kSnapGap * 0.5f - 3.0f;
		splitter.width = bottomRect.GetWidth();
		splitter.height = 6.0f;
		snapSplitters_.push_back(splitter);
	}

	addTiledRects(panesByTarget[static_cast<size_t>(snapTargetIndex(Emu51ImguiSnapTarget::Top))], topRect, false, Emu51ImguiSnapTarget::Top);
	addTiledRects(panesByTarget[static_cast<size_t>(snapTargetIndex(Emu51ImguiSnapTarget::Bottom))], bottomRect, false, Emu51ImguiSnapTarget::Bottom);
	addTiledRects(panesByTarget[static_cast<size_t>(snapTargetIndex(Emu51ImguiSnapTarget::Left))], leftRect, true, Emu51ImguiSnapTarget::Left);
	addTiledRects(panesByTarget[static_cast<size_t>(snapTargetIndex(Emu51ImguiSnapTarget::Right))], rightRect, true, Emu51ImguiSnapTarget::Right);
	addTiledRects(panesByTarget[static_cast<size_t>(snapTargetIndex(Emu51ImguiSnapTarget::Center))], centerRect, true, Emu51ImguiSnapTarget::Center);
}

void Emu51ImguiApp::applyWindowPlacement(const PaneKey& pane, const ImVec2& defaultSize, ImGuiWindowFlags* flags)
{
	FloatingPaneState* state = findFloatingPaneState(pane);
	if (state && state->restoreCollapsePending) {
		ImGui::SetNextWindowCollapsed(state->collapsed, ImGuiCond_Always);
		state->restoreCollapsePending = false;
	}
	for (const SnappedPaneRect& rect : snappedPaneRects_) {
		if (!(rect.pane == pane))
			continue;
		ImGui::SetNextWindowPos(ImVec2(rect.x, rect.y), ImGuiCond_Always);
		ImGui::SetNextWindowSize(ImVec2(rect.width, rect.height), ImGuiCond_Always);
		if (flags != nullptr)
			*flags |= ImGuiWindowFlags_NoResize;
		return;
	}

	if (state && state->restorePositionPending && state->position) {
		const auto* viewport = ImGui::GetMainViewport();
		if (viewport->WorkSize.x > 0 && viewport->WorkSize.y > 0) {
			const ImVec2 size = state->hasSize ? state->size : defaultSize;
			const auto work = viewport->WorkSize;
			const auto origin = viewport->WorkPos;
			const auto padding = ImMin(ImGui::GetStyle().DisplayWindowPadding, ImVec2(work.x * 0.5f, work.y * 0.5f));
			const ImVec2 min(origin.x + padding.x - size.x, origin.y + padding.y - ImGui::GetFrameHeight());
			const ImVec2 max(origin.x + work.x - padding.x, origin.y + work.y - padding.y);
			const ImVec2 position(viewport->Pos.x + state->position->x, viewport->Pos.y + state->position->y);
			ImGui::SetNextWindowPos(ImClamp(position, min, max), ImGuiCond_Always);
			state->restorePositionPending = false;
		}
	}
	if (state && state->restorePending && state->hasSize) {
		ImGui::SetNextWindowSize(state->size, ImGuiCond_Always);
		state->restorePending = false;
		return;
	}
	if (state && state->hasSize) {
		ImGui::SetNextWindowSize(state->size, ImGuiCond_FirstUseEver);
		return;
	}
	ImGui::SetNextWindowSize(defaultSize, ImGuiCond_FirstUseEver);
}

void Emu51ImguiApp::notePaneActivity(const PaneKey& pane, bool active)
{
	if (!active)
		return;
	activePane_ = pane;
	hasActivePane_ = true;
}

void Emu51ImguiApp::registerPaneWindow(const PaneKey& pane)
{
	ImGuiWindow* window = ImGui::GetCurrentWindowRead();
	if (window == nullptr || window->RootWindow == nullptr)
		return;

	const ImGuiID windowId = window->RootWindow->ID;
	for (PaneWindowBinding& binding : paneWindowBindings_) {
		if (!(binding.pane == pane))
			continue;
		binding.windowId = windowId;
		return;
	}
	paneWindowBindings_.push_back(PaneWindowBinding{pane, windowId});
}

void Emu51ImguiApp::capturePanePlacement(const PaneKey& pane)
{
	FloatingPaneState& state = ensureFloatingPaneState(pane);
	state.collapsed = ImGui::IsWindowCollapsed();
	if (snapTargetForPane(pane) != Emu51ImguiSnapTarget::None)
		return;

	const auto origin = ImGui::GetMainViewport()->Pos;
	const auto position = ImGui::GetWindowPos();
	if (!state.restorePositionPending) state.position = ImVec2(position.x - origin.x, position.y - origin.y);
	const ImVec2 size = ImGui::GetCurrentWindowRead()->SizeFull;
	if (!state.hasSize || state.size.x != size.x || state.size.y != size.y) {
		state.size = size;
		state.hasSize = true;
		markLayoutStateDirty();
	}
}

void Emu51ImguiApp::drawPaneSnapContextMenu(const PaneKey& pane)
{
	ImGuiWindow* window = ImGui::GetCurrentWindowRead();
	if (window == nullptr || window->RootWindow == nullptr)
		return;

	ImGui::PushID(static_cast<int>(pane.kind) * 1000 + pane.id);
	const ImRect titleBarRect = window->RootWindow->TitleBarRect();
	const bool titleBarHovered =
		titleBarRect.Contains(ImGui::GetIO().MousePos) &&
		ImGui::IsWindowHovered(ImGuiHoveredFlags_RootWindow);
	if (titleBarHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
		ImGui::OpenPopup("pane_snap_popup");

	if (ImGui::BeginPopup("pane_snap_popup")) {
		if (ImGui::MenuItem("Float"))
			snapPane(pane, Emu51ImguiSnapTarget::None);
		if (ImGui::MenuItem("Left"))
			snapPane(pane, Emu51ImguiSnapTarget::Left);
		if (ImGui::MenuItem("Right"))
			snapPane(pane, Emu51ImguiSnapTarget::Right);
		if (ImGui::MenuItem("Top"))
			snapPane(pane, Emu51ImguiSnapTarget::Top);
		if (ImGui::MenuItem("Bottom"))
			snapPane(pane, Emu51ImguiSnapTarget::Bottom);
		if (ImGui::MenuItem("Center"))
			snapPane(pane, Emu51ImguiSnapTarget::Center);
		ImGui::EndPopup();
	}
	ImGui::PopID();
}

bool Emu51ImguiApp::paneExists(const PaneKey& pane) const
{
	switch (pane.kind) {
	case PaneKey::Kind::Registers:
		return registersOpen_;
	case PaneKey::Kind::Peripherals:
		return peripheralsOpen_;
	case PaneKey::Kind::SerialTerminal:
		return serialTerminalOpen_;
	case PaneKey::Kind::Project:
		return projectPanelOpen_;
	case PaneKey::Kind::AssemblyEditor:
		return assemblyEditorState_.open;
	case PaneKey::Kind::Disassembly:
		for (const DisassemblyView& view : disassemblyViews_) {
			if (view.id == pane.id && view.open)
				return true;
		}
		return false;
	case PaneKey::Kind::InternalRamMonitor:
		return internalRamMonitor_.open;
	case PaneKey::Kind::Monitor:
		for (const MonitorView& view : monitorViews_) {
			if (view.id == pane.id && view.open)
				return true;
		}
		return false;
	default:
		return false;
	}
}

bool Emu51ImguiApp::paneFromWindowId(ImGuiID windowId, PaneKey* pane) const
{
	for (const PaneWindowBinding& binding : paneWindowBindings_) {
		if (binding.windowId != windowId)
			continue;
		if (pane != nullptr)
			*pane = binding.pane;
		return true;
	}
	return false;
}

Emu51ImguiSnapTarget Emu51ImguiApp::snapTargetForPane(const PaneKey& pane) const
{
	for (const PaneSnapAssignment& assignment : paneSnapAssignments_) {
		if (assignment.pane == pane)
			return assignment.target;
	}
	return Emu51ImguiSnapTarget::None;
}

Emu51ImguiApp::PaneSnapAssignment* Emu51ImguiApp::findPaneSnapAssignment(const PaneKey& pane)
{
	for (PaneSnapAssignment& assignment : paneSnapAssignments_) {
		if (assignment.pane == pane)
			return &assignment;
	}
	return nullptr;
}

const Emu51ImguiApp::PaneSnapAssignment* Emu51ImguiApp::findPaneSnapAssignment(const PaneKey& pane) const
{
	for (const PaneSnapAssignment& assignment : paneSnapAssignments_) {
		if (assignment.pane == pane)
			return &assignment;
	}
	return nullptr;
}

Emu51ImguiApp::FloatingPaneState* Emu51ImguiApp::findFloatingPaneState(const PaneKey& pane)
{
	for (FloatingPaneState& state : floatingPaneStates_) {
		if (state.pane == pane)
			return &state;
	}
	return nullptr;
}

const Emu51ImguiApp::FloatingPaneState* Emu51ImguiApp::findFloatingPaneState(const PaneKey& pane) const
{
	for (const FloatingPaneState& state : floatingPaneStates_) {
		if (state.pane == pane)
			return &state;
	}
	return nullptr;
}

Emu51ImguiApp::FloatingPaneState& Emu51ImguiApp::ensureFloatingPaneState(const PaneKey& pane)
{
	if (FloatingPaneState* state = findFloatingPaneState(pane))
		return *state;
	floatingPaneStates_.push_back(FloatingPaneState{pane});
	return floatingPaneStates_.back();
}

void Emu51ImguiApp::markLayoutStateDirty()
{
	layoutStateDirty_ = true;
}

void Emu51ImguiApp::loadState(const fs::path& path)
{
    statePath_ = path;
    statePersistenceBlocked_ = false;
    if (path.empty()) return;
    Emu51UiState state;
    const auto result = emu51LoadUiState(path,state);
    if (result == Emu51UiStateLoad::Loaded) {
        applyUiState(state);
    } else if (result == Emu51UiStateLoad::Missing) {
        if (emu51ImportLegacyUiState(path.parent_path(),state)) {
            applyUiState(state);
            saveState();
        }
    } else {
        // Preserve unreadable/corrupt or newer state instead of overwriting it.
        statePersistenceBlocked_ = true;
        statusMessage_ = "Cannot read state.json; layout and history changes will not be saved this session";
    }
}

bool Emu51ImguiApp::saveState() const
{
    if (!persistState_ || statePath_.empty() || statePersistenceBlocked_) return false;
    std::string json;
    if (!emu51EncodeUiState(captureUiState(),json)) return false;
    const bool saved = withoutExecutionLock([&] { return emu51WriteTextFileAtomically(statePath_,json); });
    const_cast<Emu51ImguiApp*>(this)->layoutStateDirty_ = !saved;
    return saved;
}

Emu51UiState Emu51ImguiApp::captureUiState() const
{
    Emu51UiState state;
    static_cast<Emu51UiLayout&>(state) = captureUiLayout();
    state.recentPrograms = recentProgramPaths_;
    state.recentProjects = recentProjectPaths_;
    return state;
}

Emu51UiLayout Emu51ImguiApp::captureUiLayout() const
{
    Emu51UiLayout state;
    state.leftWidth = snapLayoutPreferences_.leftWidthFraction;
    state.rightWidth = snapLayoutPreferences_.rightWidthFraction;
    state.topHeight = snapLayoutPreferences_.topHeightFraction;
    state.bottomHeight = snapLayoutPreferences_.bottomHeightFraction;
    state.registersVisible = registersOpen_;
    state.peripheralInspector = peripheralEditor_.inspectorVisible();
    state.collapsedEdgeDocks = collapsedEdgeDocks_;
    state.panes.clear();
    auto writePane = [this,&state](const PaneKey& pane, bool open) -> Emu51UiPaneState& {
        const auto* assignment = findPaneSnapAssignment(pane);
        const auto* floating = findFloatingPaneState(pane);
        state.panes.push_back({paneKindLabel(pane.kind),pane.id,
            snapTargetPersistedLabel(assignment ? assignment->target : Emu51ImguiSnapTarget::None),
            assignment ? assignment->share : 1.0,
            floating && floating->hasSize ? floating->size.x : 0.0,
            floating && floating->hasSize ? floating->size.y : 0.0});
        auto& saved = state.panes.back();
        saved.open = open;
        saved.collapsed = floating && floating->collapsed;
        if (floating && floating->position) {
            saved.positioned = true;
            saved.x = floating->position->x;
            saved.y = floating->position->y;
        }
        return saved;
    };
    writePane({PaneKey::Kind::Registers,0},registersOpen_);
    writePane({PaneKey::Kind::Peripherals,0},peripheralsOpen_);
    writePane({PaneKey::Kind::Project,0},projectPanelOpen_);
    writePane({PaneKey::Kind::AssemblyEditor,0},assemblyEditorState_.open);
    writePane({PaneKey::Kind::Debugger,0},true);
    writePane({PaneKey::Kind::SerialTerminal,0},true);
    writePane({PaneKey::Kind::InternalRamMonitor,0},internalRamMonitor_.open).address = internalRamMonitor_.start;
    for (const auto& view : disassemblyViews_) if (view.open) {
        auto& saved = writePane({PaneKey::Kind::Disassembly,view.id},view.open);
        saved.followPc = view.followPc;
        saved.address = view.start;
    }
    for (const auto& view : monitorViews_) if (view.open) {
        auto& saved = writePane({PaneKey::Kind::Monitor,view.id},view.open);
        saved.memoryKind = static_cast<int>(view.memoryKind);
        saved.address = view.start;
    }
    return state;
}

void Emu51ImguiApp::applyUiLayout(const Emu51UiLayout& state, bool restoreWorkspace)
{
    snapLayoutPreferences_ = {static_cast<float>(state.leftWidth),static_cast<float>(state.rightWidth),
        static_cast<float>(state.topHeight),static_cast<float>(state.bottomHeight)};
    registersOpen_ = state.registersVisible;
    peripheralEditor_.setInspectorVisible(state.peripheralInspector);
    collapsedEdgeDocks_ = state.collapsedEdgeDocks;
    paneSnapAssignments_.clear();
    floatingPaneStates_.clear();
    disassemblyViews_.clear();
    nextDisassemblyViewId_ = 1;
    if (restoreWorkspace) {
        monitorViews_.clear();
        nextMonitorViewId_ = 1;
        paneWindowBindings_.clear();
        hasActivePane_ = dragSnapActive_ = false;
        dragSnapTarget_ = Emu51ImguiSnapTarget::None;
    }
    for (const auto& saved : state.panes) {
        PaneKey::Kind kind;
        Emu51ImguiSnapTarget target;
        if (!parsePaneKind(saved.kind,&kind) || !parseSnapTarget(saved.target,&target)) continue;
        const PaneKey pane{kind,saved.id};
        if (target != Emu51ImguiSnapTarget::None)
            paneSnapAssignments_.push_back({pane,target,static_cast<float>(saved.share)});
        FloatingPaneState floating;
        floating.pane = pane;
        floating.size = ImVec2(static_cast<float>(saved.width),static_cast<float>(saved.height));
        floating.hasSize = saved.width > 0 && saved.height > 0;
        if (restoreWorkspace) {
            if (saved.positioned) floating.position = ImVec2(static_cast<float>(saved.x),static_cast<float>(saved.y));
            floating.restorePositionPending = saved.positioned && target == Emu51ImguiSnapTarget::None;
            floating.restorePending = floating.hasSize;
            floating.collapsed = saved.collapsed;
            floating.restoreCollapsePending = true;
        }
        floatingPaneStates_.push_back(floating);
        if (kind == PaneKey::Kind::Disassembly && saved.open) {
            DisassemblyView view;
            view.id = saved.id; view.followPc = saved.followPc;
            if (restoreWorkspace) view.start = view.end = static_cast<std::uint16_t>(saved.address);
            disassemblyViews_.push_back(view);
            nextDisassemblyViewId_ = std::max(nextDisassemblyViewId_,saved.id+1);
        }
        if (!restoreWorkspace) continue;
        switch (kind) {
        case PaneKey::Kind::Peripherals: peripheralsOpen_ = saved.open; break;
        case PaneKey::Kind::Project: projectPanelOpen_ = saved.open; break;
        case PaneKey::Kind::AssemblyEditor: assemblyEditorState_.open = saved.open; break;
        case PaneKey::Kind::InternalRamMonitor:
            internalRamMonitor_.open = saved.open;
            internalRamMonitor_.start = clampMonitorAddress(internalRamMonitor_, static_cast<std::uint16_t>(saved.address));
            break;
        case PaneKey::Kind::Monitor:
            if (saved.open) {
                MonitorView view;
                view.id = saved.id;
                view.memoryKind = static_cast<MonitorView::MemoryKind>(saved.memoryKind);
                view.start = clampMonitorAddress(view, static_cast<std::uint16_t>(saved.address));
                monitorViews_.push_back(view);
                nextMonitorViewId_ = std::max(nextMonitorViewId_,saved.id+1);
            }
            break;
        default: break;
        }
    }
}

void Emu51ImguiApp::applyUiState(const Emu51UiState& state)
{
    applyUiLayout(state);
    auto restorePaths = [](const auto& source, auto& target, bool programs) {
        target.clear();
        for (const auto& saved : source) {
            const auto path = normalizedProgramPath(fs::path(saved));
            if (path.empty() || (programs && isTemporaryAssemblyEditorOutputPath(fs::path(path)))) continue;
            if (std::find(target.begin(),target.end(),path) == target.end()) target.push_back(path);
            if (target.size() == kMaxRecentProgramFiles) break;
        }
    };
    restorePaths(state.recentPrograms,recentProgramPaths_,true);
    restorePaths(state.recentProjects,recentProjectPaths_,false);
    layoutStateDirty_ = false;
}

void Emu51ImguiApp::rememberRecentProgramPath(const fs::path& path)
{
	const std::string normalized = normalizedProgramPath(path);
	if (normalized.empty())
		return;
	if (isTemporaryAssemblyEditorOutputPath(fs::path(normalized)))
		return;

	recentProgramPaths_.erase(
		std::remove(recentProgramPaths_.begin(), recentProgramPaths_.end(), normalized),
		recentProgramPaths_.end());
	recentProgramPaths_.insert(recentProgramPaths_.begin(), normalized);
	if (recentProgramPaths_.size() > kMaxRecentProgramFiles)
		recentProgramPaths_.resize(kMaxRecentProgramFiles);
	saveState();
}

void Emu51ImguiApp::snapPane(const PaneKey& pane, Emu51ImguiSnapTarget target)
{
	if (target != Emu51ImguiSnapTarget::None && snapTargetForPane(pane) == Emu51ImguiSnapTarget::None) {
		FloatingPaneState& state = ensureFloatingPaneState(pane);
		if (!state.hasSize) {
			state.size = ImGui::GetWindowSize();
			state.hasSize = true;
		}
	}
	if (target == Emu51ImguiSnapTarget::None) {
		FloatingPaneState& state = ensureFloatingPaneState(pane);
		state.restorePending = state.hasSize;
	}
	setPaneSnapTarget(pane, target);
	setEdgeDockCollapsed(target, false);
	if (target == Emu51ImguiSnapTarget::None)
		statusMessage_ = "pane released from snapped layout";
	else
		statusMessage_ = std::string("pane snapped to ") + snapTargetLabel(target);
	markLayoutStateDirty();
}

void Emu51ImguiApp::setPaneSnapTarget(const PaneKey& pane, Emu51ImguiSnapTarget target)
{
	for (PaneSnapAssignment& assignment : paneSnapAssignments_) {
		if (!(assignment.pane == pane))
			continue;
		if (target == Emu51ImguiSnapTarget::None) {
			removePaneSnapTarget(pane);
			return;
		}
		assignment.target = target;
		assignment.share = 1.0f;
		return;
	}

	if (target == Emu51ImguiSnapTarget::None)
		return;

	paneSnapAssignments_.push_back(PaneSnapAssignment{pane, target, 1.0f});
}

void Emu51ImguiApp::removePaneSnapTarget(const PaneKey& pane)
{
	paneSnapAssignments_.erase(
		std::remove_if(paneSnapAssignments_.begin(), paneSnapAssignments_.end(),
			[&pane](const PaneSnapAssignment& assignment) {
				return assignment.pane == pane;
			}),
		paneSnapAssignments_.end());
}

void Emu51ImguiApp::appendSnappedPane(std::array<std::vector<PaneKey>, 5>& panesByTarget, const PaneKey& pane) const
{
	if (!paneExists(pane))
		return;

	const int index = snapTargetIndex(snapTargetForPane(pane));
	if (index < 0)
		return;
	panesByTarget[static_cast<size_t>(index)].push_back(pane);
}

