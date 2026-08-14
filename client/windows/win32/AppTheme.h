#pragma once

#include <wx/settings.h>
#include <wx/colour.h>

struct AppTheme {
    wxColour appBg;
    wxColour sidebarBg;
    wxColour sidebarHover;
    wxColour navSelectedBg;
    wxColour accent;
    wxColour navText;
    wxColour sidebarFootnote;
    wxColour headingText;
    wxColour mutedText;
    wxColour online;
    wxColour offline;
    wxColour warning;
    wxColour rowLine;
    wxColour viewerRowBg;
    wxColour bannerIdleBg;
    wxColour bannerLiveBg;
    wxColour bannerBusyBg;
    wxColour pageBg;
    wxColour surfaceBg;
    wxColour inputBg;
    wxColour inputBorder;
    wxColour dialogBg;
    wxColour buttonBg;
    wxColour buttonBorder;
    wxColour dangerBg;
};

inline const AppTheme& AppThemeCurrent() {
    static const AppTheme light = {
        .appBg = wxColour(243, 243, 243),
        .sidebarBg = wxColour(243, 243, 243),
        .sidebarHover = wxColour(230, 230, 230),
        .navSelectedBg = wxColour(226, 226, 226),
        .accent = wxColour(0, 120, 212),
        .navText = wxColour(92, 92, 92),
        .sidebarFootnote = wxColour(138, 138, 138),
        .headingText = wxColour(26, 26, 26),
        .mutedText = wxColour(96, 96, 96),
        .online = wxColour(15, 137, 62),
        .offline = wxColour(196, 43, 28),
        .warning = wxColour(157, 93, 0),
        .rowLine = wxColour(229, 229, 229),
        .viewerRowBg = wxColour(250, 250, 250),
        .bannerIdleBg = wxColour(237, 237, 237),
        .bannerLiveBg = wxColour(223, 246, 221),
        .bannerBusyBg = wxColour(225, 239, 252),
        .pageBg = wxColour(243, 243, 243),
        .surfaceBg = wxColour(255, 255, 255),
        .inputBg = wxColour(255, 255, 255),
        .inputBorder = wxColour(209, 209, 209),
        .dialogBg = wxColour(255, 255, 255),
        .buttonBg = wxColour(243, 243, 243),
        .buttonBorder = wxColour(209, 209, 209),
        .dangerBg = wxColour(196, 43, 28),
    };
    static const AppTheme dark = {
        .appBg = wxColour(32, 32, 32),
        .sidebarBg = wxColour(32, 32, 32),
        .sidebarHover = wxColour(45, 45, 45),
        .navSelectedBg = wxColour(50, 50, 50),
        .accent = wxColour(0, 120, 212),
        .navText = wxColour(180, 180, 180),
        .sidebarFootnote = wxColour(140, 140, 140),
        .headingText = wxColour(255, 255, 255),
        .mutedText = wxColour(154, 154, 154),
        .online = wxColour(76, 194, 114),
        .offline = wxColour(255, 153, 145),
        .warning = wxColour(252, 225, 0),
        .rowLine = wxColour(58, 58, 58),
        .viewerRowBg = wxColour(40, 40, 40),
        .bannerIdleBg = wxColour(44, 44, 44),
        .bannerLiveBg = wxColour(32, 56, 40),
        .bannerBusyBg = wxColour(32, 48, 64),
        .pageBg = wxColour(32, 32, 32),
        .surfaceBg = wxColour(44, 44, 44),
        .inputBg = wxColour(44, 44, 44),
        .inputBorder = wxColour(70, 70, 70),
        .dialogBg = wxColour(40, 40, 40),
        .buttonBg = wxColour(50, 50, 50),
        .buttonBorder = wxColour(70, 70, 70),
        .dangerBg = wxColour(180, 60, 50),
    };
    return wxSystemSettings::GetAppearance().IsDark() ? dark : light;
}
