#include "ViewerRun.h"

#include "Output.h"

namespace deskhubcli {

ExitCode RunViewers(const ViewRequest&) {
    PrintError(
        "This build cannot show a remote screen - it has no window layer for this system. "
        "Use 'shell' for a prompt, or the desktop app to watch a screen.");
    return ExitCode::Unsupported;
}

}
