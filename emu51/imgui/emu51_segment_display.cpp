// SPDX-License-Identifier: GPL-2.0-only
#include "emu51_segment_display.h"
#include "imgui.h"
#include <algorithm>
#include <array>
#include <cmath>
namespace {
struct Line { float x1,y1,x2,y2; };
constexpr Line seven[]={{.18f,0,.82f,0},{1,.18f,1,.82f},{1,1.18f,1,1.82f},{.18f,2,.82f,2},
    {0,1.18f,0,1.82f},{0,.18f,0,.82f},{.18f,1,.82f,1}};
constexpr Line fourteen[]={{.18f,0,.82f,0},{1,.18f,1,.82f},{1,1.18f,1,1.82f},{.18f,2,.82f,2},
    {0,1.18f,0,1.82f},{0,.18f,0,.82f},{.15f,1,.42f,1},{.58f,1,.85f,1},
    {.14f,.18f,.4f,.82f},{.5f,.18f,.5f,.82f},{.86f,.18f,.6f,.82f},
    {.6f,1.18f,.86f,1.82f},{.5f,1.18f,.5f,1.82f},{.4f,1.18f,.14f,1.82f}};
constexpr Line sixteen[]={{.14f,0,.42f,0},{.58f,0,.86f,0},{1,.18f,1,.82f},{1,1.18f,1,1.82f},
    {.58f,2,.86f,2},{.14f,2,.42f,2},{0,1.18f,0,1.82f},{0,.18f,0,.82f},
    {.14f,1,.42f,1},{.58f,1,.86f,1},{.14f,.18f,.4f,.82f},{.5f,.18f,.5f,.82f},{.86f,.18f,.6f,.82f},
    {.6f,1.18f,.86f,1.82f},{.5f,1.18f,.5f,1.82f},{.4f,1.18f,.14f,1.82f}};
ImU32 shade(int color,float brightness) {
    constexpr ImVec4 colors[]={{1,.1f,.03f,1},{.1f,1,.2f,1},{1,.6f,.03f,1},{.15f,.5f,1,1},{.9f,.95f,1,1}};
    auto rgb=colors[std::clamp(color,0,4)];const float gain=.055f+.945f*std::sqrt(std::clamp(brightness,0.0f,1.0f));
    rgb.x*=gain;rgb.y*=gain;rgb.z*=gain;return ImGui::ColorConvertFloat4ToU32(rgb);
}
void segment(ImDrawList* draw,ImVec2 base,float scale,const Line& line,ImU32 color) {
    const ImVec2 a(base.x+line.x1*scale,base.y+line.y1*scale),b(base.x+line.x2*scale,base.y+line.y2*scale);
    const float length=std::hypot(b.x-a.x,b.y-a.y),thickness=scale*.065f;
    const ImVec2 along((b.x-a.x)/length*thickness,(b.y-a.y)/length*thickness),normal(-along.y,along.x);
    const ImVec2 points[]={{a.x-along.x,a.y-along.y},{a.x+normal.x,a.y+normal.y},{b.x+normal.x,b.y+normal.y},
        {b.x+along.x,b.y+along.y},{b.x-normal.x,b.y-normal.y},{a.x-normal.x,a.y-normal.y}};
    draw->AddConvexPolyFilled(points,6,color);
}
}
void emu51DrawSegmentDisplay(const e51p_segment_snapshot& state,int style,int color,bool instantaneous,bool colon,float zoom)
{
    zoom=std::clamp(zoom,.1f,16.0f);
    const auto origin=ImGui::GetCursorScreenPos();auto* draw=ImGui::GetWindowDrawList();
    const auto level=[&](unsigned digit,unsigned bit) { return instantaneous ? ((state.instantaneous[digit]&(1u<<bit)) ? 1.0f : 0.0f) : state.brightness[digit][bit]*static_cast<float>(state.digits); };
    if (!style) {
        const float scale=zoom*std::min(1.0f,std::max(.25f,(ImGui::GetContentRegionAvail().x/zoom-12)/(static_cast<float>(state.segments)*18.0f)));
        const float width=static_cast<float>(state.segments*18)*scale+12*zoom,height=55*scale+12*zoom;
        draw->AddRectFilled(origin,{origin.x+width,origin.y+height},IM_COL32(12,14,18,255),4*zoom);
        for (unsigned s=0;s<state.segments;++s) draw->AddRectFilled({origin.x+6*zoom+static_cast<float>(s*18)*scale,origin.y+6*zoom},
            {origin.x+6*zoom+static_cast<float>(s*18+14)*scale,origin.y+height-6*zoom},shade(color,level(0,s)),2*zoom);
        ImGui::Dummy({width,height});return;
    }
    const unsigned count=std::clamp(state.digits,1u,8u), bits=style==14 ? 14u : style==16 ? 16u : 7u;
    // Wrap long displays on narrow docks, preserving a readable minimum digit size.
    const float available=std::max(70.0f,ImGui::GetContentRegionAvail().x/zoom);
    const unsigned columns=std::max(1u,std::min(count,static_cast<unsigned>(available/62)));
    const float scale=zoom*std::min(35.0f,(available-16)/(static_cast<float>(columns)*1.65f));
    const float width=static_cast<float>(columns)*1.65f*scale+16*zoom,height=static_cast<float>((count+columns-1)/columns)*2.8f*scale+16*zoom;
    draw->AddRectFilled(origin,{origin.x+width,origin.y+height},IM_COL32(12,14,18,255),4*zoom);
    for (unsigned d=0;d<count;++d) {
        const ImVec2 at(origin.x+10*zoom+static_cast<float>(d%columns)*1.65f*scale,origin.y+10*zoom+static_cast<float>(d/columns)*2.8f*scale);
        for (unsigned bit=0;bit<bits;++bit) {
            const auto& line=style==14 ? fourteen[bit] : style==16 ? sixteen[bit] : seven[bit];
            segment(draw,at,scale,line,shade(color,bit<state.segments ? level(d,bit) : 0));
        }
        draw->AddCircleFilled({at.x+1.25f*scale,at.y+2*scale},scale*.075f,shade(color,bits<state.segments ? level(d,bits) : 0));
        if (d==1&&colon) for (unsigned dot=0;dot<2;++dot) {
            const float brightness=instantaneous ? ((state.colon&(1u<<dot)) ? 1.0f : 0.0f) : state.colon_brightness[dot]*static_cast<float>(count);
            // Center between digit 2's right edge (1.0) and digit 3's left edge (1.65).
            draw->AddCircleFilled({at.x+(1.0f+1.65f)*.5f*scale,at.y+(.65f+.7f*static_cast<float>(dot))*scale},scale*.075f,shade(color,brightness));
        }
    }
    ImGui::Dummy({width,height});
}
