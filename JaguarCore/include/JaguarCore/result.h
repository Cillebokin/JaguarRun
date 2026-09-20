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
    static Result fromValue(T value)
    {
        return Result(std::move(value));
    }

    static Result fromError(Error error)
    {
        return Result(std::move(error));
    }

    bool hasValue() const noexcept { return std::holds_alternative<T>(m_Value); }

    const T& value() const& { return std::get<T>(m_Value); }
    T& value() & { return std::get<T>(m_Value); }
    T&& value() && { return std::get<T>(std::move(m_Value)); }

    const Error& error() const& { return std::get<Error>(m_Value); }

private:
    explicit Result(T value) : m_Value(std::move(value)) {}
    explicit Result(Error error) : m_Value(std::move(error)) {}

    std::variant<T, Error> m_Value;
};

template <>
class Result<void>
{
public:
    static Result fromValue() { return Result(true, {}); }

    static Result fromError(Error error) { return Result(false, std::move(error)); }

    bool hasValue() const noexcept { return m_Success; }
    const Error& error() const& { return m_Error; }

private:
    Result(bool success, Error error) : m_Success(success), m_Error(std::move(error)) {}

    bool m_Success = false;
    Error m_Error;
};
}
