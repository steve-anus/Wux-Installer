#include "Application.h"
#include "check.h"
#include <string>

namespace {
using namespace host;
constexpr auto FG = PROCUI_STATUS_IN_FOREGROUND;
constexpr auto BG = PROCUI_STATUS_IN_BACKGROUND;
constexpr auto RELEASE = PROCUI_STATUS_RELEASE_FOREGROUND;
constexpr auto EXITING = PROCUI_STATUS_EXITING;

void append(ProcUIStatus status, unsigned times = 1, bool idle = true)
{
    for(unsigned i = 0; i < times; ++i) model.script.push_back({status, idle});
}

Events resources(const Events &events)
{
    return select(events, {"delete MainWindow", "delete font", "delete CVideo",
                          "memoryRelease", "memoryInitialize", "new CVideo",
                          "new font", "new MainWindow", "ProcUIDrawDoneRelease"});
}

struct Interval
{
    std::string status;
    Events events;
};

std::vector<Interval> intervals()
{
    std::vector<Interval> result;
    for(const auto &event : model.events)
    {
        if(event.compare(0, 7, "status ") == 0) result.push_back({event, {}});
        else if(!result.empty()) result.back().events.push_back(event);
    }
    return result;
}

void checkLifecycle(Check &check, const std::string &name)
{
    finish();
    for(const auto &reason : model.violations) check.require(false, reason);
    check.require(model.next == model.script.size(), "script ended before the sanctioned exit status");

    Events expectedStatuses;
    for(const auto &step : model.script)
    {
        switch(step.status)
        {
            case FG: expectedStatuses.push_back("status FG"); break;
            case BG: expectedStatuses.push_back("status BG"); break;
            case RELEASE: expectedStatuses.push_back("status RELEASE"); break;
            case EXITING: expectedStatuses.push_back("status EXITING"); break;
        }
    }
    check.equal(select(model.events, {"status FG", "status BG", "status RELEASE", "status EXITING"}),
                expectedStatuses, "consumed statuses");

    const Events teardown = {"delete MainWindow", "delete font", "delete CVideo",
                             "memoryRelease", "ProcUIDrawDoneRelease"};
    const Events startup = {"memoryInitialize", "new CVideo", "new font", "new MainWindow"};
    unsigned releases = 0;
    bool startupExpected = true;
    bool deferred = false;
    unsigned frames = 0;
    for(const auto &interval : intervals())
    {
        const auto &events = interval.events;
        if(interval.status == "status RELEASE")
        {
            ++releases;
            const bool idle = name == "S1" || (name == "S6" && releases == 1);
            if(idle)
            {
                check.equal(resources(events), teardown, "idle release resource order");
                check.require(count(events, "abortFlowForExit") == 0, "idle release aborted the flow");
                startupExpected = true;
            }
            else
            {
                check.equal(resources(events), {"ProcUIDrawDoneRelease"}, "deferred release retained resources");
                check.require(count(events, "abortFlowForExit") == 1, "release must abort the flow once");
                const std::string why = name == "S4" ? "stuck delete queue" : "foreground release";
                check.require(count(events, "logFlowGate(" + why + ")") == 1,
                              "missing release gate reason: " + why);
                deferred = true;
            }
        }
        else if(interval.status == "status FG")
        {
            if(deferred)
            {
                check.equal(resources(events), {}, "deferred exit must not rebuild resources");
                check.require(!contains(events, "CVideo::") && !contains(events, "MainWindow::") &&
                              !contains(events, "GuiImage::draw") && !contains(events, "GX2"),
                              "foreground resources used after deferred exit resumed");
                check.require(count(events, "ProcUIShutdown") == 1,
                              "deferred exit must finish on the resumed foreground status");
            }
            else
            {
                check.equal(resources(events), startupExpected ? startup : Events{}, "foreground resource order");
                startupExpected = false;
                check.require(count(events, "MainWindow::updateFlow") == 1,
                              "foreground status must advance the flow once");
                check.require(count(events, "MainWindow::drawDrc") == 1 &&
                              count(events, "MainWindow::drawTv") == 1 &&
                              count(events, "CVideo::waitForVSync") == 1,
                              "foreground status must draw one complete frame");
                ++frames;
            }
        }
        else if(interval.status == "status BG")
        {
            check.equal(resources(events), {}, "background must retain resources");
        }
        else if(interval.status == "status EXITING")
        {
            check.require(count(events, "abortFlowForExit") == 1, "EXITING must abort the flow once");
            check.require(count(events, "ProcUIShutdown") == 1, "shutdown must follow EXITING");
        }
    }
    const unsigned expectedReleases = name == "S1" ? 3 : name == "S6" ? 2 : name == "S5" ? 0 : 1;
    const unsigned expectedFrames = name == "S1" ? 12 : name == "S6" ? 6 : name == "S5" ? 2 : 3;
    check.require(releases == expectedReleases, "wrong release count");
    check.require(count(model.events, "ProcUIDrawDoneRelease") == expectedReleases,
                  "each release must acknowledge DrawDoneRelease exactly once");
    check.require(frames == expectedFrames && count(model.events, "MainWindow::updateFlow") == expectedFrames,
                  "wrong foreground frame count");
    const unsigned expectedAborts = name == "S3" || name == "S6" ? 2 : 1;
    check.require(count(model.events, "abortFlowForExit") == expectedAborts, "wrong abort count");
}
}

int main(int argc, char **argv)
{
    if(argc != 2 || std::string(argv[1]).size() != 2 || argv[1][0] != 'S' ||
       argv[1][1] < '1' || argv[1][1] > '6')
    {
        std::cerr << "usage: lifecycle_test S1|S2|S3|S4|S5|S6\n";
        return 2;
    }
    const std::string name = argv[1];
    reset();
    if(name == "S1")
    {
        append(FG, 3);
        for(unsigned i = 0; i < 3; ++i)
        {
            append(RELEASE);
            append(BG, 2);
            append(FG, 3);
        }
        append(EXITING);
    }
    else if(name == "S5")
    {
        append(FG, 2);
        append(EXITING);
    }
    else
    {
        append(FG, 3);
        if(name == "S6")
        {
            append(RELEASE);
            append(BG);
            append(FG, 3);
        }
        model.deleteQueueEmpty = name != "S4";
        append(RELEASE, 1, name == "S4");
        append(BG, name == "S4" ? 1 : name == "S6" ? 3 : 2, false);
        append(name == "S2" || name == "S4" ? FG : EXITING, 1, false);
    }

    Check check;
    Application::instance()->exec();
    if(name == "S5")
    {
        check.require(model.homeDenied != nullptr, "HOME denied callback was not registered");
        if(model.homeDenied)
        {
            model.errorViewerInitialized = false;
            model.homeDenied(model.homeContext);
            check.require(model.homeNixSigns == 0, "HOME sign appeared before ErrorViewer initialization");
            model.errorViewerInitialized = true;
            model.homeDenied(model.homeContext);
            check.require(model.homeNixSigns == 1, "initialized ErrorViewer must show one HOME sign");
        }
    }
    Application::destroyInstance();
    checkLifecycle(check, name);
    cleanupFakes();
    return check.report(name) ? 0 : 1;
}
