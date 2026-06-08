#pragma once

#include "ben_gear/acp/core/message.hpp"
#include "ben_gear/base/utils/json.hpp"
#include "ben_gear/base/container/string.hpp"

namespace ben_gear::llm {

// 使用命名空间别名简化代码
namespace container = base::container;
namespace acp = ben_gear::acp;

// ==================== OpenAI 适配器 ====================

/// OpenAI 格式适配器
/// 
/// 职责：
/// - ACP 消息 ↔ OpenAI 格式转换
/// - 处理 OpenAI 特定逻辑
/// 
/// 无状态，纯静态方法
class OpenAIAdapter {
public:
    /// ACP 消息 → OpenAI 格式
    static Json to_openai_format(const acp::ACPMessage& msg);
    
    /// OpenAI 格式 → ACP 消息
    static acp::ACPMessage from_openai_format(const Json& j);
    
    /// 批量转换：ACP 消息列表 → OpenAI 消息数组
    static Json to_openai_messages(const container::Vector<acp::ACPMessage>& messages);
    
private:
    /// 角色转换：ACP → OpenAI
    static const char* role_to_openai(acp::Role role);
    
    /// 角色转换：OpenAI → ACP
    static acp::Role role_from_openai(const container::String& role);
};

// ==================== Anthropic 适配器 ====================

/// Anthropic 格式适配器
/// 
/// 职责：
/// - ACP 消息 ↔ Anthropic 格式转换
/// - 处理 Anthropic 特定逻辑（system 分离等）
/// 
/// 无状态，纯静态方法
class AnthropicAdapter {
public:
    /// ACP 消息 → Anthropic 格式
    static Json to_anthropic_format(const acp::ACPMessage& msg);
    
    /// Anthropic 格式 → ACP 消息
    static acp::ACPMessage from_anthropic_format(const Json& j);
    
    /// 批量转换：ACP 消息列表 → Anthropic 消息数组（不含 system）
    static Json to_anthropic_messages(const container::Vector<acp::ACPMessage>& messages);
    
    /// 提取系统提示（Anthropic 需要单独处理 system）
    static container::String extract_system_prompt(const container::Vector<acp::ACPMessage>& messages);
    
private:
    /// 角色转换：ACP → Anthropic
    static const char* role_to_anthropic(acp::Role role);
    
    /// 角色转换：Anthropic → ACP
    static acp::Role role_from_anthropic(const container::String& role);
};

} // namespace ben_gear::llm
