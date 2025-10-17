#include "GameEditor.h"
#include <cstdio>

GameEditor::GameEditor(GameDatabase* database)
    :
    fDatabase(database)
{
}


GameEditor::~GameEditor()
{
    // Don't delete fDatabase - we don't own it
}


status_t
GameEditor::CreateRoom(const BString& name, const BString& description,
                      int graphX, int graphY, int& newRoomId)
{
    if (!IsReady())
        return B_NO_INIT;

    const char* sql = "INSERT INTO rooms (name, description, graph_x, graph_y) "
                      "VALUES (?, ?, ?, ?);";

    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(fDatabase->Handle(), sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to prepare create room statement: %s\n",
                sqlite3_errmsg(fDatabase->Handle()));
        return B_ERROR;
    }

    sqlite3_bind_text(stmt, 1, name.String(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, description.String(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, graphX);
    sqlite3_bind_int(stmt, 4, graphY);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        fprintf(stderr, "Failed to create room: %s\n",
                sqlite3_errmsg(fDatabase->Handle()));
        return B_ERROR;
    }

    // Get the ID of the newly created room
    newRoomId = sqlite3_last_insert_rowid(fDatabase->Handle());
    printf("Created room %d: %s\n", newRoomId, name.String());

    return B_OK;
}


status_t
GameEditor::UpdateRoom(int roomId, const BString& name, const BString& description)
{
    if (!IsReady())
        return B_NO_INIT;

    const char* sql = "UPDATE rooms SET name = ?, description = ? WHERE id = ?;";

    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(fDatabase->Handle(), sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to prepare update room statement: %s\n",
                sqlite3_errmsg(fDatabase->Handle()));
        return B_ERROR;
    }

    sqlite3_bind_text(stmt, 1, name.String(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, description.String(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, roomId);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        fprintf(stderr, "Failed to update room: %s\n",
                sqlite3_errmsg(fDatabase->Handle()));
        return B_ERROR;
    }

    printf("Updated room %d\n", roomId);
    return B_OK;
}


status_t
GameEditor::DeleteRoom(int roomId)
{
    if (!IsReady())
        return B_NO_INIT;

    printf("Deleting room %d\n", roomId);

    // Use a transaction for safety
    sqlite3_exec(fDatabase->Handle(), "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);

    // Step 1: If this is the current room, move to another room first
    const char* checkCurrentSQL = "SELECT current_room_id FROM game_state WHERE id = 1;";
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(fDatabase->Handle(), checkCurrentSQL, -1, &stmt, nullptr);
    if (rc == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            int currentRoomId = sqlite3_column_int(stmt, 0);
            sqlite3_finalize(stmt);

            if (currentRoomId == roomId) {
                // Need to move to a different room
                const char* findOtherSQL = "SELECT id FROM rooms WHERE id != ? LIMIT 1;";
                rc = sqlite3_prepare_v2(fDatabase->Handle(), findOtherSQL, -1, &stmt, nullptr);
                if (rc == SQLITE_OK) {
                    sqlite3_bind_int(stmt, 1, roomId);
                    if (sqlite3_step(stmt) == SQLITE_ROW) {
                        int otherRoomId = sqlite3_column_int(stmt, 0);
                        sqlite3_finalize(stmt);

                        // Update game state to point to other room
                        const char* updateStateSQL =
                            "UPDATE game_state SET current_room_id = ? WHERE id = 1;";
                        rc = sqlite3_prepare_v2(fDatabase->Handle(), updateStateSQL,
                                               -1, &stmt, nullptr);
                        if (rc == SQLITE_OK) {
                            sqlite3_bind_int(stmt, 1, otherRoomId);
                            rc = sqlite3_step(stmt);
                            sqlite3_finalize(stmt);

                            if (rc != SQLITE_DONE) {
                                sqlite3_exec(fDatabase->Handle(), "ROLLBACK;",
                                           nullptr, nullptr, nullptr);
                                fprintf(stderr, "Failed to update game state\n");
                                return B_ERROR;
                            }
                        }
                    } else {
                        // No other rooms - can't delete the last room
                        sqlite3_finalize(stmt);
                        sqlite3_exec(fDatabase->Handle(), "ROLLBACK;",
                                   nullptr, nullptr, nullptr);
                        fprintf(stderr, "Cannot delete the last room\n");
                        return B_ERROR;
                    }
                }
            }
        } else {
            sqlite3_finalize(stmt);
        }
    }

    // Step 2: Clear THIS room's connections to other rooms
    // const char* clearOwnSQL =
        // "UPDATE rooms SET north_room_id = NULL, south_room_id = NULL, "
        // "east_room_id = NULL, west_room_id = NULL WHERE id = ?;";
    //
    // rc = sqlite3_prepare_v2(fDatabase->Handle(), clearOwnSQL, -1, &stmt, nullptr);
    // if (rc != SQLITE_OK) {
        // sqlite3_exec(fDatabase->Handle(), "ROLLBACK;", nullptr, nullptr, nullptr);
        // fprintf(stderr, "Failed to prepare clear own connections: %s\n",
                // sqlite3_errmsg(fDatabase->Handle()));
        // return B_ERROR;
    // }
    //
    // sqlite3_bind_int(stmt, 1, roomId);
    // rc = sqlite3_step(stmt);
    // sqlite3_finalize(stmt);
    //
    // if (rc != SQLITE_DONE) {
        // sqlite3_exec(fDatabase->Handle(), "ROLLBACK;", nullptr, nullptr, nullptr);
        // fprintf(stderr, "Failed to clear own connections: %s\n",
                // sqlite3_errmsg(fDatabase->Handle()));
        // return B_ERROR;
    // }

    // Step 3: Clear OTHER rooms' connections TO this room
    const char* updateSQL[] = {
        "UPDATE rooms SET north_room_id = NULL WHERE north_room_id = ?;",
        "UPDATE rooms SET south_room_id = NULL WHERE south_room_id = ?;",
        "UPDATE rooms SET east_room_id = NULL WHERE east_room_id = ?;",
        "UPDATE rooms SET west_room_id = NULL WHERE west_room_id = ?;"
    };

    for (int i = 0; i < 4; i++) {
        rc = sqlite3_prepare_v2(fDatabase->Handle(), updateSQL[i], -1, &stmt, nullptr);
        if (rc != SQLITE_OK) {
            sqlite3_exec(fDatabase->Handle(), "ROLLBACK;", nullptr, nullptr, nullptr);
            fprintf(stderr, "Failed to prepare disconnect statement: %s\n",
                    sqlite3_errmsg(fDatabase->Handle()));
            return B_ERROR;
        }

        sqlite3_bind_int(stmt, 1, roomId);
        rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        if (rc != SQLITE_DONE) {
            sqlite3_exec(fDatabase->Handle(), "ROLLBACK;", nullptr, nullptr, nullptr);
            fprintf(stderr, "Failed to disconnect rooms: %s\n",
                    sqlite3_errmsg(fDatabase->Handle()));
            return B_ERROR;
        }
    }

    // Step 4: Now delete the room
    const char* deleteSQL = "DELETE FROM rooms WHERE id = ?;";
    rc = sqlite3_prepare_v2(fDatabase->Handle(), deleteSQL, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        sqlite3_exec(fDatabase->Handle(), "ROLLBACK;", nullptr, nullptr, nullptr);
        fprintf(stderr, "Failed to prepare delete room statement: %s\n",
                sqlite3_errmsg(fDatabase->Handle()));
        return B_ERROR;
    }

    sqlite3_bind_int(stmt, 1, roomId);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        sqlite3_exec(fDatabase->Handle(), "ROLLBACK;", nullptr, nullptr, nullptr);
        fprintf(stderr, "Failed to delete room: %s\n",
                sqlite3_errmsg(fDatabase->Handle()));
        return B_ERROR;
    }

    // Commit the transaction
    sqlite3_exec(fDatabase->Handle(), "COMMIT;", nullptr, nullptr, nullptr);

    printf("Deleted room %d\n", roomId);
    return B_OK;
}


status_t
GameEditor::ConnectRooms(int roomId, const char* direction, int targetRoomId)
{
    if (!IsReady())
        return B_NO_INIT;

    // Determine which column to update based on direction
    BString columnName;
    if (strcmp(direction, "north") == 0)
        columnName = "north_room_id";
    else if (strcmp(direction, "south") == 0)
        columnName = "south_room_id";
    else if (strcmp(direction, "east") == 0)
        columnName = "east_room_id";
    else if (strcmp(direction, "west") == 0)
        columnName = "west_room_id";
    else
        return B_BAD_VALUE;

    BString sql;
    sql << "UPDATE rooms SET " << columnName << " = ? WHERE id = ?;";

    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(fDatabase->Handle(), sql.String(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to prepare connect rooms statement: %s\n",
                sqlite3_errmsg(fDatabase->Handle()));
        return B_ERROR;
    }

    sqlite3_bind_int(stmt, 1, targetRoomId);
    sqlite3_bind_int(stmt, 2, roomId);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        fprintf(stderr, "Failed to connect rooms: %s\n",
                sqlite3_errmsg(fDatabase->Handle()));
        return B_ERROR;
    }

    printf("Connected room %d %s to room %d\n", roomId, direction, targetRoomId);
    return B_OK;
}


status_t
GameEditor::DisconnectRoom(int roomId, const char* direction)
{
    if (!IsReady())
        return B_NO_INIT;

    // Set the direction to NULL
    BString columnName;
    if (strcmp(direction, "north") == 0)
        columnName = "north_room_id";
    else if (strcmp(direction, "south") == 0)
        columnName = "south_room_id";
    else if (strcmp(direction, "east") == 0)
        columnName = "east_room_id";
    else if (strcmp(direction, "west") == 0)
        columnName = "west_room_id";
    else
        return B_BAD_VALUE;

    BString sql;
    sql << "UPDATE rooms SET " << columnName << " = NULL WHERE id = ?;";

    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(fDatabase->Handle(), sql.String(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to prepare disconnect room statement: %s\n",
                sqlite3_errmsg(fDatabase->Handle()));
        return B_ERROR;
    }

    sqlite3_bind_int(stmt, 1, roomId);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        fprintf(stderr, "Failed to disconnect room: %s\n",
                sqlite3_errmsg(fDatabase->Handle()));
        return B_ERROR;
    }

    printf("Disconnected room %d %s\n", roomId, direction);
    return B_OK;
}


status_t
GameEditor::CreateItem(const BString& name, const BString& description,
                      const BString& roomDescription, bool canTake,
                      bool canUse, int& newItemId)
{
    if (!IsReady())
        return B_NO_INIT;

    const char* sql = "INSERT INTO items (name, description, room_description, "
                      "can_take, can_use) VALUES (?, ?, ?, ?, ?);";

    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(fDatabase->Handle(), sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to prepare create item statement: %s\n",
                sqlite3_errmsg(fDatabase->Handle()));
        return B_ERROR;
    }

    sqlite3_bind_text(stmt, 1, name.String(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, description.String(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, roomDescription.String(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 4, canTake ? 1 : 0);
    sqlite3_bind_int(stmt, 5, canUse ? 1 : 0);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        fprintf(stderr, "Failed to create item: %s\n",
                sqlite3_errmsg(fDatabase->Handle()));
        return B_ERROR;
    }

    newItemId = sqlite3_last_insert_rowid(fDatabase->Handle());
    printf("Created item %d: %s\n", newItemId, name.String());

    return B_OK;
}


status_t
GameEditor::UpdateItem(int itemId, const BString& name,
                      const BString& description,
                      const BString& roomDescription)
{
    if (!IsReady())
        return B_NO_INIT;

    const char* sql = "UPDATE items SET name = ?, description = ?, "
                      "room_description = ? WHERE id = ?;";

    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(fDatabase->Handle(), sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to prepare update item statement: %s\n",
                sqlite3_errmsg(fDatabase->Handle()));
        return B_ERROR;
    }

    sqlite3_bind_text(stmt, 1, name.String(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, description.String(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, roomDescription.String(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 4, itemId);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        fprintf(stderr, "Failed to update item: %s\n",
                sqlite3_errmsg(fDatabase->Handle()));
        return B_ERROR;
    }

    printf("Updated item %d\n", itemId);
    return B_OK;
}


status_t
GameEditor::DeleteItem(int itemId)
{
    if (!IsReady())
        return B_NO_INIT;

    // Delete from items (CASCADE will handle item_locations)
    const char* sql = "DELETE FROM items WHERE id = ?;";

    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(fDatabase->Handle(), sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to prepare delete item statement: %s\n",
                sqlite3_errmsg(fDatabase->Handle()));
        return B_ERROR;
    }

    sqlite3_bind_int(stmt, 1, itemId);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        fprintf(stderr, "Failed to delete item: %s\n",
                sqlite3_errmsg(fDatabase->Handle()));
        return B_ERROR;
    }

    printf("Deleted item %d\n", itemId);
    return B_OK;
}


status_t
GameEditor::PlaceItem(int itemId, int roomId)
{
    if (!IsReady())
        return B_NO_INIT;

    // Insert or update item location
    const char* sql = "INSERT OR REPLACE INTO item_locations (item_id, room_id) "
                      "VALUES (?, ?);";

    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(fDatabase->Handle(), sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to prepare place item statement: %s\n",
                sqlite3_errmsg(fDatabase->Handle()));
        return B_ERROR;
    }

    sqlite3_bind_int(stmt, 1, itemId);
    sqlite3_bind_int(stmt, 2, roomId);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        fprintf(stderr, "Failed to place item: %s\n",
                sqlite3_errmsg(fDatabase->Handle()));
        return B_ERROR;
    }

    printf("Placed item %d in room %d\n", itemId, roomId);
    return B_OK;
}


status_t
GameEditor::CreateItemAction(int itemId, int roomId,
                            const BString& actionType,
                            int targetItemId, const BString& targetDirection,
                            const BString& successMessage, bool consumesItem,
                            int& newActionId)
{
    if (!IsReady())
        return B_NO_INIT;

    const char* sql = "INSERT INTO item_actions (item_id, room_id, action_type, "
                      "target_item_id, target_direction, success_message, consumes_item) "
                      "VALUES (?, ?, ?, ?, ?, ?, ?);";

    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(fDatabase->Handle(), sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to prepare create action statement: %s\n",
                sqlite3_errmsg(fDatabase->Handle()));
        return B_ERROR;
    }

    sqlite3_bind_int(stmt, 1, itemId);

    if (roomId > 0)
        sqlite3_bind_int(stmt, 2, roomId);
    else
        sqlite3_bind_null(stmt, 2);

    sqlite3_bind_text(stmt, 3, actionType.String(), -1, SQLITE_TRANSIENT);

    if (targetItemId > 0)
        sqlite3_bind_int(stmt, 4, targetItemId);
    else
        sqlite3_bind_null(stmt, 4);

    if (targetDirection.Length() > 0)
        sqlite3_bind_text(stmt, 5, targetDirection.String(), -1, SQLITE_TRANSIENT);
    else
        sqlite3_bind_null(stmt, 5);

    if (successMessage.Length() > 0)
        sqlite3_bind_text(stmt, 6, successMessage.String(), -1, SQLITE_TRANSIENT);
    else
        sqlite3_bind_null(stmt, 6);

    sqlite3_bind_int(stmt, 7, consumesItem ? 1 : 0);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        fprintf(stderr, "Failed to create item action: %s\n",
                sqlite3_errmsg(fDatabase->Handle()));
        return B_ERROR;
    }

    newActionId = sqlite3_last_insert_rowid(fDatabase->Handle());
    printf("Created item action %d\n", newActionId);

    return B_OK;
}


status_t
GameEditor::DeleteItemAction(int actionId)
{
    if (!IsReady())
        return B_NO_INIT;

    const char* sql = "DELETE FROM item_actions WHERE id = ?;";

    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(fDatabase->Handle(), sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to prepare delete action statement: %s\n",
                sqlite3_errmsg(fDatabase->Handle()));
        return B_ERROR;
    }

    sqlite3_bind_int(stmt, 1, actionId);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        fprintf(stderr, "Failed to delete item action: %s\n",
                sqlite3_errmsg(fDatabase->Handle()));
        return B_ERROR;
    }

    printf("Deleted item action %d\n", actionId);
    return B_OK;
}


status_t
GameEditor::CreateExitCondition(int roomId, const BString& direction,
                               const BString& lockedMessage,
                               int requiredItemId)
{
    if (!IsReady())
        return B_NO_INIT;

    const char* sql = "INSERT INTO exit_conditions (room_id, direction, "
                      "is_locked, locked_message, required_item_id) "
                      "VALUES (?, ?, 1, ?, ?);";

    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(fDatabase->Handle(), sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to prepare create exit condition statement: %s\n",
                sqlite3_errmsg(fDatabase->Handle()));
        return B_ERROR;
    }

    sqlite3_bind_int(stmt, 1, roomId);
    sqlite3_bind_text(stmt, 2, direction.String(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, lockedMessage.String(), -1, SQLITE_TRANSIENT);

    if (requiredItemId > 0)
        sqlite3_bind_int(stmt, 4, requiredItemId);
    else
        sqlite3_bind_null(stmt, 4);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        fprintf(stderr, "Failed to create exit condition: %s\n",
                sqlite3_errmsg(fDatabase->Handle()));
        return B_ERROR;
    }

    printf("Created exit condition for room %d %s\n", roomId, direction.String());
    return B_OK;
}


status_t
GameEditor::DeleteExitCondition(int roomId, const BString& direction)
{
    if (!IsReady())
        return B_NO_INIT;

    const char* sql = "DELETE FROM exit_conditions WHERE room_id = ? AND direction = ?;";

    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(fDatabase->Handle(), sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to prepare delete exit condition statement: %s\n",
                sqlite3_errmsg(fDatabase->Handle()));
        return B_ERROR;
    }

    sqlite3_bind_int(stmt, 1, roomId);
    sqlite3_bind_text(stmt, 2, direction.String(), -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        fprintf(stderr, "Failed to delete exit condition: %s\n",
                sqlite3_errmsg(fDatabase->Handle()));
        return B_ERROR;
    }

    printf("Deleted exit condition for room %d %s\n", roomId, direction.String());
    return B_OK;
}


status_t
GameEditor::SaveAsInitialState()
{
    if (!IsReady())
        return B_NO_INIT;

    return fDatabase->SaveCurrentStateAsInitial();
}


status_t
GameEditor::ClearGameState()
{
    if (!IsReady())
        return B_NO_INIT;

    return fDatabase->ClearGameState();
}