import sys

with open(sys.argv[1], 'r', encoding='utf-8') as f:
    content = f.read()

# Replace extract_text return type and body
content = content.replace(
    'std::string extract_text(Json& response, config::Provider provider) {',
    'container::String extract_text(Json& response, config::Provider provider) {'
)
content = content.replace(
    '                return Json(msg["content"]).get<std::string>();',
    '                return container::String(Json(msg["content"]).get<std::string>().c_str());'
)
content = content.replace(
    '            std::string text;',
    '            container::String text;'
)
content = content.replace(
    '                    text += Json(block["text"]).get<std::string>();',
    '''                    auto part = Json(block["text"]).get<std::string>();
                    text.append(part.data(), part.size());'''
)
content = content.replace(
    '    return "";\n}\n\n/// 从 JSON 响应中提取思考内容\nstd::string extract_thinking',
    '    return {};\n}\n\n/// 从 JSON 响应中提取思考内容\ncontainer::String extract_thinking'
)

# Replace extract_thinking return type
content = content.replace(
    'std::string extract_thinking(Json& response, config::Provider provider) {',
    'container::String extract_thinking(Json& response, config::Provider provider) {'
)
content = content.replace(
    '                return Json(msg["reasoning_content"]).get<std::string>();',
    '                return container::String(Json(msg["reasoning_content"]).get<std::string>().c_str());'
)
content = content.replace(
    '                    return Json(block["thinking"]).get<std::string>();',
    '                    return container::String(Json(block["thinking"]).get<std::string>().c_str());'
)
content = content.replace(
    '    return "";',
    '    return {};'
)

with open(sys.argv[1], 'w', encoding='utf-8') as f:
    f.write(content)

print('Done')
