#include "GameDatabase.h"
#include <File.h>
#include <cstdio>
#include <Entry.h>

// SQL Schema
static const char* kSchemaSQL = R"(
CREATE TABLE rooms (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL,
    description TEXT NOT NULL,
    image_path TEXT,
    north_room_id INTEGER,
    south_room_id INTEGER,
    east_room_id INTEGER,
    west_room_id INTEGER,
    graph_x INTEGER DEFAULT 0,
    graph_y INTEGER DEFAULT 0,
    FOREIGN KEY (north_room_id) REFERENCES rooms(id) ON DELETE SET NULL,
    FOREIGN KEY (south_room_id) REFERENCES rooms(id) ON DELETE SET NULL,
    FOREIGN KEY (east_room_id) REFERENCES rooms(id) ON DELETE SET NULL,
    FOREIGN KEY (west_room_id) REFERENCES rooms(id) ON DELETE SET NULL
);

CREATE TABLE items (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL,
    description TEXT NOT NULL,
    room_description TEXT,
    image_path TEXT,
    can_take BOOLEAN DEFAULT 1,
    can_use BOOLEAN DEFAULT 0,
    can_combine BOOLEAN DEFAULT 0,
    use_message TEXT,
    is_visible BOOLEAN DEFAULT 1
);

CREATE TABLE item_locations (
    item_id INTEGER PRIMARY KEY,
    room_id INTEGER,
    FOREIGN KEY (item_id) REFERENCES items(id) ON DELETE CASCADE,
    FOREIGN KEY (room_id) REFERENCES rooms(id) ON DELETE CASCADE
);

CREATE TABLE item_locations_initial (
    item_id INTEGER PRIMARY KEY,
    room_id INTEGER,
    FOREIGN KEY (item_id) REFERENCES items(id) ON DELETE CASCADE,
    FOREIGN KEY (room_id) REFERENCES rooms(id) ON DELETE CASCADE
);

CREATE TABLE item_combinations (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    item1_id INTEGER NOT NULL,
    item2_id INTEGER NOT NULL,
    result_item_id INTEGER NOT NULL,
    success_message TEXT,
    FOREIGN KEY (item1_id) REFERENCES items(id) ON DELETE CASCADE,
    FOREIGN KEY (item2_id) REFERENCES items(id) ON DELETE CASCADE,
    FOREIGN KEY (result_item_id) REFERENCES items(id) ON DELETE CASCADE,
    UNIQUE(item1_id, item2_id)
);

CREATE TABLE item_actions (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    item_id INTEGER NOT NULL,
    room_id INTEGER,
    action_type TEXT NOT NULL,
    target_item_id INTEGER,
    target_direction TEXT,
    success_message TEXT,
    consumes_item BOOLEAN DEFAULT 0,
    FOREIGN KEY (item_id) REFERENCES items(id) ON DELETE CASCADE,
    FOREIGN KEY (room_id) REFERENCES rooms(id) ON DELETE CASCADE,
    FOREIGN KEY (target_item_id) REFERENCES items(id) ON DELETE CASCADE
);

CREATE TABLE exit_conditions (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    room_id INTEGER NOT NULL,
    direction TEXT NOT NULL,
    is_locked BOOLEAN DEFAULT 1,
    required_item_id INTEGER,
    locked_message TEXT DEFAULT 'The way is blocked.',
    FOREIGN KEY (room_id) REFERENCES rooms(id) ON DELETE CASCADE,
    FOREIGN KEY (required_item_id) REFERENCES items(id) ON DELETE SET NULL,
    UNIQUE(room_id, direction)
);

CREATE TABLE completed_actions (
    action_id INTEGER NOT NULL PRIMARY KEY,
    completed_at INTEGER DEFAULT (strftime('%s', 'now')),
    FOREIGN KEY (action_id) REFERENCES item_actions(id) ON DELETE CASCADE
);

CREATE TABLE removed_items (
    item_id INTEGER PRIMARY KEY,
    removed_at INTEGER DEFAULT (strftime('%s', 'now')),
    FOREIGN KEY (item_id) REFERENCES items(id) ON DELETE CASCADE
);

CREATE TABLE revealed_items (
    item_id INTEGER PRIMARY KEY,
    revealed_at INTEGER DEFAULT (strftime('%s', 'now')),
    FOREIGN KEY (item_id) REFERENCES items(id) ON DELETE CASCADE
);

CREATE TABLE unlocked_exits (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    room_id INTEGER NOT NULL,
    direction TEXT NOT NULL,
    unlocked_at INTEGER DEFAULT (strftime('%s', 'now')),
    FOREIGN KEY (room_id) REFERENCES rooms(id) ON DELETE CASCADE,
    UNIQUE(room_id, direction)
);

CREATE TABLE game_state (
    id INTEGER PRIMARY KEY CHECK (id = 1),
    current_room_id INTEGER NOT NULL,
    score INTEGER DEFAULT 0,
    health INTEGER DEFAULT 100,
    moves_count INTEGER DEFAULT 0,
    start_time INTEGER,
    FOREIGN KEY (current_room_id) REFERENCES rooms(id)
);

CREATE TABLE game_metadata (
    key TEXT PRIMARY KEY,
    value TEXT
);

INSERT INTO game_metadata (key, value) VALUES
    ('title', 'Untitled Adventure'),
    ('author', ''),
    ('version', '1.0'),
    ('starting_room_id', '1');

CREATE INDEX idx_item_locations_room ON item_locations(room_id);
CREATE INDEX idx_item_combinations_items ON item_combinations(item1_id, item2_id);
CREATE INDEX idx_item_actions_room ON item_actions(room_id);
CREATE INDEX idx_item_actions_item ON item_actions(item_id);
CREATE INDEX idx_exit_conditions_room ON exit_conditions(room_id);
CREATE INDEX idx_unlocked_exits_room ON unlocked_exits(room_id);
)";


GameDatabase::GameDatabase()
    :
    fDatabase(nullptr),
    fDatabasePath("")
{
}


GameDatabase::~GameDatabase()
{
    Close();
}


status_t
GameDatabase::CreateNew(const char* path)
{
    printf("CreateNew: Starting, path=%s\n", path ? path : "NULL");

    if (!path || strlen(path) == 0) {
        fprintf(stderr, "CreateNew: Invalid path\n");
        return B_BAD_VALUE;
    }

    // Close any existing database
    printf("CreateNew: Closing any existing database\n");
    Close();

    // Delete existing file if present
    printf("CreateNew: Checking for existing file\n");
    BEntry entry(path);
    if (entry.Exists()) {
        printf("CreateNew: Removing existing file\n");
        entry.Remove();
    }

    // Open new database
    printf("CreateNew: Opening new database\n");
    sqlite3* tempDb = nullptr;
    int rc = sqlite3_open(path, &tempDb);
    if (rc != SQLITE_OK) {
        const char* errMsg = tempDb ? sqlite3_errmsg(tempDb) : "unknown error";
        fprintf(stderr, "Cannot create database: %s\n", errMsg);
        if (tempDb) {
            printf("CreateNew: Closing failed database handle\n");
            sqlite3_close(tempDb);
        }
        return B_ERROR;
    }

    printf("CreateNew: Database opened, assigning to fDatabase\n");
    fDatabase = tempDb;
    fDatabasePath = path;

    // Create schema
    printf("CreateNew: Creating schema\n");
    status_t status = _CreateSchema();
    if (status != B_OK) {
        fprintf(stderr, "Failed to create schema\n");
        printf("CreateNew: Schema creation failed, closing database\n");
        Close();
        printf("CreateNew: Close completed after schema failure\n");
        return status;
    }

    // Add starter content
    printf("CreateNew: Creating starter content\n");
    status = _CreateStarterContent();
    if (status != B_OK) {
        fprintf(stderr, "Failed to create starter content\n");
        printf("CreateNew: Starter content failed, closing database\n");
        Close();
        printf("CreateNew: Close completed after starter content failure\n");
        return status;
    }

    printf("Database created successfully: %s\n", path);
    return B_OK;
}


status_t
GameDatabase::Open(const char* path)
{
    if (!path || strlen(path) == 0)
        return B_BAD_VALUE;

    // Check if file exists
    BEntry entry(path);
    if (!entry.Exists())
        return B_ENTRY_NOT_FOUND;

    // Close any existing database
    Close();

    // Open database
    sqlite3* tempDb = nullptr;
    int rc = sqlite3_open(path, &tempDb);
    if (rc != SQLITE_OK) {
        const char* errMsg = tempDb ? sqlite3_errmsg(tempDb) : "unknown error";
        fprintf(stderr, "Cannot open database: %s\n", errMsg);
        if (tempDb)
            sqlite3_close(tempDb);
        return B_ERROR;
    }

    fDatabase = tempDb;
    fDatabasePath = path;

    // Verify it has the correct schema
    if (!VerifySchema()) {
        fprintf(stderr, "Database schema verification failed\n");
        Close();
        return B_ERROR;
    }

    printf("Database opened successfully: %s\n", path);
    return B_OK;
}


void
GameDatabase::Close()
{
    if (fDatabase != nullptr) {
        int rc = sqlite3_close(fDatabase);

        if (rc != SQLITE_OK) {
            // Database might be busy, try to force close
            rc = sqlite3_close_v2(fDatabase);
            if (rc != SQLITE_OK) {
                fprintf(stderr, "Error: sqlite3_close_v2 also failed with code %d\n", rc);
            }
        }
        fDatabase = nullptr;
    }
    fDatabasePath = "";
}


bool
GameDatabase::VerifySchema()
{
    if (!fDatabase)
        return false;

    // Simple check: verify key tables exist
    const char* checkSQL =
        "SELECT name FROM sqlite_master WHERE type='table' AND name IN "
        "('rooms', 'items', 'game_state', 'game_metadata');";

    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(fDatabase, checkSQL, -1, &stmt, nullptr);
    if (rc != SQLITE_OK)
        return false;

    int tableCount = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        tableCount++;
    }
    sqlite3_finalize(stmt);

    // Should find all 4 core tables
    return tableCount == 4;
}


// Add to GameDatabase.cpp

status_t
GameDatabase::ClearGameState()
{
    if (!fDatabase)
        return B_NO_INIT;

    printf("Clearing game state...\n");

    // Clear all state tables
    const char* clearSQL = R"(
DELETE FROM completed_actions;
DELETE FROM removed_items;
DELETE FROM revealed_items;
DELETE FROM unlocked_exits;
DELETE FROM item_locations;

-- Restore initial item locations from backup table
INSERT INTO item_locations (item_id, room_id)
SELECT item_id, room_id FROM item_locations_initial;

-- Reset game state
UPDATE game_state SET
    current_room_id = (SELECT value FROM game_metadata WHERE key = 'starting_room_id'),
    score = 0,
    health = 100,
    moves_count = 0,
    start_time = strftime('%s', 'now')
WHERE id = 1;
)";

    status_t status = _ExecuteSQL(clearSQL);
    if (status == B_OK)
        printf("Game state cleared successfully\n");
    else
        fprintf(stderr, "Failed to clear game state\n");

    return status;
}


status_t
GameDatabase::_ExecuteSQL(const char* sql)
{
    if (!fDatabase) {
        fprintf(stderr, "SQL error: database not initialized\n");
        return B_NO_INIT;
    }

    if (!sql || strlen(sql) == 0) {
        fprintf(stderr, "SQL error: empty SQL statement\n");
        return B_BAD_VALUE;
    }

    char* errMsg = nullptr;
    int rc = sqlite3_exec(fDatabase, sql, nullptr, nullptr, &errMsg);

    if (rc != SQLITE_OK) {
        fprintf(stderr, "SQL error (code %d): %s\n", rc, errMsg ? errMsg : "unknown");
        if (errMsg)
            sqlite3_free(errMsg);
        return B_ERROR;
    }

    return B_OK;
}


status_t
GameDatabase::_CreateSchema()
{
    printf("_CreateSchema: Starting\n");

    // Enable foreign keys first
    status_t status = _ExecuteSQL("PRAGMA foreign_keys = ON;");
    if (status != B_OK) {
        fprintf(stderr, "_CreateSchema: Failed to enable foreign keys\n");
        return status;
    }

    printf("_CreateSchema: Executing main schema\n");
    status = _ExecuteSQL(kSchemaSQL);

    if (status == B_OK)
        printf("_CreateSchema: Success\n");
    else
        fprintf(stderr, "_CreateSchema: Failed\n");

    return status;
}


status_t
GameDatabase::_CreateStarterContent()
{
    printf("_CreateStarterContent: Starting\n");

    // Starter content: Cave and Mountain Path with a stone
    // NOTE: Insert rooms first without foreign key references, then update them
    const char* contentSQL = R"(
-- Room 1: Dark Cave (without exit reference initially)
INSERT INTO rooms (id, name, description, graph_x, graph_y) VALUES
    (1, 'Dark Cave', 'You are in a dark, damp cave. The walls glisten with moisture. A narrow passage leads south.', 0, 0);

-- Room 2: Mountain Path (without exit reference initially)
INSERT INTO rooms (id, name, description, graph_x, graph_y) VALUES
    (2, 'Mountain Path', 'You stand on a narrow mountain path. The cave entrance is to the north. Steep cliffs drop away on either side.', 0, 100);

-- Now update the exits to link rooms together
UPDATE rooms SET south_room_id = 2 WHERE id = 1;
UPDATE rooms SET north_room_id = 1 WHERE id = 2;

-- Item 1: Stone
INSERT INTO items (id, name, description, room_description, can_take, can_use) VALUES
    (1, 'Stone', 'A smooth, palm-sized stone.', 'A smooth stone lies on the ground.', 1, 0);

-- Item 2: Stick
INSERT INTO items (id, name, description, room_description, can_take, can_use) VALUES
    (2, 'Stick', 'A sturdy wooden stick, good for poking things.', 'A wooden stick rests against a rock.', 1, 0);

-- Place items in Mountain Path
INSERT INTO item_locations (item_id, room_id) VALUES (1, 2);
INSERT INTO item_locations (item_id, room_id) VALUES (2, 2);

-- Store initial locations for game reset
INSERT INTO item_locations_initial (item_id, room_id) VALUES (1, 2);
INSERT INTO item_locations_initial (item_id, room_id) VALUES (2, 2);

-- Initialize game state (start in cave)
INSERT INTO game_state (id, current_room_id, start_time) VALUES
    (1, 1, strftime('%s', 'now'));

-- Update metadata
UPDATE game_metadata SET value = 'Cave Adventure' WHERE key = 'title';
UPDATE game_metadata SET value = 'Pathfinder' WHERE key = 'author';
)";

    printf("_CreateStarterContent: Executing content SQL\n");
    status_t status = _ExecuteSQL(contentSQL);

    if (status == B_OK)
        printf("_CreateStarterContent: Success\n");
    else
        fprintf(stderr, "_CreateStarterContent: Failed\n");

    return status;
}


status_t
GameDatabase::GetRoom(int roomId, Room& room)
{
    if (!fDatabase)
        return B_NO_INIT;

    const char* sql = "SELECT id, name, description, image_path, "
                      "north_room_id, south_room_id, east_room_id, west_room_id, "
                      "graph_x, graph_y FROM rooms WHERE id = ?;";

    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(fDatabase, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to prepare room query: %s\n", sqlite3_errmsg(fDatabase));
        return B_ERROR;
    }

    sqlite3_bind_int(stmt, 1, roomId);

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        room.id = sqlite3_column_int(stmt, 0);
        room.name = (const char*)sqlite3_column_text(stmt, 1);
        room.description = (const char*)sqlite3_column_text(stmt, 2);

        if (sqlite3_column_type(stmt, 3) != SQLITE_NULL)
            room.imagePath = (const char*)sqlite3_column_text(stmt, 3);

        room.northRoomId = (sqlite3_column_type(stmt, 4) == SQLITE_NULL)
            ? -1 : sqlite3_column_int(stmt, 4);
        room.southRoomId = (sqlite3_column_type(stmt, 5) == SQLITE_NULL)
            ? -1 : sqlite3_column_int(stmt, 5);
        room.eastRoomId = (sqlite3_column_type(stmt, 6) == SQLITE_NULL)
            ? -1 : sqlite3_column_int(stmt, 6);
        room.westRoomId = (sqlite3_column_type(stmt, 7) == SQLITE_NULL)
            ? -1 : sqlite3_column_int(stmt, 7);

        room.graphX = sqlite3_column_int(stmt, 8);
        room.graphY = sqlite3_column_int(stmt, 9);

        sqlite3_finalize(stmt);
        return B_OK;
    }

    sqlite3_finalize(stmt);
    return B_ENTRY_NOT_FOUND;
}


status_t
GameDatabase::GetItemsInRoom(int roomId, std::vector<Item>& items)
{
    if (!fDatabase)
        return B_NO_INIT;

    items.clear();

    const char* sql = "SELECT i.id, i.name, i.description, i.room_description, "
                  "i.image_path, i.can_take, i.can_use, i.can_combine, "
                  "i.use_message, i.is_visible "
                  "FROM items i "
                  "JOIN item_locations il ON i.id = il.item_id "
                  "WHERE il.room_id = ? "
                  "AND i.id NOT IN (SELECT item_id FROM removed_items) "
                  "AND (i.is_visible = 1 OR i.id IN (SELECT item_id FROM revealed_items));";

    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(fDatabase, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to prepare items query: %s\n", sqlite3_errmsg(fDatabase));
        return B_ERROR;
    }

    sqlite3_bind_int(stmt, 1, roomId);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Item item;
        item.id = sqlite3_column_int(stmt, 0);
        item.name = (const char*)sqlite3_column_text(stmt, 1);
        item.description = (const char*)sqlite3_column_text(stmt, 2);

        if (sqlite3_column_type(stmt, 3) != SQLITE_NULL)
            item.roomDescription = (const char*)sqlite3_column_text(stmt, 3);
        if (sqlite3_column_type(stmt, 4) != SQLITE_NULL)
            item.imagePath = (const char*)sqlite3_column_text(stmt, 4);

        item.canTake = sqlite3_column_int(stmt, 5) != 0;
        item.canUse = sqlite3_column_int(stmt, 6) != 0;
        item.canCombine = sqlite3_column_int(stmt, 7) != 0;

        if (sqlite3_column_type(stmt, 8) != SQLITE_NULL)
            item.useMessage = (const char*)sqlite3_column_text(stmt, 8);

        item.isVisible = sqlite3_column_int(stmt, 9) != 0;

        items.push_back(item);
    }

    sqlite3_finalize(stmt);
    return B_OK;
}


status_t
GameDatabase::GetInventoryItems(std::vector<Item>& items)
{
    if (!fDatabase)
        return B_NO_INIT;

    items.clear();

    const char* sql = "SELECT i.id, i.name, i.description, i.room_description, "
                  "i.image_path, i.can_take, i.can_use, i.can_combine, "
                  "i.use_message, i.is_visible "
                  "FROM items i "
                  "JOIN item_locations il ON i.id = il.item_id "
                  "WHERE il.room_id IS NULL "
                  "AND i.id NOT IN (SELECT item_id FROM removed_items);";

    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(fDatabase, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to prepare inventory query: %s\n", sqlite3_errmsg(fDatabase));
        return B_ERROR;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Item item;
        item.id = sqlite3_column_int(stmt, 0);
        item.name = (const char*)sqlite3_column_text(stmt, 1);
        item.description = (const char*)sqlite3_column_text(stmt, 2);

        if (sqlite3_column_type(stmt, 3) != SQLITE_NULL)
            item.roomDescription = (const char*)sqlite3_column_text(stmt, 3);
        if (sqlite3_column_type(stmt, 4) != SQLITE_NULL)
            item.imagePath = (const char*)sqlite3_column_text(stmt, 4);

        item.canTake = sqlite3_column_int(stmt, 5) != 0;
        item.canUse = sqlite3_column_int(stmt, 6) != 0;
        item.canCombine = sqlite3_column_int(stmt, 7) != 0;

        if (sqlite3_column_type(stmt, 8) != SQLITE_NULL)
            item.useMessage = (const char*)sqlite3_column_text(stmt, 8);

        item.isVisible = sqlite3_column_int(stmt, 9) != 0;

        items.push_back(item);
    }

    sqlite3_finalize(stmt);
    return B_OK;
}


status_t
GameDatabase::GetGameState(GameState& state)
{
    if (!fDatabase)
        return B_NO_INIT;

    const char* sql = "SELECT current_room_id, score, health, moves_count, start_time "
                      "FROM game_state WHERE id = 1;";

    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(fDatabase, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to prepare game state query: %s\n", sqlite3_errmsg(fDatabase));
        return B_ERROR;
    }

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        state.currentRoomId = sqlite3_column_int(stmt, 0);
        state.score = sqlite3_column_int(stmt, 1);
        state.health = sqlite3_column_int(stmt, 2);
        state.movesCount = sqlite3_column_int(stmt, 3);
        state.startTime = sqlite3_column_int(stmt, 4);

        sqlite3_finalize(stmt);
        return B_OK;
    }

    sqlite3_finalize(stmt);
    return B_ENTRY_NOT_FOUND;
}


status_t
GameDatabase::UpdateGameState(const GameState& state)
{
    if (!fDatabase)
        return B_NO_INIT;

    const char* sql = "UPDATE game_state SET current_room_id = ?, score = ?, "
                      "health = ?, moves_count = ? WHERE id = 1;";

    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(fDatabase, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to prepare update statement: %s\n", sqlite3_errmsg(fDatabase));
        return B_ERROR;
    }

    sqlite3_bind_int(stmt, 1, state.currentRoomId);
    sqlite3_bind_int(stmt, 2, state.score);
    sqlite3_bind_int(stmt, 3, state.health);
    sqlite3_bind_int(stmt, 4, state.movesCount);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        fprintf(stderr, "Failed to update game state: %s\n", sqlite3_errmsg(fDatabase));
        return B_ERROR;
    }

    return B_OK;
}


status_t
GameDatabase::MoveToRoom(int newRoomId)
{
    if (!fDatabase)
        return B_NO_INIT;

    // Simple version: just update room and increment moves
    const char* sql = "UPDATE game_state SET current_room_id = ?, "
                      "moves_count = moves_count + 1 WHERE id = 1;";

    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(fDatabase, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to prepare move statement: %s\n", sqlite3_errmsg(fDatabase));
        return B_ERROR;
    }

    sqlite3_bind_int(stmt, 1, newRoomId);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        fprintf(stderr, "Failed to move to room: %s\n", sqlite3_errmsg(fDatabase));
        return B_ERROR;
    }

    return B_OK;
}


status_t
GameDatabase::MoveItemToInventory(int itemId)
{
    if (!fDatabase)
        return B_NO_INIT;

    // Move item to inventory by setting room_id to NULL
    const char* sql = "UPDATE item_locations SET room_id = NULL WHERE item_id = ?;";

    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(fDatabase, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to prepare take item statement: %s\n",
                sqlite3_errmsg(fDatabase));
        return B_ERROR;
    }

    sqlite3_bind_int(stmt, 1, itemId);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        fprintf(stderr, "Failed to take item: %s\n", sqlite3_errmsg(fDatabase));
        return B_ERROR;
    }

    return B_OK;
}


status_t
GameDatabase::MoveItemToRoom(int itemId, int roomId)
{
	if (!fDatabase)
		return B_NO_INIT;

	// Move item to current room by updating room_id
	const char* sql = "UPDATE item_locations SET room_id = ? WHERE item_id = ?;";

	sqlite3_stmt* stmt;
	int rc = sqlite3_prepare_v2(fDatabase, sql, -1, &stmt, nullptr);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "Failed to prepare drop item statement: %s\n",
				sqlite3_errmsg(fDatabase));
		return B_ERROR;
	}

	sqlite3_bind_int(stmt, 1, roomId);
	sqlite3_bind_int(stmt, 2, itemId);

	rc = sqlite3_step(stmt);
	sqlite3_finalize(stmt);

	if (rc != SQLITE_DONE) {
		fprintf(stderr, "Failed to drop item: %s\n", sqlite3_errmsg(fDatabase));
		return B_ERROR;
	}

	return B_OK;
}


status_t
GameDatabase::GetItemActions(int itemId, int roomId, std::vector<ItemAction>& actions)
{
    if (!fDatabase)
        return B_NO_INIT;

    actions.clear();

    // Get actions that work in this specific room OR any room (room_id IS NULL)
    const char* sql = "SELECT id, item_id, room_id, action_type, target_item_id, "
                      "target_direction, success_message, consumes_item "
                      "FROM item_actions "
                      "WHERE item_id = ? AND (room_id = ? OR room_id IS NULL);";

    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(fDatabase, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to prepare item actions query: %s\n",
                sqlite3_errmsg(fDatabase));
        return B_ERROR;
    }

    sqlite3_bind_int(stmt, 1, itemId);
    sqlite3_bind_int(stmt, 2, roomId);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        ItemAction action;
        action.id = sqlite3_column_int(stmt, 0);
        action.itemId = sqlite3_column_int(stmt, 1);

        if (sqlite3_column_type(stmt, 2) != SQLITE_NULL)
            action.roomId = sqlite3_column_int(stmt, 2);

        action.actionType = (const char*)sqlite3_column_text(stmt, 3);

        if (sqlite3_column_type(stmt, 4) != SQLITE_NULL)
            action.targetItemId = sqlite3_column_int(stmt, 4);

        if (sqlite3_column_type(stmt, 5) != SQLITE_NULL)
            action.targetDirection = (const char*)sqlite3_column_text(stmt, 5);

        if (sqlite3_column_type(stmt, 6) != SQLITE_NULL)
            action.successMessage = (const char*)sqlite3_column_text(stmt, 6);

        action.consumesItem = sqlite3_column_int(stmt, 7) != 0;

        actions.push_back(action);
    }

    sqlite3_finalize(stmt);
    return B_OK;
}


status_t
GameDatabase::MarkActionCompleted(int actionId)
{
    if (!fDatabase)
        return B_NO_INIT;

    const char* sql = "INSERT OR IGNORE INTO completed_actions (action_id) VALUES (?);";

    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(fDatabase, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to prepare mark action statement: %s\n",
                sqlite3_errmsg(fDatabase));
        return B_ERROR;
    }

    sqlite3_bind_int(stmt, 1, actionId);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        fprintf(stderr, "Failed to mark action completed: %s\n",
                sqlite3_errmsg(fDatabase));
        return B_ERROR;
    }

    return B_OK;
}


bool
GameDatabase::IsActionCompleted(int actionId)
{
    if (!fDatabase)
        return false;

    const char* sql = "SELECT 1 FROM completed_actions WHERE action_id = ?;";

    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(fDatabase, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK)
        return false;

    sqlite3_bind_int(stmt, 1, actionId);

    bool completed = (sqlite3_step(stmt) == SQLITE_ROW);
    sqlite3_finalize(stmt);

    return completed;
}


status_t
GameDatabase::SetItemVisibility(int itemId, bool visible)
{
    if (!fDatabase)
        return B_NO_INIT;

    if (visible) {
        // Add to revealed_items (making it visible)
        const char* sql = "INSERT OR IGNORE INTO revealed_items (item_id) VALUES (?);";

        sqlite3_stmt* stmt;
        int rc = sqlite3_prepare_v2(fDatabase, sql, -1, &stmt, nullptr);
        if (rc != SQLITE_OK) {
            fprintf(stderr, "Failed to prepare reveal item statement: %s\n",
                    sqlite3_errmsg(fDatabase));
            return B_ERROR;
        }

        sqlite3_bind_int(stmt, 1, itemId);

        rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        if (rc != SQLITE_DONE) {
            fprintf(stderr, "Failed to reveal item: %s\n", sqlite3_errmsg(fDatabase));
            return B_ERROR;
        }
    } else {
        // Remove from revealed_items (hide it again - rarely used)
        const char* sql = "DELETE FROM revealed_items WHERE item_id = ?;";

        sqlite3_stmt* stmt;
        int rc = sqlite3_prepare_v2(fDatabase, sql, -1, &stmt, nullptr);
        if (rc != SQLITE_OK) {
            fprintf(stderr, "Failed to prepare hide item statement: %s\n",
                    sqlite3_errmsg(fDatabase));
            return B_ERROR;
        }

        sqlite3_bind_int(stmt, 1, itemId);

        rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        if (rc != SQLITE_DONE) {
            fprintf(stderr, "Failed to hide item: %s\n", sqlite3_errmsg(fDatabase));
            return B_ERROR;
        }
    }

    return B_OK;
}


status_t
GameDatabase::RemoveItemFromRoom(int itemId)
{
    if (!fDatabase)
        return B_NO_INIT;

    // Add to removed_items instead of deleting from item_locations
    const char* sql = "INSERT OR IGNORE INTO removed_items (item_id) VALUES (?);";

    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(fDatabase, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to prepare remove item statement: %s\n",
                sqlite3_errmsg(fDatabase));
        return B_ERROR;
    }

    sqlite3_bind_int(stmt, 1, itemId);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        fprintf(stderr, "Failed to remove item: %s\n", sqlite3_errmsg(fDatabase));
        return B_ERROR;
    }

    return B_OK;
}


status_t
GameDatabase::UnlockExit(int roomId, const char* direction)
{
    if (!fDatabase)
        return B_NO_INIT;

    // Add to unlocked_exits instead of deleting from exit_conditions
    const char* sql = "INSERT OR IGNORE INTO unlocked_exits (room_id, direction) VALUES (?, ?);";

    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(fDatabase, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to prepare unlock exit statement: %s\n",
                sqlite3_errmsg(fDatabase));
        return B_ERROR;
    }

    sqlite3_bind_int(stmt, 1, roomId);
    sqlite3_bind_text(stmt, 2, direction, -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        fprintf(stderr, "Failed to unlock exit: %s\n", sqlite3_errmsg(fDatabase));
        return B_ERROR;
    }

    return B_OK;
}


bool
GameDatabase::IsExitLocked(int roomId, const char* direction)
{
    if (!fDatabase)
        return false;

    // Check if there's an exit condition AND it hasn't been unlocked
    const char* sql = "SELECT ec.is_locked, ec.locked_message "
                      "FROM exit_conditions ec "
                      "WHERE ec.room_id = ? AND ec.direction = ? "
                      "AND NOT EXISTS ("
                      "    SELECT 1 FROM unlocked_exits ue "
                      "    WHERE ue.room_id = ec.room_id AND ue.direction = ec.direction"
                      ");";

    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(fDatabase, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK)
        return false;

    sqlite3_bind_int(stmt, 1, roomId);
    sqlite3_bind_text(stmt, 2, direction, -1, SQLITE_TRANSIENT);

    bool locked = (sqlite3_step(stmt) == SQLITE_ROW);
    sqlite3_finalize(stmt);

    return locked;
}


// Method to save item locations after edit mode, for game reset
status_t
GameDatabase::SaveCurrentStateAsInitial()
{
    if (!fDatabase)
        return B_NO_INIT;

    // Copy current item_locations to item_locations_initial
    const char* sql = R"(
DELETE FROM item_locations_initial;
INSERT INTO item_locations_initial (item_id, room_id)
SELECT item_id, room_id FROM item_locations;
)";

    return _ExecuteSQL(sql);
}