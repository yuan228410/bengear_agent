#include "ben_gear/server/ws/protocol.hpp"

#include "ben_gear/base/log/logger.hpp"
#include "ben_gear/base/utils/json.hpp"

#include <cstdio>
#include <cstring>
#include <string_view>

namespace ben_gear::server {

namespace {
// JSON 字符串转义：将原始字符串中的特殊字符转义为 JSON 合法形式
std::string escape_json(std::string_view sv) {
    std::string out;
    out.reserve(sv.size() + 16);
    for (auto ch : sv) {
        switch (ch) {
            case '"':  out.append("\\\""); break;
            case '\\': out.append("\\\\"); break;
            case '\n': out.append("\\n"); break;
            case '\r': out.append("\\r"); break;
            case '\t': out.append("\\t"); break;
            case '\b': out.append("\\b"); break;
            case '\f': out.append("\\f"); break;
            default:
                if (static_cast<unsigned char>(ch) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(ch));
                    out.append(buf);
                } else {
                    out.push_back(ch);
                }
                break;
        }
    }
    return out;
}

bool is_json_object_or_array(std::string_view value) {
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t' || value.front() == '\n' || value.front() == '\r')) {
        value.remove_prefix(1);
    }
    if (value.empty() || (value.front() != '{' && value.front() != '[')) {
        return false;
    }
    std::string error;
    parse_json(value, error);
    if (!error.empty()) {
        log::debug_fmt("WS data treated as text after JSON validation failed: first_char={} error={}", value.front(), error);
        return false;
    }
    return true;
}
}

std::string WsMessage::to_json() const {
    std::string buf;
    buf.reserve(128 + type.size() + session_id.size() + json_data.size());
    buf.append("{\"v\":"); buf.append(std::to_string(version));
    buf.append(",\"type\":\""); buf.append(escape_json(type.data())); buf.push_back('"');
    if (!session_id.empty()) { buf.append(",\"session_id\":\""); buf.append(escape_json(session_id.data())); buf.push_back('"'); }
    for (const auto& [k, v] : strings) {
        if (k == "type" || k == "session_id") continue;
        buf.push_back(','); buf.push_back('"'); buf.append(k.data(), k.size());
        buf.append("\":\""); buf.append(escape_json(v)); buf.push_back('"');
    }
    for (const auto& [k, v] : ints) { buf.push_back(','); buf.push_back('"'); buf.append(k.data(), k.size()); buf.append("\":"); buf.append(std::to_string(v)); }
    for (const auto& [k, v] : doubles) { buf.push_back(','); buf.push_back('"'); buf.append(k.data(), k.size()); buf.append("\":"); char tmp[32]; std::snprintf(tmp,sizeof(tmp),"%.3f",v); buf.append(tmp); }
    if (!json_data.empty()) {
        if (is_json_object_or_array(json_data)) {
            buf.append(",\"data\":"); buf.append(json_data);
        } else {
            // 原始文本，作为 JSON 字符串值
            buf.append(",\"data\":\""); buf.append(escape_json(json_data)); buf.push_back('"');
        }
    }
    buf.push_back('}');
    return buf;
}

namespace {
container::String extract_cs(std::string_view j, std::string_view key) {
    auto pos = j.find(key); if (pos == std::string_view::npos) return {};
    pos += key.size();
    while (pos < j.size() && (j[pos]=='"'||j[pos]==':'||j[pos]==' ')) ++pos;
    auto s = pos;
    while (pos < j.size() && j[pos]!='"') { if (j[pos]=='\\'&&pos+1<j.size()) ++pos; ++pos; }
    return container::String(j.substr(s, pos - s));
}
int extract_int_sv(std::string_view j, std::string_view key, int fb=0) {
    auto pos = j.find(key); if (pos == std::string_view::npos) return fb;
    pos += key.size();
    while (pos < j.size() && (j[pos]=='"'||j[pos]==':'||j[pos]==' ')) ++pos;
    char* end=nullptr; auto v=static_cast<int>(strtol(j.data()+pos,&end,10));
    return (end==j.data()+pos)?fb:v;
}
std::string extract_json_obj(std::string_view j, std::string_view key) {
    auto pos = j.find(key); if (pos == std::string_view::npos) return {};
    pos += key.size();
    while (pos < j.size() && j[pos]!='{' && j[pos]!='[') ++pos;
    if (pos >= j.size()) return {};
    char open=j[pos], close=(open=='{')?'}':']'; int depth=0; auto e=pos;
    while (e < j.size()) { if (j[e]==open) ++depth; else if (j[e]==close){--depth;if(!depth){++e;break;}} ++e; }
    return std::string(j.substr(pos, e-pos));
}
}

WsMessage WsMessage::from_json(const std::string& json_str) {
    WsMessage msg; std::string_view sv(json_str);
    msg.version = extract_int_sv(sv,"\"v\"",1);
    msg.type = extract_cs(sv,"\"type\"");
    msg.session_id = extract_cs(sv,"\"session_id\"");
    auto p=extract_cs(sv,"\"prompt\""); if(!p.empty()) msg.strings["prompt"]=std::move(p);
    auto w=extract_cs(sv,"\"workspace\""); if(!w.empty()) msg.strings["workspace"]=std::move(w);
    auto n=extract_cs(sv,"\"name\""); if(!n.empty()) msg.strings["name"]=std::move(n);
    auto d=extract_json_obj(sv,"\"data\""); if(!d.empty()) msg.json_data=std::move(d);
    return msg;
}

// 客户端 -> 服务端
WsMessage WsMessage::chat(const container::String& s,const container::String& p){WsMessage m;m.type="chat";m.session_id=s;m.strings["prompt"]=p;return m;}
WsMessage WsMessage::abort(const container::String& s){WsMessage m;m.type="abort";m.session_id=s;return m;}
WsMessage WsMessage::switch_session(const container::String& s,const container::String& w){WsMessage m;m.type="switch";m.session_id=s;m.strings["workspace"]=w;return m;}
WsMessage WsMessage::rename(const container::String& s,const container::String& n){WsMessage m;m.type="rename";m.session_id=s;m.strings["name"]=n;return m;}
WsMessage WsMessage::del(const container::String& s){WsMessage m;m.type="delete";m.session_id=s;return m;}
WsMessage WsMessage::ping(){WsMessage m;m.type="ping";return m;}

// 服务端 -> 客户端
WsMessage WsMessage::token(const container::String& s,const container::String& c){WsMessage m;m.type="token";m.session_id=s;m.strings["content"]=c;return m;}
WsMessage WsMessage::thinking(const container::String& s,int ch,double el,const container::String& c){WsMessage m;m.type="thinking";m.session_id=s;m.ints["chars"]=ch;m.doubles["elapsed"]=el;if(!c.empty())m.strings["content"]=c;return m;}
WsMessage WsMessage::tool_call(const container::String& s,const container::String& n,const std::string& a){WsMessage m;m.type="tool_call";m.session_id=s;m.strings["name"]=n;m.json_data=a.empty()?"{}":a;return m;}
WsMessage WsMessage::tool_result(const container::String& s,const container::String& n,const std::string& r,double el){WsMessage m;m.type="tool_result";m.session_id=s;m.strings["name"]=n;m.doubles["elapsed"]=el;m.json_data=r.empty()?"{}":r;return m;}
WsMessage WsMessage::sub_agent(const container::String& s,const container::String& et,const std::string& d){WsMessage m;m.type="sub_agent";m.session_id=s;m.strings["event_type"]=et;m.json_data=d.empty()?"{}":d;return m;}
namespace {
std::string merge_done_data(const std::string& usage_json, const std::string& outcome_json) {
    std::string data = usage_json.empty() ? "{}" : usage_json;
    if (outcome_json.empty()) return data;
    if (data == "{}") {
        return std::string("{\"outcome\":") + outcome_json + "}";
    }
    if (!data.empty() && data.front() == '{' && data.back() == '}') {
        data.pop_back();
        data += ",\"outcome\":";
        data += outcome_json;
        data += '}';
        return data;
    }
    return std::string("{\"usage\":") + data + ",\"outcome\":" + outcome_json + "}";
}
}

WsMessage WsMessage::done(const container::String& s,const std::string& u,double ts,double tf){WsMessage m;m.type="done";m.session_id=s;m.doubles["total_seconds"]=ts;m.doubles["ttfb_seconds"]=tf;m.json_data=u.empty()?"{}":u;return m;}
WsMessage WsMessage::done_with_outcome(const container::String& s,const std::string& u,const std::string& o,double ts,double tf){WsMessage m=WsMessage::done(s,u,ts,tf);m.json_data=merge_done_data(u,o);return m;}
WsMessage WsMessage::error_msg(const container::String& s,const container::String& msg){WsMessage m;m.type="error";m.session_id=s;m.strings["message"]=msg;return m;}
WsMessage WsMessage::error_msg(const container::String& s,const container::String& msg,const std::string& o){WsMessage m=WsMessage::error_msg(s,msg);if(!o.empty())m.json_data=std::string("{\"outcome\":")+o+"}";return m;}
WsMessage WsMessage::connected(const container::String& s,const std::string& cfg){WsMessage m;m.type="connected";m.session_id=s;m.json_data=cfg.empty()?"{}":cfg;return m;}
WsMessage WsMessage::sessions(const std::string& j){WsMessage m;m.type="sessions";m.json_data=j;return m;}
WsMessage WsMessage::pong(){WsMessage m;m.type="pong";return m;}

} // namespace ben_gear::server
