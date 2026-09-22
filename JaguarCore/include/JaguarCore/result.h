#pragma once

#include <string>
#include <utility>
#include <variant>

namespace jaguar
{
enum class ErrorCode
{
    None,
    InvalidInput,
    InvalidPolygon,
    SelfIntersection,
    ZeroArea,
    UnsupportedShape,
    OffsetFailed,
    CollisionEngineFailure,
    SnapshotMismatch,
    Infeasible,
    Cancelled,
    TimeLimit,
    SerializationFailure,
};

struct Error
{
    ErrorCode code = ErrorCode::None;
    std::string message;
};

template <typename T>
class Result
{
public:
    inline static Result FromValue(T value)
    {
        return Result(std::move(value));
    }

    inline static Result FromError(Error error)
    {
        return Result(std::move(error));
    }

    inline bool HasValue() const noexcept
    {
        return std::holds_alternative<T>(m_Value);
    }

    inline const T& Value() const& { return std::get<T>(m_Value); }
    inline T& Value() & { return std::get<T>(m_Value); }
    inline T&& Value() && { return std::get<T>(std::move(m_Value)); }

    inline const Error& GetError() const& { return std::get<Error>(m_Value); }

private:
    inline explicit Result(T value) : m_Value(std::move(value)) {}
    inline explicit Result(Error error) : m_Value(std::move(error)) {}

    std::variant<T, Error> m_Value;
};

template <>
class Result<void>
{
public:
    inline static Result FromValue() { return Result(true, {}); }

    inline static Result FromError(Error error)
    {
        return Result(false, std::move(error));
    }

    inline bool HasValue() const noexcept { return m_Success; }
    inline const Error& GetError() const& { return m_Error; }

private:
    inline Result(bool success, Error error)
        : m_Success(success), m_Error(std::move(error))
    {
    }

    bool m_Success = false;
    Error m_Error;
};
}
