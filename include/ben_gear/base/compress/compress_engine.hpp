#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace ben_gear::net {

/// 压缩引擎抽象接口
///
/// 提供后端无关的压缩/解压操作。
/// 默认使用 miniz（单文件 vendor），可选 zlib。
class CompressEngine {
public:
    virtual ~CompressEngine() = default;

    /// 解压 deflate 数据
    /// @param src           压缩数据
    /// @param src_len       压缩数据长度
    /// @param dst           输出缓冲区
    /// @param expected_size 预期解压大小（0=自动）
    /// @return 成功/失败
    virtual bool inflate(const uint8_t* src, uint32_t src_len,
                         std::vector<uint8_t>& dst,
                         uint32_t expected_size) = 0;

    /// 压缩数据（raw deflate）
    /// @param src     原始数据
    /// @param src_len 原始数据长度
    /// @param dst     输出缓冲区
    /// @return 成功/失败
    virtual bool deflate(const uint8_t* src, uint32_t src_len,
                         std::vector<uint8_t>& dst) = 0;

    /// 后端名称
    virtual const char* name() const noexcept = 0;
};

/// 获取全局 CompressEngine 实例
CompressEngine& global_compress_engine();

/// 设置全局 CompressEngine 实例
void set_global_compress_engine(std::unique_ptr<CompressEngine> engine);

/// 创建默认后端（miniz）
std::unique_ptr<CompressEngine> create_default_compress_engine();

}  // namespace ben_gear::net
