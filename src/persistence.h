#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

struct sqlite3;

struct StoredMessage {
    std::uint64_t id{};
    std::string kind;
    std::string name;
    std::string text;
    std::string time;
    std::string roomId;
    std::string recipient;
};

struct StoredLookup {
    std::uint64_t id{};
    std::string owner;
    std::string query;
    std::string result;
    std::string time;
};

struct StoredScore {
    std::uint64_t id{};
    std::string name;
    int score{};
    std::string time;
};

struct StoredRoom {
    std::string id;
    std::string name;
    int unread{};
};

struct StoredContact {
    std::string name;
    int unread{};
};

class Persistence {
public:
    explicit Persistence(const std::string& path);
    ~Persistence();
    Persistence(const Persistence&) = delete;
    Persistence& operator=(const Persistence&) = delete;

    std::pair<bool, std::string> registerAccount(const std::string& username, const std::string& password);
    bool authenticate(const std::string& username, const std::string& password);
    bool accountExists(const std::string& username);
    std::vector<std::string> accountNames();

    std::vector<StoredRoom> rooms(const std::string& owner);
    std::pair<bool, std::string> createRoom(const std::string& id, const std::string& name);
    bool roomExists(const std::string& id);
    std::vector<StoredContact> contacts(const std::string& owner);
    void incrementRoomUnread(const std::string& roomId, const std::string& sender);
    void incrementDirectUnread(const std::string& recipient, const std::string& sender);
    void clearUnread(const std::string& owner, const std::string& channel);

    std::uint64_t insertMessage(const StoredMessage& message);
    std::vector<StoredMessage> loadMessages(std::size_t limit);
    void insertLookup(const StoredLookup& lookup);
    std::vector<StoredLookup> loadLookups(std::size_t limit);
    void insertScore(const StoredScore& score);
    std::vector<StoredScore> loadScores(std::size_t limit);

private:
    void exec(const char* sql);
    void migrate();

    sqlite3* database_{};
    std::mutex mutex_;
};
