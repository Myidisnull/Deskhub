#include "Tests.h"
#include "support/TestSupport.h"

#include "deskhub/ui/Strings.h"
#include "deskhub/ui/TransferView.h"

#include <cstdio>
#include <string>

using namespace deskhub;

namespace {

TransferProgress MakeProgress(uint16_t index, uint16_t count, uint64_t bytes, uint64_t size) {
    TransferProgress progress;
    progress.fileIndex = index;
    progress.fileCount = count;
    progress.batchBytes = bytes;
    progress.batchSize = size;
    progress.name = "notes.txt";
    return progress;
}

void TestSendingReadsAsActiveWithoutAnOutcome() {
    std::printf("[transferview] a batch on the wire shows progress and no verdict...\n");
    const ui::TransferView view = ui::TransferViewOf(FileSenderState::Sending,
        TransferReason::Accepted, MakeProgress(1, 4, 250, 1000));

    Check(view.active && !view.done && !view.failed, "sending is active and unfinished");
    Check(!view.Idle(), "an active transfer is not idle");
    Check(view.Percent() == 25, "a quarter of the bytes is twenty five percent");
    Check(view.Step() == "2/4", "the step counts files from one");
    Check(view.StatusLine() == "notes.txt", "the file being sent is the status line");
    Check(view.message.empty(), "no outcome is spelled out until the batch settles");
}

void TestOfferingCountsAsActive() {
    std::printf("[transferview] waiting on the host to accept still reads as active...\n");
    const ui::TransferView view = ui::TransferViewOf(FileSenderState::Offering,
        TransferReason::Accepted, TransferProgress{});

    Check(view.active, "an offer in flight is an active transfer");
    Check(view.Percent() == 0, "nothing has moved yet");
    Check(view.Step().empty(), "an empty batch has no step to show");
    Check(view.StatusLine() == ui::kTransferSending, "the status falls back to the sending line");
}

void TestDoneCarriesTheHappyOutcome() {
    std::printf("[transferview] a finished batch reports every file arrived...\n");
    const ui::TransferView view = ui::TransferViewOf(FileSenderState::Done,
        TransferReason::Accepted, MakeProgress(2, 3, 900, 900));

    Check(!view.active && view.done && !view.failed, "done is settled and not a failure");
    Check(view.Percent() == 100, "every byte is through");
    Check(view.Step() == "3/3", "the step stops at the last file");
    Check(view.message == ui::kTransferDone, "the outcome text is the shared done line");
    Check(view.StatusLine() == ui::kTransferDone, "a settled transfer shows its outcome");
}

void TestRefusalAndFailureBothReadAsFailed() {
    std::printf("[transferview] a refused or broken batch reports why it stopped...\n");
    const ui::TransferView refused = ui::TransferViewOf(FileSenderState::Refused,
        TransferReason::NotAccepting, TransferProgress{});
    Check(refused.failed && !refused.done, "a refusal is a failure, not a success");
    Check(refused.message == ui::kTransferNotAccepting, "the refusal reason is spelled out");

    const ui::TransferView broken = ui::TransferViewOf(FileSenderState::Failed,
        TransferReason::LinkLost, TransferProgress{});
    Check(broken.failed, "a dropped link is a failure");
    Check(broken.message == ui::kTransferLinkLost, "the lost link is spelled out");
}

void TestIdleTransferIsFullyBlank() {
    std::printf("[transferview] nothing sent yet leaves the whole panel empty...\n");
    const ui::TransferView view = ui::TransferViewOf(FileSenderState::Idle,
        TransferReason::Accepted, TransferProgress{});

    Check(view.Idle(), "an untouched sender is idle");
    Check(view.Percent() == 100, "an idle bar is not a stalled one");
    Check(view.StatusLine().empty(), "an idle transfer says nothing");
    Check(view == ui::TransferView{}, "an idle view equals a default-built one");
}

void TestAChangedKeyIsItsOwnKindOfStop() {
    std::printf("[transferview] a changed host key is distinguishable from a plain failure...\n");
    const ui::TransferView plain = ui::TransferViewOf(FileSenderState::Failed,
        TransferReason::LinkLost, TransferProgress{});
    Check(!plain.keyChanged && plain.fingerprint.empty(),
        "an ordinary failure carries no key question");

    ui::TransferView asked = plain;
    asked.keyChanged = true;
    asked.fingerprint = "SHA256:abc";
    Check(!(asked == plain), "the key question changes what the view is");
    Check(asked.failed, "and it still reads as a stopped transfer");
}

void TestProgressBeyondTheBatchSizeStaysWhole() {
    std::printf("[transferview] a batch that overruns its estimate still caps at full...\n");
    const ui::TransferView view = ui::TransferViewOf(FileSenderState::Sending,
        TransferReason::Accepted, MakeProgress(9, 2, 4000, 1000));

    Check(view.Percent() == 100, "the fraction never passes one");
    Check(view.Step() == "2/2", "the step never passes the file count");
}

void TestFileSizesReadLikeAFileManager() {
    std::printf("[transferview] byte counts read as B, KB, MB with one decimal...\n");
    Check(ui::FileSizeText(0) == "0 B", "nothing is zero bytes");
    Check(ui::FileSizeText(999) == "999 B", "under a thousand stays in bytes");
    Check(ui::FileSizeText(1000) == "1.0 KB", "a round thousand is one kilobyte");
    Check(ui::FileSizeText(1500) == "1.5 KB", "the decimal is the leading remainder digit");
    Check(ui::FileSizeText(20'000) == "20 KB", "ten and over drops the decimal");
    Check(ui::FileSizeText(3'400'000) == "3.4 MB", "millions read as megabytes");
    Check(ui::FileSizeText(9'999'999'999'999'999ULL).find("TB") != std::string::npos,
        "the largest unit is terabytes");
}

void TestShareSummaryNamesEveryTenant() {
    std::printf("[transferview] the sharing banner names screen, shell and files...\n");
    Check(ui::ShareSummaryLine(false, false, false, 47777).empty(),
        "sharing nothing says nothing");
    Check(ui::ShareSummaryLine(false, false, true, 47777) == "Files on UDP port 47777.",
        "files alone still names the port");
    Check(ui::ShareSummaryLine(true, true, true, 47777) ==
              "Screen \xC2\xB7 Terminal \xC2\xB7 Files on UDP port 47777.",
        "all three are listed in order");
    Check(ui::ShareSummaryLine(true, true, 47777) == ui::ShareSummaryLine(true, true, false, 47777),
        "the older two-way call means no files");
}

}

void RunTransferViewTests() {
    TestSendingReadsAsActiveWithoutAnOutcome();
    TestOfferingCountsAsActive();
    TestDoneCarriesTheHappyOutcome();
    TestRefusalAndFailureBothReadAsFailed();
    TestIdleTransferIsFullyBlank();
    TestAChangedKeyIsItsOwnKindOfStop();
    TestProgressBeyondTheBatchSizeStaysWhole();
    TestFileSizesReadLikeAFileManager();
    TestShareSummaryNamesEveryTenant();
}
