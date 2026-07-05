#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "persistence.h"
#include "vendor/sqlite/sqlite3.h"

#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace {

constexpr ULONG kSaltBytes = 16;
constexpr ULONG kHashBytes = 32;
constexpr ULONGLONG kPasswordRounds = 120000;

std::string hexEncode(const unsigned char* data, std::size_t size) {
    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (std::size_t index = 0; index < size; ++index) output << std::setw(2) << static_cast<int>(data[index]);
    return output.str();
}

std::vector<unsigned char> hexDecode(const std::string& value) {
    if (value.size() % 2) return {};
    std::vector<unsigned char> result(value.size() / 2);
    for (std::size_t index = 0; index < result.size(); ++index) {
        try { result[index] = static_cast<unsigned char>(std::stoul(value.substr(index * 2, 2), nullptr, 16)); }
        catch (...) { return {}; }
    }
    return result;
}

std::string derivePassword(const std::string& password, const unsigned char* salt, ULONG saltSize) {
    BCRYPT_ALG_HANDLE algorithm{};
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr,
            BCRYPT_ALG_HANDLE_HMAC_FLAG) < 0) throw std::runtime_error("BCryptOpenAlgorithmProvider failed");
    std::array<unsigned char, kHashBytes> hash{};
    const auto status = BCryptDeriveKeyPBKDF2(algorithm,
        reinterpret_cast<PUCHAR>(const_cast<char*>(password.data())), static_cast<ULONG>(password.size()),
        const_cast<PUCHAR>(salt), saltSize, kPasswordRounds, hash.data(), static_cast<ULONG>(hash.size()), 0);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (status < 0) throw std::runtime_error("BCryptDeriveKeyPBKDF2 failed");
    return hexEncode(hash.data(), hash.size());
}

bool constantTimeEqual(const std::string& left, const std::string& right) {
    unsigned char difference = static_cast<unsigned char>(left.size() ^ right.size());
    const auto count = std::min(left.size(), right.size());
    for (std::size_t index = 0; index < count; ++index) difference |= static_cast<unsigned char>(left[index] ^ right[index]);
    return difference == 0;
}

std::string columnText(sqlite3_stmt* statement, int index) {
    const auto* value = sqlite3_column_text(statement, index);
    return value ? reinterpret_cast<const char*>(value) : "";
}

struct Statement {
    sqlite3_stmt* value{};
    Statement(sqlite3* database, const char* sql) {
        if (sqlite3_prepare_v2(database, sql, -1, &value, nullptr) != SQLITE_OK)
            throw std::runtime_error(sqlite3_errmsg(database));
    }
    ~Statement() { if (value) sqlite3_finalize(value); }
};

void bindText(sqlite3_stmt* statement, int index, const std::string& value) {
    sqlite3_bind_text(statement, index, value.c_str(), static_cast<int>(value.size()), SQLITE_TRANSIENT);
}

} // namespace

Persistence::Persistence(const std::string& path) {
    if (sqlite3_open_v2(path.c_str(), &database_, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr) != SQLITE_OK) {
        const std::string message = database_ ? sqlite3_errmsg(database_) : "Cannot open database";
        if (database_) sqlite3_close(database_);
        database_ = nullptr;
        throw std::runtime_error(message);
    }
    sqlite3_busy_timeout(database_, 3000);
    exec("PRAGMA journal_mode=WAL; PRAGMA foreign_keys=ON; PRAGMA synchronous=NORMAL;");
    migrate();
}

Persistence::~Persistence() { if (database_) sqlite3_close(database_); }

void Persistence::exec(const char* sql) {
    char* error{};
    if (sqlite3_exec(database_, sql, nullptr, nullptr, &error) != SQLITE_OK) {
        const std::string message = error ? error : "SQLite error";
        sqlite3_free(error);
        throw std::runtime_error(message);
    }
}

void Persistence::migrate() {
    exec(R"SQL(
        CREATE TABLE IF NOT EXISTS accounts(
            username TEXT PRIMARY KEY COLLATE NOCASE,
            password_hash TEXT NOT NULL,
            salt TEXT NOT NULL,
            created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
        );
        CREATE TABLE IF NOT EXISTS rooms(
            id TEXT PRIMARY KEY,
            name TEXT NOT NULL UNIQUE,
            created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
        );
        CREATE TABLE IF NOT EXISTS messages(
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            kind TEXT NOT NULL,
            sender TEXT NOT NULL,
            text TEXT NOT NULL,
            display_time TEXT NOT NULL,
            room_id TEXT NOT NULL DEFAULT 'lobby',
            recipient TEXT NOT NULL DEFAULT '',
            created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
        );
        CREATE INDEX IF NOT EXISTS messages_room_id ON messages(room_id,id);
        CREATE INDEX IF NOT EXISTS messages_direct ON messages(sender,recipient,id);
        CREATE TABLE IF NOT EXISTS lookups(
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            owner TEXT NOT NULL,
            query TEXT NOT NULL,
            result TEXT NOT NULL,
            display_time TEXT NOT NULL,
            created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
        );
        CREATE TABLE IF NOT EXISTS scores(
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT NOT NULL,
            score INTEGER NOT NULL,
            display_time TEXT NOT NULL,
            created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
        );
        CREATE TABLE IF NOT EXISTS unread(
            owner TEXT NOT NULL COLLATE NOCASE,
            channel TEXT NOT NULL,
            count INTEGER NOT NULL DEFAULT 0,
            PRIMARY KEY(owner,channel)
        );
        INSERT OR IGNORE INTO rooms(id,name) VALUES
            ('lobby','公共大厅'),('english','英语角'),('music','音乐茶室');
    )SQL");
}

std::pair<bool, std::string> Persistence::registerAccount(const std::string& username, const std::string& password) {
    std::lock_guard<std::mutex> guard(mutex_);
    std::array<unsigned char, kSaltBytes> salt{};
    if (BCryptGenRandom(nullptr, salt.data(), static_cast<ULONG>(salt.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
        return {false, "Cannot generate password salt"};
    const auto hash = derivePassword(password, salt.data(), static_cast<ULONG>(salt.size()));
    Statement statement(database_, "INSERT INTO accounts(username,password_hash,salt) VALUES(?,?,?);");
    bindText(statement.value, 1, username);
    bindText(statement.value, 2, hash);
    bindText(statement.value, 3, hexEncode(salt.data(), salt.size()));
    const auto result = sqlite3_step(statement.value);
    if (result == SQLITE_CONSTRAINT) return {false, "account_exists"};
    if (result != SQLITE_DONE) return {false, sqlite3_errmsg(database_)};
    return {true, ""};
}

bool Persistence::authenticate(const std::string& username, const std::string& password) {
    std::lock_guard<std::mutex> guard(mutex_);
    Statement statement(database_, "SELECT password_hash,salt FROM accounts WHERE username=? COLLATE NOCASE;");
    bindText(statement.value, 1, username);
    if (sqlite3_step(statement.value) != SQLITE_ROW) return false;
    const auto expected = columnText(statement.value, 0);
    const auto salt = hexDecode(columnText(statement.value, 1));
    if (salt.empty()) return false;
    return constantTimeEqual(expected, derivePassword(password, salt.data(), static_cast<ULONG>(salt.size())));
}

bool Persistence::accountExists(const std::string& username) {
    std::lock_guard<std::mutex> guard(mutex_);
    Statement statement(database_, "SELECT 1 FROM accounts WHERE username=? COLLATE NOCASE;");
    bindText(statement.value, 1, username);
    return sqlite3_step(statement.value) == SQLITE_ROW;
}

std::vector<std::string> Persistence::accountNames() {
    std::lock_guard<std::mutex> guard(mutex_);
    Statement statement(database_, "SELECT username FROM accounts ORDER BY username COLLATE NOCASE;");
    std::vector<std::string> result;
    while (sqlite3_step(statement.value) == SQLITE_ROW) result.push_back(columnText(statement.value, 0));
    return result;
}

std::vector<StoredRoom> Persistence::rooms(const std::string& owner) {
    std::lock_guard<std::mutex> guard(mutex_);
    Statement statement(database_, R"SQL(
        SELECT r.id,r.name,COALESCE(u.count,0)
        FROM rooms r LEFT JOIN unread u ON u.owner=? COLLATE NOCASE AND u.channel='room:'||r.id
        ORDER BY CASE r.id WHEN 'lobby' THEN 0 WHEN 'english' THEN 1 WHEN 'music' THEN 2 ELSE 3 END,r.name;
    )SQL");
    bindText(statement.value, 1, owner);
    std::vector<StoredRoom> result;
    while (sqlite3_step(statement.value) == SQLITE_ROW)
        result.push_back({columnText(statement.value,0),columnText(statement.value,1),sqlite3_column_int(statement.value,2)});
    return result;
}

std::pair<bool, std::string> Persistence::createRoom(const std::string& id, const std::string& name) {
    std::lock_guard<std::mutex> guard(mutex_);
    Statement statement(database_, "INSERT INTO rooms(id,name) VALUES(?,?);");
    bindText(statement.value,1,id); bindText(statement.value,2,name);
    const auto result=sqlite3_step(statement.value);
    if(result==SQLITE_CONSTRAINT) return {false,"room_exists"};
    if(result!=SQLITE_DONE) return {false,sqlite3_errmsg(database_)};
    return {true,""};
}

bool Persistence::roomExists(const std::string& id) {
    std::lock_guard<std::mutex> guard(mutex_);
    Statement statement(database_,"SELECT 1 FROM rooms WHERE id=?;"); bindText(statement.value,1,id);
    return sqlite3_step(statement.value)==SQLITE_ROW;
}

std::vector<StoredContact> Persistence::contacts(const std::string& owner) {
    std::lock_guard<std::mutex> guard(mutex_);
    Statement statement(database_, R"SQL(
        SELECT a.username,COALESCE(u.count,0) FROM accounts a
        LEFT JOIN unread u ON u.owner=? COLLATE NOCASE AND u.channel='dm:'||lower(a.username)
        WHERE a.username<>? COLLATE NOCASE ORDER BY a.username COLLATE NOCASE;
    )SQL");
    bindText(statement.value,1,owner); bindText(statement.value,2,owner);
    std::vector<StoredContact> result;
    while(sqlite3_step(statement.value)==SQLITE_ROW) result.push_back({columnText(statement.value,0),sqlite3_column_int(statement.value,1)});
    return result;
}

void Persistence::incrementRoomUnread(const std::string& roomId, const std::string& sender) {
    std::lock_guard<std::mutex> guard(mutex_);
    Statement statement(database_, R"SQL(
        INSERT INTO unread(owner,channel,count)
        SELECT username,'room:'||?,1 FROM accounts WHERE username<>? COLLATE NOCASE
        ON CONFLICT(owner,channel) DO UPDATE SET count=count+1;
    )SQL");
    bindText(statement.value,1,roomId); bindText(statement.value,2,sender); sqlite3_step(statement.value);
}

void Persistence::incrementDirectUnread(const std::string& recipient, const std::string& sender) {
    std::lock_guard<std::mutex> guard(mutex_);
    Statement statement(database_, R"SQL(
        INSERT INTO unread(owner,channel,count) VALUES(?,'dm:'||lower(?),1)
        ON CONFLICT(owner,channel) DO UPDATE SET count=count+1;
    )SQL");
    bindText(statement.value,1,recipient); bindText(statement.value,2,sender); sqlite3_step(statement.value);
}

void Persistence::clearUnread(const std::string& owner, const std::string& channel) {
    if(owner.empty()) return;
    std::lock_guard<std::mutex> guard(mutex_);
    Statement statement(database_,"DELETE FROM unread WHERE owner=? COLLATE NOCASE AND channel=?;");
    bindText(statement.value,1,owner); bindText(statement.value,2,channel); sqlite3_step(statement.value);
}

std::uint64_t Persistence::insertMessage(const StoredMessage& message) {
    std::lock_guard<std::mutex> guard(mutex_);
    Statement statement(database_,"INSERT INTO messages(kind,sender,text,display_time,room_id,recipient) VALUES(?,?,?,?,?,?);");
    bindText(statement.value,1,message.kind); bindText(statement.value,2,message.name); bindText(statement.value,3,message.text);
    bindText(statement.value,4,message.time); bindText(statement.value,5,message.roomId); bindText(statement.value,6,message.recipient);
    if(sqlite3_step(statement.value)!=SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(database_));
    return static_cast<std::uint64_t>(sqlite3_last_insert_rowid(database_));
}

std::vector<StoredMessage> Persistence::loadMessages(std::size_t limit) {
    std::lock_guard<std::mutex> guard(mutex_);
    Statement statement(database_,"SELECT id,kind,sender,text,display_time,room_id,recipient FROM messages ORDER BY id DESC LIMIT ?;");
    sqlite3_bind_int64(statement.value,1,static_cast<sqlite3_int64>(limit));
    std::vector<StoredMessage> result;
    while(sqlite3_step(statement.value)==SQLITE_ROW) result.push_back({static_cast<std::uint64_t>(sqlite3_column_int64(statement.value,0)),columnText(statement.value,1),columnText(statement.value,2),columnText(statement.value,3),columnText(statement.value,4),columnText(statement.value,5),columnText(statement.value,6)});
    std::reverse(result.begin(),result.end()); return result;
}

void Persistence::insertLookup(const StoredLookup& lookup) {
    std::lock_guard<std::mutex> guard(mutex_);
    Statement statement(database_,"INSERT INTO lookups(owner,query,result,display_time) VALUES(?,?,?,?);");
    bindText(statement.value,1,lookup.owner); bindText(statement.value,2,lookup.query); bindText(statement.value,3,lookup.result); bindText(statement.value,4,lookup.time); sqlite3_step(statement.value);
}

std::vector<StoredLookup> Persistence::loadLookups(std::size_t limit) {
    std::lock_guard<std::mutex> guard(mutex_);
    Statement statement(database_,"SELECT id,owner,query,result,display_time FROM lookups ORDER BY id DESC LIMIT ?;"); sqlite3_bind_int64(statement.value,1,static_cast<sqlite3_int64>(limit));
    std::vector<StoredLookup> result; while(sqlite3_step(statement.value)==SQLITE_ROW) result.push_back({static_cast<std::uint64_t>(sqlite3_column_int64(statement.value,0)),columnText(statement.value,1),columnText(statement.value,2),columnText(statement.value,3),columnText(statement.value,4)});
    std::reverse(result.begin(),result.end()); return result;
}

void Persistence::insertScore(const StoredScore& score) {
    std::lock_guard<std::mutex> guard(mutex_);
    Statement statement(database_,"INSERT INTO scores(name,score,display_time) VALUES(?,?,?);"); bindText(statement.value,1,score.name); sqlite3_bind_int(statement.value,2,score.score); bindText(statement.value,3,score.time); sqlite3_step(statement.value);
    exec("DELETE FROM scores WHERE id NOT IN (SELECT id FROM scores ORDER BY score DESC,id ASC LIMIT 50);");
}

std::vector<StoredScore> Persistence::loadScores(std::size_t limit) {
    std::lock_guard<std::mutex> guard(mutex_);
    Statement statement(database_,"SELECT id,name,score,display_time FROM scores ORDER BY score DESC,id ASC LIMIT ?;"); sqlite3_bind_int64(statement.value,1,static_cast<sqlite3_int64>(limit));
    std::vector<StoredScore> result; while(sqlite3_step(statement.value)==SQLITE_ROW) result.push_back({static_cast<std::uint64_t>(sqlite3_column_int64(statement.value,0)),columnText(statement.value,1),sqlite3_column_int(statement.value,2),columnText(statement.value,3)}); return result;
}
