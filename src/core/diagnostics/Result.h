#pragma once

#include <string>
#include <utility>

namespace tsq::core::diagnostics
{
class Result
{
public:
    static Result success()
    {
        return Result { true, {} };
    }

    static Result failure (std::string error)
    {
        return Result { false, std::move (error) };
    }

    bool succeeded() const noexcept
    {
        return succeeded_;
    }

    bool failed() const noexcept
    {
        return ! succeeded_;
    }

    const std::string& error() const noexcept
    {
        return error_;
    }

private:
    Result (bool succeeded, std::string error)
        : succeeded_ (succeeded),
          error_ (std::move (error))
    {
    }

    bool succeeded_ = false;
    std::string error_;
};
}
