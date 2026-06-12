#include "MacNativeEditorDriver.h"

#import <AppKit/AppKit.h>
#import <objc/runtime.h>

#include <atomic>
#include <sstream>

namespace tsq::tests
{
namespace
{
std::atomic<int> eventNumber { 1 };

void appendViewTree (std::ostringstream& stream, NSView* view, int depth)
{
    for (auto index = 0; index < depth; ++index)
        stream << "  ";

    const auto frame = [view frame];
    const auto bounds = [view bounds];
    stream << class_getName ([view class])
           << " frame=" << frame.origin.x << "," << frame.origin.y << " "
           << frame.size.width << "x" << frame.size.height
           << " bounds=" << bounds.origin.x << "," << bounds.origin.y << " "
           << bounds.size.width << "x" << bounds.size.height
           << " flipped=" << ([view isFlipped] ? "true" : "false")
           << " hidden=" << ([view isHidden] ? "true" : "false")
           << " subviews=" << [[view subviews] count]
           << '\n';

    for (NSView* subview in [view subviews])
        appendViewTree (stream, subview, depth + 1);
}

NSPoint convertTopLeftLocalPointToWindow (NSView* view, MacNativeEditorPoint point)
{
    auto localPoint = NSMakePoint (point.x, point.y);
    if (! [view isFlipped])
        localPoint.y = NSHeight ([view bounds]) - localPoint.y;

    return [view convertPoint:localPoint toView:nil];
}

NSView* hitViewForTopLeftLocalPoint (NSView* view, MacNativeEditorPoint point)
{
    auto localPoint = NSMakePoint (point.x, point.y);
    if (! [view isFlipped])
        localPoint.y = NSHeight ([view bounds]) - localPoint.y;

    auto* hitView = [view hitTest:localPoint];
    return hitView == nil ? view : hitView;
}

NSEvent* createMouseEvent (NSView* view, NSEventType type, NSPoint windowPoint, NSInteger clickCount, CGFloat pressure)
{
    auto* window = [view window];
    if (window == nil)
        return nil;

    return [NSEvent mouseEventWithType:type
                              location:windowPoint
                         modifierFlags:0
                             timestamp:[[NSProcessInfo processInfo] systemUptime]
                          windowNumber:[window windowNumber]
                               context:nil
                           eventNumber:eventNumber.fetch_add (1)
                            clickCount:clickCount
                              pressure:pressure];
}

bool dispatchMouseEventDirectly (NSView* targetView, NSEvent* event)
{
    if (targetView == nil || event == nil)
        return false;

    switch ([event type])
    {
        case NSEventTypeLeftMouseDown:
            [targetView mouseDown:event];
            return true;

        case NSEventTypeLeftMouseDragged:
            [targetView mouseDragged:event];
            return true;

        case NSEventTypeLeftMouseUp:
            [targetView mouseUp:event];
            return true;

        case NSEventTypeMouseMoved:
            [targetView mouseMoved:event];
            return true;

        default:
            return false;
    }
}

void spinAppKitRunLoop()
{
    @autoreleasepool
    {
        [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode
                                 beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.01]];
    }
}

bool dispatchMouseEventThroughApplicationQueue (NSEvent* event)
{
    if (event == nil)
        return false;

    [[NSApplication sharedApplication] postEvent:event atStart:NO];
    spinAppKitRunLoop();
    return true;
}

bool dispatchMouseEventThroughWindow (NSView* view, NSEvent* event)
{
    auto* window = [view window];
    if (window == nil || event == nil)
        return false;

    [window sendEvent:event];
    return true;
}

void prepareWindow (NSView* view)
{
    auto* app = [NSApplication sharedApplication];
    [app setActivationPolicy:NSApplicationActivationPolicyRegular];
    [app activateIgnoringOtherApps:YES];

    if (auto* window = [view window])
    {
        [window makeKeyAndOrderFront:nil];
        [window orderFrontRegardless];
    }
}

bool dispatchMouseSequence (void* nsView,
                            MacNativeEditorPoint start,
                            MacNativeEditorPoint end,
                            bool drag)
{
    auto* rootView = static_cast<NSView*> (nsView);
    if (rootView == nil)
        return false;

    prepareWindow (rootView);

    auto* targetView = hitViewForTopLeftLocalPoint (rootView, start);
    const auto startWindowPoint = convertTopLeftLocalPointToWindow (rootView, start);
    const auto endWindowPoint = convertTopLeftLocalPointToWindow (rootView, end);

    auto* moveEvent = createMouseEvent (targetView, NSEventTypeMouseMoved, startWindowPoint, 0, 0.0);
    dispatchMouseEventThroughApplicationQueue (moveEvent);
    dispatchMouseEventThroughWindow (targetView, moveEvent);
    dispatchMouseEventDirectly (targetView, moveEvent);
    spinAppKitRunLoop();

    auto* downEvent = createMouseEvent (targetView, NSEventTypeLeftMouseDown, startWindowPoint, 1, 1.0);
    if (downEvent == nil)
        return false;

    // Direct delivery is what embedded VST views tend to need in a test host.
    // Window delivery is also attempted so the host window's tracking state is exercised.
    dispatchMouseEventThroughApplicationQueue (downEvent);
    dispatchMouseEventThroughWindow (targetView, downEvent);
    dispatchMouseEventDirectly (targetView, downEvent);
    spinAppKitRunLoop();

    if (drag)
    {
        constexpr auto steps = 16;
        for (auto step = 1; step <= steps; ++step)
        {
            const auto alpha = static_cast<double> (step) / static_cast<double> (steps);
            const auto windowPoint = NSMakePoint (startWindowPoint.x + ((endWindowPoint.x - startWindowPoint.x) * alpha),
                                                  startWindowPoint.y + ((endWindowPoint.y - startWindowPoint.y) * alpha));
            auto* dragEvent = createMouseEvent (targetView, NSEventTypeLeftMouseDragged, windowPoint, 1, 1.0);
            if (dragEvent == nil)
                return false;

            dispatchMouseEventThroughApplicationQueue (dragEvent);
            dispatchMouseEventThroughWindow (targetView, dragEvent);
            dispatchMouseEventDirectly (targetView, dragEvent);
            spinAppKitRunLoop();
        }
    }

    auto* upEvent = createMouseEvent (targetView, NSEventTypeLeftMouseUp, endWindowPoint, 1, 0.0);
    if (upEvent == nil)
        return false;

    dispatchMouseEventThroughApplicationQueue (upEvent);
    dispatchMouseEventThroughWindow (targetView, upEvent);
    dispatchMouseEventDirectly (targetView, upEvent);
    spinAppKitRunLoop();
    return true;
}
}

std::string describeMacNativeViewTree (void* nsView)
{
    auto* view = static_cast<NSView*> (nsView);
    if (view == nil)
        return "native NSView is null\n";

    std::ostringstream stream;
    appendViewTree (stream, view, 0);
    return stream.str();
}

std::string describeMacNativeHitView (void* nsView, MacNativeEditorPoint point)
{
    auto* view = static_cast<NSView*> (nsView);
    if (view == nil)
        return "native NSView is null";

    auto* hitView = hitViewForTopLeftLocalPoint (view, point);
    const auto windowPoint = convertTopLeftLocalPointToWindow (view, point);

    std::ostringstream stream;
    stream << "hit-test point=" << point.x << "," << point.y
           << " window=" << windowPoint.x << "," << windowPoint.y
           << " root=" << class_getName ([view class])
           << " hit=" << (hitView == nil ? "nil" : class_getName ([hitView class]));
    return stream.str();
}

bool clickMacNativeEditorView (void* nsView, MacNativeEditorPoint point)
{
    return dispatchMouseSequence (nsView, point, point, false);
}

bool dragMacNativeEditorView (void* nsView, MacNativeEditorPoint start, MacNativeEditorPoint end)
{
    return dispatchMouseSequence (nsView, start, end, true);
}
}
