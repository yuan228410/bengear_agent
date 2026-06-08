#pragma once

// ACP 协议核心
#include "ben_gear/acp/core/types.hpp"
#include "ben_gear/acp/core/content_block.hpp"
#include "ben_gear/acp/core/message.hpp"

// ACP 编解码
#include "ben_gear/acp/codec/serializer.hpp"
#include "ben_gear/acp/codec/json_codec.hpp"

// ACP 流式处理
#include "ben_gear/acp/stream/handler.hpp"
#include "ben_gear/acp/stream/dispatcher.hpp"

// ACP 适配器
#include "ben_gear/acp/adapter/tool_adapter.hpp"

/*!
 * @brief Agent Communication Protocol (ACP) 模块
 * 
 * 模块结构：
 * - core: 核心数据结构（零依赖）
 * - codec: 编解码器（JSON 序列化/解析）
 * - stream: 流式事件处理
 * - adapter: 工具适配器
 * 
 * 使用示例：
 * @code
 * // 创建消息
 * auto msg = ben_gear::acp::ACPMessage::user_message("Hello");
 * 
 * // 序列化
 * ben_gear::acp::JsonSerializer serializer;
 * auto json_str = serializer.serialize(msg);
 * 
 * // 解析
 * ben_gear::acp::JsonParser parser;
 * auto parsed = parser.parse(json_str, error);
 * @endcode
 */
namespace ben_gear::acp {
    // 模块文档见上方
}
