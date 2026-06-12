#pragma once

#include <string>

namespace tsq::tests
{
struct MacNativeEditorPoint
{
    double x = 0.0;
    double y = 0.0;
};

std::string describeMacNativeViewTree (void* nsView);
std::string describeMacNativeHitView (void* nsView, MacNativeEditorPoint point);
bool clickMacNativeEditorView (void* nsView, MacNativeEditorPoint point);
bool dragMacNativeEditorView (void* nsView, MacNativeEditorPoint start, MacNativeEditorPoint end);
}
