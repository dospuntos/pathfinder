#include "GameDatabase.h"
#include <File.h>
#include <cstdio>
#include <Entry.h>

// SQL Schema - same as before but in C++ string
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
)";


GameDatabase::GameDatabase()
    :
    fDatabase(nullptr),
    fDatabasePath("")
{
    printf("GameDatabase constructed, fDatabase initialized to %p\n", fDatabase);
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
    printf("Close: Called, fDatabase=%p\n", fDatabase);

    if (fDatabase != nullptr) {
        printf("Close: Attempting to close database\n");
        int rc = sqlite3_close(fDatabase);

        if (rc != SQLITE_OK) {
            // Database might be busy, try to force close
            fprintf(stderr, "Warning: sqlite3_close failed with code %d, trying close_v2\n", rc);
            rc = sqlite3_close_v2(fDatabase);
            if (rc != SQLITE_OK) {
                fprintf(stderr, "Error: sqlite3_close_v2 also failed with code %d\n", rc);
            }
        }

        printf("Close: Setting fDatabase to nullptr\n");
        fDatabase = nullptr;
    }

    printf("Close: Clearing path\n");
    fDatabasePath = "";
    printf("Close: Completed\n");
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