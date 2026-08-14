#pragma once

class wxWindow;

enum class BackgroundPromptResult { kConfirm,
    kClose };

BackgroundPromptResult ShowBackgroundPrompt(wxWindow* parent, bool& runInBackground);
