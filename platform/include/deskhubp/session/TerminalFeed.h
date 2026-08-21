#pragma once
#include "deskhub/terminal/KeyEncoder.h"
#include "deskhubp/session/TerminalHost.h"
#include "deskhubp/session/TerminalViewer.h"

#include <cstdint>

namespace deskhubp {

class TerminalFeed {
public:
    virtual ~TerminalFeed() = default;
    virtual bool Alive() const = 0;
    virtual TerminalSnapshot Snapshot(size_t scrollOffset) const = 0;
    virtual void SendKey(const deskhub::term::TermKeyEvent& key) = 0;
    virtual void Resize(deskhub::TermSize size) = 0;
    virtual void Shutdown() = 0;
};

class RemoteTerminalFeed final : public TerminalFeed {
public:
    TerminalViewer viewer{};

    bool Alive() const override {
        return viewer.Running();
    }
    TerminalSnapshot Snapshot(size_t scrollOffset) const override {
        return viewer.Snapshot(scrollOffset);
    }
    void SendKey(const deskhub::term::TermKeyEvent& key) override {
        viewer.SendKey(key);
    }
    void Resize(deskhub::TermSize size) override {
        viewer.Resize(size);
    }
    void Shutdown() override {
        viewer.Stop();
    }
};

class LocalShellFeed final : public TerminalFeed {
public:
    LocalShellFeed(TerminalHost& host, uint32_t termId) : host_(&host), termId_(termId) {}

    bool Alive() const override {
        return host_->LocalAlive(termId_);
    }
    TerminalSnapshot Snapshot(size_t scrollOffset) const override {
        return host_->LocalSnapshot(termId_, scrollOffset);
    }
    void SendKey(const deskhub::term::TermKeyEvent& key) override {
        host_->SendLocalKey(termId_, key);
    }
    void Resize(deskhub::TermSize size) override {
        host_->ResizeLocal(termId_, size);
    }
    void Shutdown() override {
        host_->CloseLocal(termId_);
    }

private:
    TerminalHost* host_;
    uint32_t termId_;
};

}
