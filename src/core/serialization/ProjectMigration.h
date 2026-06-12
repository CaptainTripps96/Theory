#pragma once

#include "core/serialization/JsonHelpers.h"

namespace tsq::core::serialization
{
class ProjectMigration
{
public:
    static JsonValue migrateToCurrent (JsonValue projectJson);
};
}
