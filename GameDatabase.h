#ifndef GAME_DATABASE_H
#define GAME_DATABASE_H

#include <String.h>
#include <sqlite3.h>
#include <vector>

// Structure to hold room data
struct Room {
    int id;
    BString name;
    BString description;
    BString imagePath;
    int northRoomId;
    int southRoomId;
    int eastRoomId;
    int westRoomId;
    int graphX;
    int graphY;

    Room() : id(0), northRoomId(-1), southRoomId(-1),
             eastRoomId(-1), westRoomId(-1), graphX(0), graphY(0) {}
};

// Structure to hold item data
struct Item {
    int id;
    BString name;
    BString description;
    BString roomDescription;
    BString imagePath;
    bool canTake;
    bool canUse;
    bool canCombine;
    BString useMessage;
    bool isVisible;

    Item() : id(0), canTake(true), canUse(false),
             canCombine(false), isVisible(true) {}
};

// Structure to hold game state
struct GameState {
    int currentRoomId;
    int score;
    int health;
    int movesCount;
    int startTime;

    GameState() : currentRoomId(1), score(0), health(100),
                  movesCount(0), startTime(0) {}
};

class GameDatabase {
public:
    GameDatabase();
    ~GameDatabase();

    status_t CreateNew(const char* path);
    status_t Open(const char* path);
    void Close();
    bool IsOpen() const { return fDatabase != nullptr; }
    const char* Path() const { return fDatabasePath.String(); }
    bool VerifySchema();

    // Query methods
    status_t GetRoom(int roomId, Room& room);
    status_t GetItemsInRoom(int roomId, std::vector<Item>& items);
    status_t GetInventoryItems(std::vector<Item>& items);
    status_t GetGameState(GameState& state);
	status_t UpdateGameState(const GameState& state);
	status_t MoveToRoom(int newRoomId);

    // Get the SQLite database handle (for future advanced queries)
    sqlite3* Handle() const { return fDatabase; }

private:
    sqlite3* fDatabase;
    BString fDatabasePath;

    status_t _ExecuteSQL(const char* sql);
    status_t _CreateSchema();

    // Populate with starter content (cave and mountain path)
    status_t _CreateStarterContent();
};

#endif // GAME_DATABASE_H