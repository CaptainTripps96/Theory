#import <ApplicationServices/ApplicationServices.h>

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>

namespace
{
bool parseDouble (std::string_view text, double& value)
{
    std::string input { text };
    char* end = nullptr;
    value = std::strtod (input.c_str(), &end);
    return end != input.c_str() && end != nullptr && *end == '\0';
}

CGPoint makePoint (double x, double y)
{
    return CGPointMake (static_cast<CGFloat> (x), static_cast<CGFloat> (y));
}

void sleepForInput()
{
    std::this_thread::sleep_for (std::chrono::milliseconds (35));
}

void postMouseEvent (CGEventType type, CGPoint point, int clickState = 1)
{
    auto event = CGEventCreateMouseEvent (nullptr, type, point, kCGMouseButtonLeft);
    if (event == nullptr)
        return;

    CGEventSetIntegerValueField (event, kCGMouseEventClickState, clickState);
    CGEventPost (kCGHIDEventTap, event);
    CGEventPost (kCGSessionEventTap, event);
    CGEventPost (kCGAnnotatedSessionEventTap, event);
    CFRelease (event);
    sleepForInput();
}

void click (CGPoint point)
{
    postMouseEvent (kCGEventMouseMoved, point);
    postMouseEvent (kCGEventLeftMouseDown, point);
    postMouseEvent (kCGEventLeftMouseUp, point);
}

void doubleClick (CGPoint point)
{
    postMouseEvent (kCGEventMouseMoved, point);
    postMouseEvent (kCGEventLeftMouseDown, point, 1);
    postMouseEvent (kCGEventLeftMouseUp, point, 1);
    std::this_thread::sleep_for (std::chrono::milliseconds (80));
    postMouseEvent (kCGEventLeftMouseDown, point, 2);
    postMouseEvent (kCGEventLeftMouseUp, point, 2);
}

void drag (CGPoint start, CGPoint end)
{
    postMouseEvent (kCGEventMouseMoved, start);
    postMouseEvent (kCGEventLeftMouseDown, start);

    constexpr auto steps = 24;
    for (auto step = 1; step <= steps; ++step)
    {
        const auto alpha = static_cast<double> (step) / static_cast<double> (steps);
        postMouseEvent (kCGEventLeftMouseDragged,
                        makePoint (start.x + ((end.x - start.x) * alpha),
                                   start.y + ((end.y - start.y) * alpha)));
    }

    postMouseEvent (kCGEventLeftMouseUp, end);
}
}

int main (int argc, char** argv)
{
    if (! CGPreflightPostEventAccess())
    {
        std::cerr << "Quartz event posting is not permitted\n";
        return 2;
    }

    if (argc == 4 && std::string_view { argv[1] } == "click")
    {
        double x = 0.0;
        double y = 0.0;
        if (! parseDouble (argv[2], x) || ! parseDouble (argv[3], y))
            return 3;

        click (makePoint (x, y));
        return 0;
    }

    if (argc == 4 && std::string_view { argv[1] } == "doubleclick")
    {
        double x = 0.0;
        double y = 0.0;
        if (! parseDouble (argv[2], x) || ! parseDouble (argv[3], y))
            return 3;

        doubleClick (makePoint (x, y));
        return 0;
    }

    if (argc == 6 && std::string_view { argv[1] } == "drag")
    {
        double startX = 0.0;
        double startY = 0.0;
        double endX = 0.0;
        double endY = 0.0;
        if (! parseDouble (argv[2], startX)
            || ! parseDouble (argv[3], startY)
            || ! parseDouble (argv[4], endX)
            || ! parseDouble (argv[5], endY))
        {
            return 3;
        }

        drag (makePoint (startX, startY), makePoint (endX, endY));
        return 0;
    }

    std::cerr << "usage: tsq_mac_mouse_event_tool click x y | doubleclick x y | drag x1 y1 x2 y2\n";
    return 1;
}
