#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <shellapi.h>

#include "persistence.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <optional>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

constexpr std::size_t kMaxHeaderSize = 64 * 1024;
constexpr std::size_t kMaxJsonBody = 64 * 1024;
constexpr std::size_t kMaxMusicUpload = 25 * 1024 * 1024;
constexpr std::size_t kMaxMessages = 500;
constexpr std::size_t kMaxLookups = 300;
constexpr auto kSessionTtl = std::chrono::seconds(120);

std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::string asciiLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool endsWith(const std::string& value, const std::string& suffix) {
    return value.size() >= suffix.size()
        && value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string jsonEscape(const std::string& value) {
    std::ostringstream output;
    for (unsigned char ch : value) {
        switch (ch) {
            case '"': output << "\\\""; break;
            case '\\': output << "\\\\"; break;
            case '\b': output << "\\b"; break;
            case '\f': output << "\\f"; break;
            case '\n': output << "\\n"; break;
            case '\r': output << "\\r"; break;
            case '\t': output << "\\t"; break;
            default:
                if (ch < 0x20) {
                    output << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                           << static_cast<int>(ch) << std::dec;
                } else {
                    output << static_cast<char>(ch);
                }
        }
    }
    return output.str();
}

void appendUtf8(std::string& output, std::uint32_t codepoint) {
    if (codepoint <= 0x7F) {
        output.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7FF) {
        output.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
        output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else if (codepoint <= 0xFFFF) {
        output.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
        output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else {
        output.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
        output.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
        output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }
}

int hexValue(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

std::optional<std::string> jsonStringField(const std::string& body, const std::string& key) {
    const std::string marker = "\"" + key + "\"";
    auto position = body.find(marker);
    if (position == std::string::npos) return std::nullopt;
    position = body.find(':', position + marker.size());
    if (position == std::string::npos) return std::nullopt;
    position = body.find('"', position + 1);
    if (position == std::string::npos) return std::nullopt;

    std::string value;
    for (++position; position < body.size(); ++position) {
        char ch = body[position];
        if (ch == '"') return value;
        if (ch != '\\') {
            value.push_back(ch);
            continue;
        }
        if (++position >= body.size()) return std::nullopt;
        ch = body[position];
        switch (ch) {
            case '"': value.push_back('"'); break;
            case '\\': value.push_back('\\'); break;
            case '/': value.push_back('/'); break;
            case 'b': value.push_back('\b'); break;
            case 'f': value.push_back('\f'); break;
            case 'n': value.push_back('\n'); break;
            case 'r': value.push_back('\r'); break;
            case 't': value.push_back('\t'); break;
            case 'u': {
                if (position + 4 >= body.size()) return std::nullopt;
                std::uint32_t codepoint = 0;
                for (int index = 0; index < 4; ++index) {
                    const int valueHex = hexValue(body[++position]);
                    if (valueHex < 0) return std::nullopt;
                    codepoint = (codepoint << 4) | static_cast<std::uint32_t>(valueHex);
                }
                if (codepoint >= 0xD800 && codepoint <= 0xDBFF && position + 6 < body.size()
                    && body[position + 1] == '\\' && body[position + 2] == 'u') {
                    position += 2;
                    std::uint32_t low = 0;
                    for (int index = 0; index < 4; ++index) {
                        const int valueHex = hexValue(body[++position]);
                        if (valueHex < 0) return std::nullopt;
                        low = (low << 4) | static_cast<std::uint32_t>(valueHex);
                    }
                    if (low >= 0xDC00 && low <= 0xDFFF) {
                        codepoint = 0x10000 + ((codepoint - 0xD800) << 10) + (low - 0xDC00);
                    }
                }
                appendUtf8(value, codepoint);
                break;
            }
            default: return std::nullopt;
        }
    }
    return std::nullopt;
}

std::optional<long long> jsonIntegerField(const std::string& body, const std::string& key) {
    const std::string marker = "\"" + key + "\"";
    auto position = body.find(marker);
    if (position == std::string::npos) return std::nullopt;
    position = body.find(':', position + marker.size());
    if (position == std::string::npos) return std::nullopt;
    ++position;
    while (position < body.size() && std::isspace(static_cast<unsigned char>(body[position]))) ++position;
    auto end = position;
    if (end < body.size() && body[end] == '-') ++end;
    while (end < body.size() && std::isdigit(static_cast<unsigned char>(body[end]))) ++end;
    if (end == position || (end == position + 1 && body[position] == '-')) return std::nullopt;
    try { return std::stoll(body.substr(position, end - position)); }
    catch (...) { return std::nullopt; }
}

std::string urlDecode(const std::string& value) {
    std::string decoded;
    for (std::size_t index = 0; index < value.size(); ++index) {
        if (value[index] == '%' && index + 2 < value.size()) {
            const int high = hexValue(value[index + 1]);
            const int low = hexValue(value[index + 2]);
            if (high >= 0 && low >= 0) {
                decoded.push_back(static_cast<char>((high << 4) | low));
                index += 2;
                continue;
            }
        }
        decoded.push_back(value[index] == '+' ? ' ' : value[index]);
    }
    return decoded;
}

std::unordered_map<std::string, std::string> parseQuery(const std::string& target) {
    std::unordered_map<std::string, std::string> result;
    const auto question = target.find('?');
    if (question == std::string::npos) return result;
    std::stringstream stream(target.substr(question + 1));
    std::string pair;
    while (std::getline(stream, pair, '&')) {
        const auto equals = pair.find('=');
        if (equals != std::string::npos) {
            result[urlDecode(pair.substr(0, equals))] = urlDecode(pair.substr(equals + 1));
        }
    }
    return result;
}

std::string currentTime() {
    const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm local{};
    localtime_s(&local, &now);
    char buffer[6]{};
    std::strftime(buffer, sizeof(buffer), "%H:%M", &local);
    return buffer;
}

class Dictionary {
public:
    explicit Dictionary(const std::string& path) {
        std::ifstream input(path, std::ios::binary);
        if (!input) throw std::runtime_error("Cannot open dictionary file: " + path);
        std::string line;
        while (std::getline(input, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            const auto split = line.find_first_of(" \t");
            if (split == std::string::npos) continue;
            auto word = asciiLower(trim(line.substr(0, split)));
            auto meaning = trim(line.substr(split + 1));
            if (!word.empty() && !meaning.empty()) entries_[word] = meaning;
        }
        if (entries_.empty()) throw std::runtime_error("Dictionary file has no valid entries.");
    }

    std::size_t size() const { return entries_.size(); }

    std::optional<std::string> extractQuery(const std::string& message) const {
        const std::vector<std::string> prefixes = {
            u8"@词典", u8"词典机器人", "/dict", "/d", u8"查词", u8"查询"
        };
        const auto clean = trim(message);
        for (const auto& prefix : prefixes) {
            if (clean.rfind(prefix, 0) == 0) {
                auto query = trim(clean.substr(prefix.size()));
                if (query.rfind(":", 0) == 0) query = trim(query.substr(1));
                if (query.rfind(u8"：", 0) == 0) query = trim(query.substr(std::string(u8"：").size()));
                return query;
            }
        }
        return std::nullopt;
    }

    std::string reply(const std::string& rawQuery) const {
        const auto query = asciiLower(trim(rawQuery));
        if (query.empty()) return u8"请告诉我要查的单词，例如：@词典 hello";
        const auto found = entries_.find(query);
        if (found != entries_.end()) return query + "\n" + found->second;

        std::vector<std::string> reverse;
        for (const auto& entry : entries_) {
            if (entry.second.find(rawQuery) != std::string::npos) reverse.push_back(entry.first);
            if (reverse.size() == 6) break;
        }
        if (!reverse.empty()) {
            std::ostringstream output;
            output << u8"和“" << rawQuery << u8"”相关的词：";
            for (std::size_t index = 0; index < reverse.size(); ++index) {
                if (index) output << ", ";
                output << reverse[index];
            }
            return output.str();
        }
        return u8"暂时没有找到“" + rawQuery + u8"”。请检查拼写后再试。";
    }

private:
    std::unordered_map<std::string, std::string> entries_;
};

class MusicLibrary {
public:
    MusicLibrary(std::string root, std::string adminPassword)
        : root_(std::move(root)), adminPassword_(std::move(adminPassword)) {
        std::filesystem::create_directories(std::filesystem::u8path(root_));
    }

    std::pair<int, std::string> list() const {
        std::lock_guard<std::mutex> guard(mutex_);
        std::vector<std::pair<std::string, std::uintmax_t>> tracks;
        for (const auto& entry : std::filesystem::directory_iterator(std::filesystem::u8path(root_))) {
            if (!entry.is_regular_file()) continue;
            const auto name = entry.path().filename().u8string();
            if (!isAllowedName(name)) continue;
            tracks.emplace_back(name, entry.file_size());
        }
        std::sort(tracks.begin(), tracks.end(), [](const auto& left, const auto& right) {
            return asciiLower(left.first) < asciiLower(right.first);
        });
        std::ostringstream output;
        output << "{\"tracks\":[";
        for (std::size_t index = 0; index < tracks.size(); ++index) {
            if (index) output << ',';
            output << "{\"name\":\"" << jsonEscape(tracks[index].first) << "\",\"size\":"
                   << tracks[index].second << '}';
        }
        output << "]}";
        return {200, output.str()};
    }

    std::pair<int, std::string> upload(const std::string& password, const std::string& rawName,
                                       const std::string& bytes) {
        if (!authorized(password)) return {403, errorJson(u8"管理员口令错误")};
        const auto name = trim(rawName);
        if (!isAllowedName(name)) return {400, errorJson(u8"仅支持 mp3、wav、ogg、m4a 和 aac 音频")};
        if (bytes.empty()) return {400, errorJson(u8"音乐文件不能为空")};
        if (bytes.size() > kMaxMusicUpload) return {413, errorJson(u8"音乐文件不能超过 25 MB")};
        std::lock_guard<std::mutex> guard(mutex_);
        const auto target = std::filesystem::u8path(root_) / std::filesystem::u8path(name);
        auto temporary = target;
        temporary += ".uploading";
        {
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            if (!output) return {500, errorJson(u8"无法写入音乐目录")};
            output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
            if (!output) return {500, errorJson(u8"音乐保存失败")};
        }
        std::error_code error;
        std::filesystem::remove(target, error);
        error.clear();
        std::filesystem::rename(temporary, target, error);
        if (error) {
            std::filesystem::remove(temporary);
            return {500, errorJson(u8"音乐保存失败")};
        }
        return {201, "{\"ok\":true,\"name\":\"" + jsonEscape(name) + "\"}"};
    }

    std::pair<int, std::string> remove(const std::string& password, const std::string& rawName) {
        if (!authorized(password)) return {403, errorJson(u8"管理员口令错误")};
        const auto name = trim(rawName);
        if (!isAllowedName(name)) return {400, errorJson(u8"音乐名称无效")};
        std::lock_guard<std::mutex> guard(mutex_);
        std::error_code error;
        const bool removed = std::filesystem::remove(
            std::filesystem::u8path(root_) / std::filesystem::u8path(name), error);
        if (error) return {500, errorJson(u8"删除音乐失败")};
        if (!removed) return {404, errorJson(u8"音乐不存在")};
        return {200, "{\"ok\":true}"};
    }

    std::optional<std::filesystem::path> resolve(const std::string& rawName) const {
        const auto name = trim(rawName);
        if (!isAllowedName(name)) return std::nullopt;
        const auto path = std::filesystem::u8path(root_) / std::filesystem::u8path(name);
        if (!std::filesystem::is_regular_file(path)) return std::nullopt;
        return path;
    }

    static std::string contentType(const std::string& name) {
        const auto lower = asciiLower(name);
        if (endsWith(lower, ".mp3")) return "audio/mpeg";
        if (endsWith(lower, ".wav")) return "audio/wav";
        if (endsWith(lower, ".ogg")) return "audio/ogg";
        if (endsWith(lower, ".m4a")) return "audio/mp4";
        return "audio/aac";
    }

private:
    bool authorized(const std::string& password) const {
        return !adminPassword_.empty() && password == adminPassword_;
    }

    static bool isAllowedName(const std::string& name) {
        if (name.empty() || name.size() > 180 || name == "." || name == ".."
            || name.find('/') != std::string::npos || name.find('\\') != std::string::npos
            || name.find("..") != std::string::npos) return false;
        for (unsigned char ch : name) if (ch < 0x20) return false;
        const auto lower = asciiLower(name);
        return endsWith(lower, ".mp3") || endsWith(lower, ".wav") || endsWith(lower, ".ogg")
            || endsWith(lower, ".m4a") || endsWith(lower, ".aac");
    }

    static std::string errorJson(const std::string& message) {
        return "{\"error\":\"" + jsonEscape(message) + "\"}";
    }

    std::string root_;
    std::string adminPassword_;
    mutable std::mutex mutex_;
};

using Message = StoredMessage;
using LookupRecord = StoredLookup;
using GameScore = StoredScore;

struct UserSession {
    std::string name;
    std::chrono::steady_clock::time_point lastSeen;
    bool registered{};
};

class ChatState {
public:
    ChatState(Dictionary dictionary, const std::string& databasePath)
        : dictionary_(std::move(dictionary)), persistence_(databasePath) {
        messages_ = persistence_.loadMessages(kMaxMessages);
        lookups_ = persistence_.loadLookups(kMaxLookups);
        scores_ = persistence_.loadScores(50);
        if (messages_.empty()) appendLocked("bot", u8"词典机器人", u8"你好！在消息框输入“@词典 hello”就能查词。");
    }

    std::pair<int, std::string> join(const std::string& rawName) {
        const auto name = trim(rawName);
        if (name.empty()) return {400, errorJson(u8"请输入昵称")};
        if (name.size() > 60) return {400, errorJson(u8"昵称过长")};
        if (persistence_.accountExists(name)) return {409, errorJson(u8"该昵称属于注册用户，请使用密码登录")};
        std::lock_guard<std::mutex> guard(mutex_);
        removeExpiredLocked();
        return createSessionLocked(name, false, 201);
    }

    std::pair<int, std::string> registerAccount(const std::string& rawName, const std::string& password) {
        const auto name = trim(rawName);
        if (name.size() < 2 || name.size() > 40) return {400, errorJson(u8"用户名长度应为 2～20 个字符")};
        if (password.size() < 8 || password.size() > 128) return {400, errorJson(u8"密码至少需要 8 个字符")};
        if (name == u8"词典机器人" || name == u8"系统") return {409, errorJson(u8"这个用户名不可使用")};
        const auto result = persistence_.registerAccount(name, password);
        if (!result.first) return {result.second == "account_exists" ? 409 : 500,
            errorJson(result.second == "account_exists" ? u8"用户名已注册" : u8"注册失败")};
        std::lock_guard<std::mutex> guard(mutex_);
        removeExpiredLocked();
        return createSessionLocked(name, true, 201);
    }

    std::pair<int, std::string> loginAccount(const std::string& rawName, const std::string& password) {
        const auto name = canonicalAccountName(trim(rawName));
        if (name.empty() || !persistence_.authenticate(name, password)) return {401, errorJson(u8"用户名或密码错误")};
        std::lock_guard<std::mutex> guard(mutex_);
        removeExpiredLocked();
        return createSessionLocked(name, true, 200);
    }

    std::pair<int, std::string> send(const std::string& clientId, const std::string& rawText,
                                     std::string roomId, std::string recipient) {
        const auto text = trim(rawText);
        if (text.empty()) return {400, errorJson(u8"消息不能为空")};
        if (text.size() > 2000) return {400, errorJson(u8"消息过长")};
        roomId = trim(roomId);
        recipient = trim(recipient);
        if (roomId.empty()) roomId = "lobby";
        std::lock_guard<std::mutex> guard(mutex_);
        removeExpiredLocked();
        const auto user = users_.find(clientId);
        if (user == users_.end()) return {401, errorJson(u8"登录已失效，请重新进入聊天室")};
        user->second.lastSeen = std::chrono::steady_clock::now();
        if (!recipient.empty()) {
            const auto canonical = canonicalContactLocked(recipient);
            if (canonical.empty() || asciiLower(canonical) == asciiLower(user->second.name))
                return {404, errorJson(u8"私聊用户不存在")};
            recipient = canonical;
            roomId = "direct";
        } else if (!persistence_.roomExists(roomId)) {
            return {404, errorJson(u8"聊天室不存在")};
        }
        const auto message = appendLocked("user", user->second.name, text, roomId, recipient);
        if (recipient.empty()) persistence_.incrementRoomUnread(roomId, user->second.name);
        else if (persistence_.accountExists(recipient)) persistence_.incrementDirectUnread(recipient, user->second.name);
        const auto query = dictionary_.extractQuery(text);
        if (query && recipient.empty()) {
            const auto result = dictionary_.reply(*query);
            appendLocked("bot", u8"词典机器人", result, roomId);
            appendLookupLocked(user->second.name, *query, result);
        }
        return {201, messageJson(message)};
    }

    std::pair<int, std::string> lookup(const std::string& clientId, const std::string& rawQuery) {
        const auto query = trim(rawQuery);
        if (query.empty()) return {400, errorJson(u8"请输入要查询的单词")};
        if (query.size() > 200) return {400, errorJson(u8"查询内容过长")};
        std::lock_guard<std::mutex> guard(mutex_);
        removeExpiredLocked();
        const auto user = users_.find(clientId);
        if (user == users_.end()) return {401, errorJson(u8"登录已失效，请重新进入聊天室")};
        user->second.lastSeen = std::chrono::steady_clock::now();
        const auto result = dictionary_.reply(query);
        appendLookupLocked(user->second.name, query, result);
        return {200, "{\"query\":\"" + jsonEscape(query) + "\",\"result\":\"" + jsonEscape(result)
            + "\",\"time\":\"" + jsonEscape(currentTime()) + "\"}"};
    }

    std::pair<int, std::string> history(const std::string& clientId, const std::string& type,
                                        const std::string& rawQuery) {
        std::lock_guard<std::mutex> guard(mutex_);
        removeExpiredLocked();
        const auto user = users_.find(clientId);
        if (user == users_.end()) return {401, errorJson(u8"登录已失效，请重新进入聊天室")};
        user->second.lastSeen = std::chrono::steady_clock::now();
        const auto query = asciiLower(trim(rawQuery));

        std::ostringstream output;
        output << "{\"type\":\"" << jsonEscape(type) << "\",\"items\":[";
        bool first = true;
        std::size_t emitted = 0;
        if (type == "dictionary") {
            for (auto record = lookups_.rbegin(); record != lookups_.rend() && emitted < 100; ++record) {
                if (record->owner != user->second.name) continue;
                const auto searchable = asciiLower(record->query + " " + record->result);
                if (!query.empty() && searchable.find(query) == std::string::npos) continue;
                if (!first) output << ',';
                first = false;
                output << "{\"id\":" << record->id << ",\"query\":\"" << jsonEscape(record->query)
                       << "\",\"result\":\"" << jsonEscape(record->result) << "\",\"time\":\""
                       << jsonEscape(record->time) << "\"}";
                ++emitted;
            }
        } else {
            for (auto message = messages_.rbegin(); message != messages_.rend() && emitted < 100; ++message) {
                if (!message->recipient.empty()
                    && asciiLower(message->name) != asciiLower(user->second.name)
                    && asciiLower(message->recipient) != asciiLower(user->second.name)) continue;
                const auto searchable = asciiLower(message->name + " " + message->text);
                if (!query.empty() && searchable.find(query) == std::string::npos) continue;
                if (!first) output << ',';
                first = false;
                output << messageJson(*message);
                ++emitted;
            }
        }
        output << "],\"count\":" << emitted << '}';
        return {200, output.str()};
    }

    std::pair<int, std::string> submitGameScore(const std::string& clientId, long long rawScore) {
        if (rawScore < 0 || rawScore > 1000000) return {400, errorJson(u8"游戏分数无效")};
        std::lock_guard<std::mutex> guard(mutex_);
        removeExpiredLocked();
        const auto user = users_.find(clientId);
        if (user == users_.end()) return {401, errorJson(u8"登录已失效，请重新进入聊天室")};
        user->second.lastSeen = std::chrono::steady_clock::now();
        const GameScore score{0, user->second.name, static_cast<int>(rawScore), currentTime()};
        persistence_.insertScore(score);
        scores_ = persistence_.loadScores(50);
        return {201, "{\"ok\":true}"};
    }

    std::pair<int, std::string> gameScores() const {
        std::lock_guard<std::mutex> guard(mutex_);
        std::ostringstream output;
        output << "{\"scores\":[";
        for (std::size_t index = 0; index < scores_.size() && index < 10; ++index) {
            if (index) output << ',';
            output << "{\"rank\":" << index + 1 << ",\"name\":\"" << jsonEscape(scores_[index].name)
                   << "\",\"score\":" << scores_[index].score << ",\"time\":\""
                   << jsonEscape(scores_[index].time) << "\"}";
        }
        output << "]}";
        return {200, output.str()};
    }

    std::pair<int, std::string> rooms(const std::string& clientId) {
        std::lock_guard<std::mutex> guard(mutex_);
        removeExpiredLocked();
        const auto user = users_.find(clientId);
        if (user == users_.end()) return {401, errorJson(u8"登录已失效")};
        user->second.lastSeen = std::chrono::steady_clock::now();
        const auto records = persistence_.rooms(user->second.registered ? user->second.name : "");
        std::ostringstream output; output << "{\"rooms\":[";
        for (std::size_t index=0; index<records.size(); ++index) {
            if(index) output << ',';
            output << "{\"id\":\"" << jsonEscape(records[index].id) << "\",\"name\":\""
                   << jsonEscape(records[index].name) << "\",\"unread\":" << records[index].unread << '}';
        }
        output << "]}"; return {200,output.str()};
    }

    std::pair<int, std::string> contacts(const std::string& clientId) {
        std::lock_guard<std::mutex> guard(mutex_);
        removeExpiredLocked();
        const auto user=users_.find(clientId);
        if(user==users_.end()) return {401,errorJson(u8"登录已失效")};
        user->second.lastSeen=std::chrono::steady_clock::now();
        auto records=persistence_.contacts(user->second.registered?user->second.name:"");
        for(const auto& online:users_) {
            if(asciiLower(online.second.name)==asciiLower(user->second.name)) continue;
            const bool exists=std::any_of(records.begin(),records.end(),[&](const StoredContact& item){return asciiLower(item.name)==asciiLower(online.second.name);});
            if(!exists) records.push_back({online.second.name,0});
        }
        std::sort(records.begin(),records.end(),[](const StoredContact& left,const StoredContact& right){return asciiLower(left.name)<asciiLower(right.name);});
        std::ostringstream output; output << "{\"contacts\":[";
        for(std::size_t index=0;index<records.size();++index){if(index)output<<',';output<<"{\"name\":\""<<jsonEscape(records[index].name)<<"\",\"unread\":"<<records[index].unread<<'}';}
        output<<"]}"; return {200,output.str()};
    }

    std::pair<int,std::string> createRoom(const std::string& clientId,const std::string& rawName) {
        const auto name=trim(rawName);
        if(name.size()<2||name.size()>40) return {400,errorJson(u8"房间名称长度应为 2～20 个字符")};
        std::lock_guard<std::mutex> guard(mutex_); removeExpiredLocked();
        const auto user=users_.find(clientId); if(user==users_.end()) return {401,errorJson(u8"登录已失效")};
        if(!user->second.registered) return {403,errorJson(u8"注册用户才能创建房间")};
        const auto id="room-"+makeClientId().substr(0,10);
        const auto result=persistence_.createRoom(id,name);
        if(!result.first) return {409,errorJson(u8"房间名称已存在")};
        return {201,"{\"id\":\""+jsonEscape(id)+"\",\"name\":\""+jsonEscape(name)+"\"}"};
    }

    std::pair<int, std::string> poll(const std::string& clientId, std::uint64_t after,
                                     std::string roomId, std::string recipient) {
        if(roomId.empty()) roomId="lobby";
        std::lock_guard<std::mutex> guard(mutex_);
        removeExpiredLocked();
        const auto user = users_.find(clientId);
        if (user == users_.end()) return {401, errorJson(u8"登录已失效，请重新进入聊天室")};
        user->second.lastSeen = std::chrono::steady_clock::now();

        std::vector<std::string> names;
        names.reserve(users_.size());
        for (const auto& item : users_) names.push_back(item.second.name);
        std::sort(names.begin(), names.end());

        std::ostringstream output;
        output << "{\"messages\":[";
        bool first = true;
        std::uint64_t lastVisible = after;
        for (const auto& message : messages_) {
            if (message.id <= after) continue;
            bool visible=false;
            if(recipient.empty()) visible=message.recipient.empty()&&message.roomId==roomId;
            else visible=(asciiLower(message.name)==asciiLower(user->second.name)&&asciiLower(message.recipient)==asciiLower(recipient))
                ||(asciiLower(message.name)==asciiLower(recipient)&&asciiLower(message.recipient)==asciiLower(user->second.name));
            if(!visible) continue;
            if (!first) output << ',';
            first = false;
            output << messageJson(message);
            lastVisible=message.id;
        }
        if(user->second.registered) persistence_.clearUnread(user->second.name,recipient.empty()?"room:"+roomId:"dm:"+asciiLower(recipient));
        output << "],\"users\":[{\"name\":\"" << jsonEscape(u8"词典机器人") << "\",\"bot\":true,\"registered\":true}";
        for (const auto& name : names) {
            bool registered=false; for(const auto& item:users_) if(item.second.name==name){registered=item.second.registered;break;}
            output << ",{\"name\":\"" << jsonEscape(name) << "\",\"bot\":false,\"registered\":"<<(registered?"true":"false")<<'}';
        }
        output << "],\"lastId\":" << lastVisible << '}';
        return {200, output.str()};
    }

    void leave(const std::string& clientId) {
        std::lock_guard<std::mutex> guard(mutex_);
        const auto user = users_.find(clientId);
        if (user == users_.end()) return;
        const auto name = user->second.name;
        users_.erase(user);
        appendLocked("system", u8"系统", name + u8" 已离开聊天室");
    }

    std::size_t dictionarySize() const { return dictionary_.size(); }

private:
    Message appendLocked(std::string kind, std::string name, std::string text,
                         std::string roomId="lobby", std::string recipient="") {
        Message message{0, std::move(kind), std::move(name), std::move(text), currentTime(),std::move(roomId),std::move(recipient)};
        message.id=persistence_.insertMessage(message);
        messages_.push_back(message);
        if (messages_.size() > kMaxMessages) messages_.erase(messages_.begin());
        return message;
    }

    void appendLookupLocked(const std::string& owner, const std::string& query, const std::string& result) {
        LookupRecord record{0,owner,query,result,currentTime()};
        persistence_.insertLookup(record);
        lookups_=persistence_.loadLookups(kMaxLookups);
    }

    void removeExpiredLocked() {
        const auto deadline = std::chrono::steady_clock::now() - kSessionTtl;
        for (auto user = users_.begin(); user != users_.end();) {
            if (user->second.lastSeen < deadline) {
                const auto name = user->second.name;
                user = users_.erase(user);
                appendLocked("system", u8"系统", name + u8" 已离开聊天室");
            } else {
                ++user;
            }
        }
    }

    static std::string makeClientId() {
        static std::mutex randomMutex;
        static std::mt19937_64 generator(std::random_device{}());
        std::lock_guard<std::mutex> guard(randomMutex);
        std::ostringstream output;
        output << std::hex << generator() << generator() << generator();
        return output.str();
    }

    static std::string errorJson(const std::string& message) {
        return "{\"error\":\"" + jsonEscape(message) + "\"}";
    }

    static std::string messageJson(const Message& message) {
        return "{\"id\":" + std::to_string(message.id)
            + ",\"kind\":\"" + jsonEscape(message.kind)
            + "\",\"name\":\"" + jsonEscape(message.name)
            + "\",\"text\":\"" + jsonEscape(message.text)
            + "\",\"time\":\"" + jsonEscape(message.time)
            + "\",\"roomId\":\"" + jsonEscape(message.roomId)
            + "\",\"recipient\":\"" + jsonEscape(message.recipient) + "\"}";
    }

    std::pair<int,std::string> createSessionLocked(const std::string& name,bool registered,int status) {
        const auto folded=asciiLower(name);
        for(const auto& user:users_) if(asciiLower(user.second.name)==folded) return {409,errorJson(u8"该用户已经在线")};
        if(name==u8"词典机器人") return {409,errorJson(u8"这个昵称已被使用")};
        const auto clientId=makeClientId(); users_[clientId]={name,std::chrono::steady_clock::now(),registered};
        appendLocked("system",u8"系统",name+u8" 加入了聊天室");
        return {status,"{\"clientId\":\""+jsonEscape(clientId)+"\",\"name\":\""+jsonEscape(name)
            +"\",\"authMode\":\""+(registered?"account":"guest")+"\"}"};
    }

    std::string canonicalAccountName(const std::string& name) {
        const auto folded=asciiLower(name); for(const auto& candidate:persistence_.accountNames()) if(asciiLower(candidate)==folded) return candidate; return {};
    }

    std::string canonicalContactLocked(const std::string& name) {
        const auto account=canonicalAccountName(name); if(!account.empty()) return account;
        const auto folded=asciiLower(name); for(const auto& item:users_) if(asciiLower(item.second.name)==folded) return item.second.name; return {};
    }

    Dictionary dictionary_;
    Persistence persistence_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, UserSession> users_;
    std::vector<Message> messages_;
    std::vector<LookupRecord> lookups_;
    std::vector<GameScore> scores_;
};

struct HttpRequest {
    std::string method;
    std::string target;
    std::unordered_map<std::string, std::string> headers;
    std::string body;
};

bool receiveRequest(SOCKET socket, HttpRequest& request) {
    std::string data;
    char buffer[4096];
    std::size_t headerEnd = std::string::npos;
    while ((headerEnd = data.find("\r\n\r\n")) == std::string::npos) {
        const int received = recv(socket, buffer, sizeof(buffer), 0);
        if (received <= 0) return false;
        data.append(buffer, static_cast<std::size_t>(received));
        if (data.size() > kMaxHeaderSize) return false;
    }

    std::istringstream headerStream(data.substr(0, headerEnd));
    std::string version;
    if (!(headerStream >> request.method >> request.target >> version)) return false;
    std::string line;
    std::getline(headerStream, line);
    while (std::getline(headerStream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const auto colon = line.find(':');
        if (colon != std::string::npos) {
            request.headers[asciiLower(trim(line.substr(0, colon)))] = trim(line.substr(colon + 1));
        }
    }

    std::size_t contentLength = 0;
    const auto length = request.headers.find("content-length");
    if (length != request.headers.end()) {
        try { contentLength = static_cast<std::size_t>(std::stoul(length->second)); }
        catch (...) { return false; }
    }
    const bool musicUpload = request.method == "POST"
        && request.target.rfind("/api/admin/music", 0) == 0;
    const auto maxBody = musicUpload ? kMaxMusicUpload : kMaxJsonBody;
    if (contentLength > maxBody) return false;
    request.body = data.substr(headerEnd + 4);
    while (request.body.size() < contentLength) {
        const int received = recv(socket, buffer, sizeof(buffer), 0);
        if (received <= 0) return false;
        request.body.append(buffer, static_cast<std::size_t>(received));
        if (request.body.size() > maxBody) return false;
    }
    request.body.resize(contentLength);
    return true;
}

bool sendAll(SOCKET socket, const std::string& data) {
    std::size_t sentTotal = 0;
    while (sentTotal < data.size()) {
        const int sent = send(socket, data.data() + sentTotal,
            static_cast<int>(std::min<std::size_t>(data.size() - sentTotal, INT_MAX)), 0);
        if (sent <= 0) return false;
        sentTotal += static_cast<std::size_t>(sent);
    }
    return true;
}

std::string statusText(int status) {
    switch (status) {
        case 200: return "OK";
        case 201: return "Created";
        case 206: return "Partial Content";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 409: return "Conflict";
        case 413: return "Payload Too Large";
        default: return "Internal Server Error";
    }
}

void sendResponse(SOCKET socket, int status, const std::string& contentType, const std::string& body) {
    std::ostringstream response;
    response << "HTTP/1.1 " << status << ' ' << statusText(status) << "\r\n"
             << "Content-Type: " << contentType << "\r\n"
             << "Content-Length: " << body.size() << "\r\n"
             << "Cache-Control: no-store\r\n"
             << "Connection: close\r\n\r\n" << body;
    sendAll(socket, response.str());
}

void sendMusicResponse(SOCKET socket, const std::filesystem::path& path, const std::string& contentType,
                       const std::string& rangeHeader) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        sendResponse(socket, 404, "application/json; charset=utf-8", "{\"error\":\"Music not found\"}");
        return;
    }
    const auto size = static_cast<std::uint64_t>(input.tellg());
    std::uint64_t start = 0;
    std::uint64_t end = size ? size - 1 : 0;
    bool partial = false;
    if (rangeHeader.rfind("bytes=", 0) == 0 && size) {
        const auto range = rangeHeader.substr(6);
        const auto dash = range.find('-');
        try {
            if (dash != std::string::npos && dash > 0) {
                start = std::stoull(range.substr(0, dash));
                if (dash + 1 < range.size()) end = std::stoull(range.substr(dash + 1));
                if (start < size) {
                    end = std::min(end, size - 1);
                    partial = start <= end;
                }
            }
        } catch (...) { partial = false; start = 0; end = size - 1; }
    }
    const auto length = size ? end - start + 1 : 0;
    std::string body(static_cast<std::size_t>(length), '\0');
    input.seekg(static_cast<std::streamoff>(start));
    if (length) input.read(body.data(), static_cast<std::streamsize>(length));
    std::ostringstream headers;
    headers << "HTTP/1.1 " << (partial ? 206 : 200) << ' '
            << statusText(partial ? 206 : 200) << "\r\nContent-Type: " << contentType
            << "\r\nContent-Length: " << body.size() << "\r\nAccept-Ranges: bytes\r\n";
    if (partial) headers << "Content-Range: bytes " << start << '-' << end << '/' << size << "\r\n";
    headers << "Cache-Control: no-cache\r\nConnection: close\r\n\r\n";
    sendAll(socket, headers.str());
    sendAll(socket, body);
}

std::optional<std::string> readFile(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return std::nullopt;
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

void handleClient(SOCKET client, ChatState& state, MusicLibrary& music) {
    HttpRequest request;
    if (!receiveRequest(client, request)) {
        sendResponse(client, 400, "application/json; charset=utf-8", "{\"error\":\"Bad request\"}");
        closesocket(client);
        return;
    }

    const auto pathEnd = request.target.find('?');
    const auto path = request.target.substr(0, pathEnd);
    if (request.method == "GET" && path == "/api/health") {
        sendResponse(client, 200, "application/json; charset=utf-8",
            "{\"ok\":true,\"backend\":\"cpp\",\"dictionarySize\":" + std::to_string(state.dictionarySize()) + "}");
    } else if (request.method == "GET" && path == "/api/game/scores") {
        const auto result = state.gameScores();
        sendResponse(client, result.first, "application/json; charset=utf-8", result.second);
    } else if (request.method == "POST" && path == "/api/game/scores") {
        const auto clientId = jsonStringField(request.body, "clientId");
        const auto score = jsonIntegerField(request.body, "score");
        const auto result = clientId && score ? state.submitGameScore(*clientId, *score)
            : std::pair<int, std::string>{400, "{\"error\":\"Invalid score data\"}"};
        sendResponse(client, result.first, "application/json; charset=utf-8", result.second);
    } else if (request.method == "GET" && path == "/api/music") {
        const auto result = music.list();
        sendResponse(client, result.first, "application/json; charset=utf-8", result.second);
    } else if (request.method == "GET" && path == "/api/music/file") {
        const auto query = parseQuery(request.target);
        const auto name = query.find("name");
        const auto file = name == query.end() ? std::nullopt : music.resolve(name->second);
        if (!file) {
            sendResponse(client, 404, "application/json; charset=utf-8", "{\"error\":\"Music not found\"}");
        } else {
            const auto range = request.headers.find("range");
            sendMusicResponse(client, *file, MusicLibrary::contentType(name->second),
                range == request.headers.end() ? "" : range->second);
        }
    } else if (request.method == "POST" && path == "/api/admin/music") {
        const auto query = parseQuery(request.target);
        const auto name = query.find("name");
        const auto password = request.headers.find("x-admin-password");
        const auto result = name != query.end() && password != request.headers.end()
            ? music.upload(password->second, name->second, request.body)
            : std::pair<int, std::string>{400, "{\"error\":\"Missing upload data\"}"};
        sendResponse(client, result.first, "application/json; charset=utf-8", result.second);
    } else if (request.method == "DELETE" && path == "/api/admin/music") {
        const auto query = parseQuery(request.target);
        const auto name = query.find("name");
        const auto password = request.headers.find("x-admin-password");
        const auto result = name != query.end() && password != request.headers.end()
            ? music.remove(password->second, name->second)
            : std::pair<int, std::string>{400, "{\"error\":\"Missing delete data\"}"};
        sendResponse(client, result.first, "application/json; charset=utf-8", result.second);
    } else if (request.method == "POST" && path == "/api/register") {
        const auto name=jsonStringField(request.body,"name");
        const auto password=jsonStringField(request.body,"password");
        const auto result=name&&password?state.registerAccount(*name,*password)
            :std::pair<int,std::string>{400,"{\"error\":\"Invalid registration data\"}"};
        sendResponse(client,result.first,"application/json; charset=utf-8",result.second);
    } else if (request.method == "POST" && path == "/api/login") {
        const auto name=jsonStringField(request.body,"name");
        const auto password=jsonStringField(request.body,"password");
        const auto result=name&&password?state.loginAccount(*name,*password)
            :std::pair<int,std::string>{400,"{\"error\":\"Invalid login data\"}"};
        sendResponse(client,result.first,"application/json; charset=utf-8",result.second);
    } else if (request.method == "POST" && path == "/api/join") {
        const auto name = jsonStringField(request.body, "name");
        const auto result = name ? state.join(*name) : std::pair<int, std::string>{400, "{\"error\":\"Invalid JSON\"}"};
        sendResponse(client, result.first, "application/json; charset=utf-8", result.second);
    } else if (request.method == "POST" && path == "/api/messages") {
        const auto clientId = jsonStringField(request.body, "clientId");
        const auto text = jsonStringField(request.body, "text");
        const auto roomId=jsonStringField(request.body,"roomId");
        const auto recipient=jsonStringField(request.body,"recipient");
        const auto result = clientId && text ? state.send(*clientId, *text,roomId?*roomId:"lobby",recipient?*recipient:"")
            : std::pair<int, std::string>{400, "{\"error\":\"Invalid JSON\"}"};
        sendResponse(client, result.first, "application/json; charset=utf-8", result.second);
    } else if (request.method == "GET" && path == "/api/rooms") {
        const auto query=parseQuery(request.target); const auto clientId=query.find("clientId");
        const auto result=clientId==query.end()?std::pair<int,std::string>{401,"{\"error\":\"Missing session\"}"}:state.rooms(clientId->second);
        sendResponse(client,result.first,"application/json; charset=utf-8",result.second);
    } else if (request.method == "POST" && path == "/api/rooms") {
        const auto clientId=jsonStringField(request.body,"clientId"); const auto name=jsonStringField(request.body,"name");
        const auto result=clientId&&name?state.createRoom(*clientId,*name):std::pair<int,std::string>{400,"{\"error\":\"Invalid room data\"}"};
        sendResponse(client,result.first,"application/json; charset=utf-8",result.second);
    } else if (request.method == "GET" && path == "/api/contacts") {
        const auto query=parseQuery(request.target); const auto clientId=query.find("clientId");
        const auto result=clientId==query.end()?std::pair<int,std::string>{401,"{\"error\":\"Missing session\"}"}:state.contacts(clientId->second);
        sendResponse(client,result.first,"application/json; charset=utf-8",result.second);
    } else if (request.method == "GET" && path == "/api/dictionary") {
        const auto query = parseQuery(request.target);
        const auto clientId = query.find("clientId");
        const auto word = query.find("q");
        const auto result = clientId != query.end() && word != query.end()
            ? state.lookup(clientId->second, word->second)
            : std::pair<int, std::string>{400, "{\"error\":\"Missing query\"}"};
        sendResponse(client, result.first, "application/json; charset=utf-8", result.second);
    } else if (request.method == "GET" && path == "/api/history") {
        const auto query = parseQuery(request.target);
        const auto clientId = query.find("clientId");
        const auto type = query.find("type");
        const auto search = query.find("q");
        const auto result = clientId != query.end()
            ? state.history(clientId->second, type == query.end() ? "chat" : type->second,
                search == query.end() ? "" : search->second)
            : std::pair<int, std::string>{401, "{\"error\":\"Missing session\"}"};
        sendResponse(client, result.first, "application/json; charset=utf-8", result.second);
    } else if (request.method == "GET" && path == "/api/messages") {
        const auto query = parseQuery(request.target);
        const auto clientId = query.find("clientId");
        const auto roomId=query.find("roomId");
        const auto recipient=query.find("recipient");
        std::uint64_t after = 0;
        try {
            const auto value = query.find("after");
            if (value != query.end()) after = std::stoull(value->second);
        } catch (...) {
            sendResponse(client, 400, "application/json; charset=utf-8", "{\"error\":\"Invalid after value\"}");
            closesocket(client);
            return;
        }
        const auto result = clientId == query.end()
            ? std::pair<int, std::string>{401, "{\"error\":\"Missing session\"}"}
            : state.poll(clientId->second, after,roomId==query.end()?"lobby":roomId->second,recipient==query.end()?"":recipient->second);
        sendResponse(client, result.first, "application/json; charset=utf-8", result.second);
    } else if (request.method == "POST" && path == "/api/leave") {
        const auto clientId = jsonStringField(request.body, "clientId");
        if (clientId) state.leave(*clientId);
        sendResponse(client, 200, "application/json; charset=utf-8", "{\"ok\":true}");
    } else {
        std::unordered_map<std::string, std::pair<std::string, std::string>> files = {
            {"/", {"static/index.html", "text/html; charset=utf-8"}},
            {"/index.html", {"static/index.html", "text/html; charset=utf-8"}},
            {"/styles.css", {"static/styles.css", "text/css; charset=utf-8"}},
            {"/app.js", {"static/app.js", "application/javascript; charset=utf-8"}},
            {"/web", {"static/web.html", "text/html; charset=utf-8"}},
            {"/web.html", {"static/web.html", "text/html; charset=utf-8"}},
            {"/web.css", {"static/web.css", "text/css; charset=utf-8"}},
            {"/web.js", {"static/web.js", "application/javascript; charset=utf-8"}}
        };
        for (int index = 1; index <= 10; ++index) {
            std::ostringstream name;
            name << "/images/scene-" << std::setw(2) << std::setfill('0') << index << ".jpg";
            files[name.str()] = {"static" + name.str(), "image/jpeg"};
        }
        const auto file = files.find(path);
        if (request.method == "GET" && file != files.end()) {
            const auto content = readFile(file->second.first);
            if (content) sendResponse(client, 200, file->second.second, *content);
            else sendResponse(client, 404, "text/plain; charset=utf-8", "File not found");
        } else {
            sendResponse(client, 404, "application/json; charset=utf-8", "{\"error\":\"Not found\"}");
        }
    }
    closesocket(client);
}

struct WinsockSession {
    WinsockSession() {
        WSADATA data{};
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0) throw std::runtime_error("WSAStartup failed.");
    }
    ~WinsockSession() { WSACleanup(); }
};

} // namespace

int main(int argc, char** argv) {
    SetConsoleOutputCP(CP_UTF8);
    std::string host = "127.0.0.1";
    int port = 8000;
    bool openBrowser = false;
    bool openWebsite = false;
    std::string adminPassword = "gensokyo-admin";
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--host" && index + 1 < argc) host = argv[++index];
        else if (argument == "--port" && index + 1 < argc) port = std::stoi(argv[++index]);
        else if (argument == "--open") openBrowser = true;
        else if (argument == "--open-website") openWebsite = true;
        else if (argument == "--admin-password" && index + 1 < argc) adminPassword = argv[++index];
    }

    try {
        WinsockSession winsock;
        ChatState state(Dictionary("data/dict.txt"),"data/chat.db");
        MusicLibrary music("data/music", adminPassword);
        SOCKET server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (server == INVALID_SOCKET) throw std::runtime_error("Cannot create server socket.");
        BOOL reuse = TRUE;
        setsockopt(server, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(static_cast<u_short>(port));
        address.sin_addr.s_addr = inet_addr(host.c_str());
        if (address.sin_addr.s_addr == INADDR_NONE && host != "255.255.255.255") {
            closesocket(server);
            throw std::runtime_error("Invalid IPv4 address: " + host);
        }
        if (bind(server, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR) {
            const int error = WSAGetLastError();
            closesocket(server);
            throw std::runtime_error("Cannot bind port " + std::to_string(port) + " (error " + std::to_string(error) + ").");
        }
        if (listen(server, SOMAXCONN) == SOCKET_ERROR) {
            closesocket(server);
            throw std::runtime_error("Cannot listen on the server socket.");
        }

        const std::string shownHost = host == "0.0.0.0" ? "127.0.0.1" : host;
        const std::string url = "http://" + shownHost + ":" + std::to_string(port);
        std::cout << u8"C++ 聊天室已启动：" << url << '\n'
                  << u8"已加载词条：" << state.dictionarySize() << '\n'
                  << u8"请保持此窗口开启，按 Ctrl+C 停止服务。" << std::endl;
        if (openBrowser || openWebsite) {
            const auto openUrl = openWebsite ? url + "/web" : url;
            ShellExecuteA(nullptr, "open", openUrl.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }

        while (true) {
            SOCKET client = accept(server, nullptr, nullptr);
            if (client == INVALID_SOCKET) continue;
            std::thread([client, &state, &music] { handleClient(client, state, music); }).detach();
        }
    } catch (const std::exception& error) {
        std::cerr << "Startup failed: " << error.what() << std::endl;
        return 1;
    }
}
