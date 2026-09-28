#pragma once

#include <string>
#include <utility>
#include <variant>

namespace jaguar
{
// 错误类型
enum class ErrorCode
{
    None,                   // 无错误
    InvalidInput,           // 输入无效
    InvalidPolygon,         // 多边形无效
    SelfIntersection,       // 多边形自相交
    ZeroArea,               // 面积为零
    UnsupportedShape,       // 不支持的形状
    OffsetFailed,           // 偏移计算失败
    CollisionEngineFailure, // 碰撞引擎失败
    SnapshotMismatch,       // 状态快照不匹配
    Infeasible,             // 无法满足操作要求
    Cancelled,              // 操作已取消
    TimeLimit,              // 超出时间限制
    SerializationFailure,   // 序列化或反序列化失败
    UnsupportedOperation,   // 操作尚未实现或不受支持
};

// 错误信息
struct Error
{
    ErrorCode code = ErrorCode::None; // 错误类型
    std::string message;              // 错误描述
};

// 表示带返回值操作的结果
template <typename T>
class Result
{
public:
    // 创建成功结果
    inline static Result FromValue(T value) { return Result(std::move(value)); }

    // 创建失败结果
    inline static Result FromError(Error error) { return Result(std::move(error)); }

    // 判断是否包含返回值
    inline bool HasValue() const noexcept { return std::holds_alternative<T>(m_Value); }

    // 获取常量返回值
    inline const T& Value() const& { return std::get<T>(m_Value); }
    // 获取可修改的返回值
    inline T& Value() & { return std::get<T>(m_Value); }
    // 移出返回值
    inline T&& Value() && { return std::get<T>(std::move(m_Value)); }

    // 获取错误信息
    inline const Error& GetError() const& { return std::get<Error>(m_Value); }

private:
    inline explicit Result(T value) : m_Value(std::move(value)) {}
    inline explicit Result(Error error) : m_Value(std::move(error)) {}

    std::variant<T, Error> m_Value;
};

// 表示无返回值操作的结果
template <>
class Result<void>
{
public:
    // 创建成功结果
    inline static Result FromValue() { return Result(true, {}); }

    // 创建失败结果
    inline static Result FromError(Error error) { return Result(false, std::move(error)); }

    // 判断操作是否成功
    inline bool HasValue() const noexcept { return m_Success; }
    // 获取错误信息
    inline const Error& GetError() const& { return m_Error; }

private:
    inline Result(bool success, Error error)
        : m_Success(success), m_Error(std::move(error))
    {}

    bool m_Success = false;
    Error m_Error;
};
}
