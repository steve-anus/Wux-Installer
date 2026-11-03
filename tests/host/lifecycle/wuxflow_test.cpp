#include "check.h"
#include "gui/MessageBox.h"
#include "menu/WuxExtractThread.h"
#include "menu/WuxFlow.h"

namespace {
using namespace host;
using State = WuxFlow::State;

Events since(std::size_t start)
{
    return Events(model.events.begin() + start, model.events.end());
}

template<typename Action>
void change(Check &check, WuxFlow &flow, State next, Action action)
{
    const State previous = flow.state();
    const auto start = model.events.size();
    action();
    const auto events = since(start);
    const bool crossing = (previous == State::Idle) != (next == State::Idle);
    const std::string menu = next == State::Idle ? "menu on" : "menu off";
    std::string line = std::string("log: WuxFlow: ") + WuxFlow::stateName(previous) +
                       " -> " + WuxFlow::stateName(next);
    if(crossing) line += ", " + menu;
    check.require(flow.state() == next, "accepted transition must update the state");
    check.equal(events, crossing ? Events{line, menu} : Events{line}, "accepted transition events");
}

template<typename Body>
bool run(const std::string &name, Body body)
{
    reset();
    Check check;
    body(check);
    for(const auto &reason : model.violations) check.require(false, reason);
    return check.report(name);
}
}

int main()
{
    bool passed = true;
    passed &= run("gate extraction to install", [](Check &check) {
        WuxFlow flow;
        change(check, flow, State::WuxExtract, [&] {
            check.require(flow.beginExtraction(), "Idle must accept extraction");
        });
        change(check, flow, State::WuxInstall, [&] { flow.extractionFinished(true); });
        change(check, flow, State::Idle, [&] { flow.flowFinished(); });
    });
    passed &= run("gate extraction to error", [](Check &check) {
        WuxFlow flow;
        change(check, flow, State::WuxExtract, [&] {
            check.require(flow.beginExtraction(), "Idle must accept extraction");
        });
        change(check, flow, State::WuxErrorBox, [&] { flow.extractionFinished(false); });
        change(check, flow, State::Idle, [&] { flow.flowFinished(); });
    });
    passed &= run("gate WUP install", [](Check &check) {
        WuxFlow flow;
        change(check, flow, State::WupInstall, [&] {
            check.require(flow.beginWupInstall(), "Idle must accept WUP install");
        });
        change(check, flow, State::Idle, [&] { flow.flowFinished(); });
    });
    passed &= run("gate error from Idle", [](Check &check) {
        WuxFlow flow;
        change(check, flow, State::WuxErrorBox, [&] { flow.enterErrorBox(); });
        change(check, flow, State::Idle, [&] { flow.flowFinished(); });
    });
    passed &= run("gate repeated state", [](Check &check) {
        WuxFlow flow;
        change(check, flow, State::Idle, [&] { flow.flowFinished(); });
        change(check, flow, State::WuxErrorBox, [&] { flow.enterErrorBox(); });
        change(check, flow, State::WuxErrorBox, [&] { flow.enterErrorBox(); });
    });
    passed &= run("refused repeated WUP install", [](Check &check) {
        WuxFlow flow;
        check.require(flow.beginWupInstall(), "first WUP install must succeed");
        const auto start = model.events.size();
        check.require(!flow.beginWupInstall(), "second WUP install must be refused");
        check.require(flow.state() == State::WupInstall, "refusal changed state");
        const auto events = since(start);
        check.equal(select(events, {"menu on", "menu off"}), {}, "refusal changed menu gate");
        check.require(contains(events, "refused"), "refusal must be logged");
    });
    passed &= run("refused extraction during WUP install", [](Check &check) {
        WuxFlow flow;
        check.require(flow.beginWupInstall(), "first WUP install must succeed");
        const auto start = model.events.size();
        check.require(!flow.beginExtraction(), "extraction must be refused during WUP install");
        check.require(flow.state() == State::WupInstall, "refusal changed state");
        const auto events = since(start);
        check.equal(select(events, {"menu on", "menu off"}), {}, "refusal changed menu gate");
        check.require(contains(events, "refused"), "refusal must be logged");
    });
    for(const auto state : {State::WupInstall, State::WuxExtract, State::WuxInstall, State::WuxErrorBox})
    {
        passed &= run(std::string("destructor fail-safe ") + WuxFlow::stateName(state), [state](Check &check) {
            std::size_t start;
            {
                WuxFlow flow;
                flow.force(state);
                start = model.events.size();
            }
            check.equal(select(since(start), {"menu on", "menu off"}), {"menu on"},
                        "non-Idle destructor must enable menu exactly once");
        });
    }
    passed &= run("destructor Idle", [](Check &check) {
        { WuxFlow flow; }
        check.equal(model.events, {}, "Idle destruction must log nothing");
    });
    passed &= run("progress box replacement", [](Check &check) {
        auto *oldBox = new MessageBox;
        auto *newBox = new MessageBox;
        {
            WuxFlow flow;
            flow.setProgressBox(oldBox);
            const auto start = model.events.size();
            flow.setProgressBox(newBox);
            check.require(contains(since(start), "replaced while still live"), "missing replacement warning");
            check.require(count(since(start), "delete box") == 0 && boxAlive(oldBox),
                          "replacement deleted the old box");
            check.require(flow.progressBox() == newBox, "replacement did not retain the new box");
        }
        check.require(!boxAlive(newBox), "shutdown did not delete replacement box");
        if(boxAlive(oldBox)) delete oldBox;
        if(boxAlive(newBox)) delete newBox;
    });
    passed &= run("shutdown worker before box", [](Check &check) {
        WuxFlow flow;
        flow.setThread(new WuxExtractThread);
        flow.setProgressBox(new MessageBox);
        const auto start = model.events.size();
        flow.shutdown();
        check.equal(since(start), {"join", "delete thread", "delete box"}, "shutdown ownership order");
        check.require(flow.thread() == nullptr && flow.progressBox() == nullptr,
                      "shutdown did not clear owned pointers");
    });
    passed &= run("null joinThread", [](Check &check) {
        WuxFlow flow;
        flow.joinThread();
        check.equal(model.events, {}, "null joinThread must log nothing");
    });
    passed &= run("null releaseThread", [](Check &check) {
        WuxFlow flow;
        flow.releaseThread();
        check.equal(model.events, {}, "null releaseThread must log nothing");
    });
    passed &= run("null shutdown", [](Check &check) {
        WuxFlow flow;
        flow.shutdown();
        check.equal(model.events, {}, "null shutdown must log nothing");
    });
    passed &= run("progress fade pairing", [](Check &check) {
        auto *box = new MessageBox;
        {
            WuxFlow flow;
            flow.setProgressBox(box);
            check.require(!flow.progressFadingOut(), "new progress box started fading");
            flow.armProgressFadeOut();
            check.require(flow.progressFadingOut(), "arming fade did not set the flag");
            flow.finishProgressFadeOut();
            check.require(flow.progressBox() == nullptr && !flow.progressFadingOut(),
                          "finishing fade must clear both pointer and flag");
            flow.shutdown();
        }
        check.require(boxAlive(box) && count(model.events, "delete box") == 0,
                      "shutdown deleted a box handed to the deferred queue");
        if(boxAlive(box)) delete box;
    });
    passed &= run("extractDone without worker", [](Check &check) {
        WuxFlow flow;
        check.require(!flow.extractDone(), "missing worker reported done");
    });
    passed &= run("extractDone running worker", [](Check &check) {
        WuxFlow flow;
        flow.setThread(new WuxExtractThread);
        model.threadTerminated = false;
        check.require(!flow.extractDone(), "running worker reported done");
    });
    passed &= run("extractDone terminated worker", [](Check &check) {
        WuxFlow flow;
        flow.setThread(new WuxExtractThread);
        model.threadTerminated = false;
        check.require(!flow.extractDone(), "running worker reported done");
        model.threadTerminated = true;
        check.require(flow.extractDone(), "terminated worker did not report done");
    });
    return passed ? 0 : 1;
}
