#include "Tests.h"
#include "support/TestSupport.h"

#include "deskhub/media/SourceLabel.h"

#include <cstdio>
#include <string>

using deskhub::media::SharedSourceLabel;
using deskhub::media::SourceName;
using deskhub::media::SourcePickerLabel;
using deskhub::media::SourceSizeLabel;

namespace {

void TestSizeLabel() {
    std::printf("[label] the size a user reads is the size that is being sent...\n");
    Check(SourceSizeLabel(1920, 1080) == "1920x1080", "width comes first, height second");
    Check(SourceSizeLabel(0, 0) == "0x0", "a source that has not reported a size still labels");
    Check(SourceSizeLabel(3840, 2160) == "3840x2160", "4K is not abbreviated or rounded");
}

void TestUnnamedSourcesStayDistinguishable() {
    std::printf("[label] a source with no name is still one the user can tell apart...\n");
    Check(SourceName("Display 1", 0) == "Display 1", "a real name is used as-is");
    Check(SourceName("", 0) == "Source 0", "an empty name falls back to the id");
    Check(SourceName("", 7) == "Source 7", "the fallback carries the id that identifies it");
    Check(SourceName("", 255) == "Source 255", "the id is printed unsigned, never as a char");
}

void TestPickerLabelShowsBothNameAndSize() {
    std::printf("[label] the picker row names the source and the size it will stream...\n");
    Check(SourcePickerLabel("Display 1", 0, 2560, 1440) == "Display 1 (2560x1440)",
        "named source: name then size in brackets");
    Check(SourcePickerLabel("", 2, 800, 600) == "Source 2 (800x600)",
        "unnamed source keeps the same shape so rows line up");
}

void TestSharedLabelCallsOutAViewer() {
    std::printf("[label] the sharing list says whether someone is actually watching...\n");
    const std::string idle = SharedSourceLabel("Display 1", 1920, 1080, false);
    Check(idle == "Display 1  (1920x1080)", "nobody watching: just the name and size");

    const std::string busy = SharedSourceLabel("Display 1", 1920, 1080, true);
    Check(busy == "Display 1  (1920x1080, viewer connected)",
        "someone watching: the row says so, so the user is never surprised");
    Check(busy.find(idle.substr(0, idle.size() - 1)) == 0,
        "the viewer note is appended, so the row does not jump around when it appears");
}

}

void RunSourceLabelTests() {
    TestSizeLabel();
    TestUnnamedSourcesStayDistinguishable();
    TestPickerLabelShowsBothNameAndSize();
    TestSharedLabelCallsOutAViewer();
}
