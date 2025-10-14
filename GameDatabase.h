#ifndef GAME_DATABASE_H
#define GAME_DATABASE_H

#include <String.h>
#include <sqlite3.h>

class GameDatabase {
public:
    GameDatabase();
    ~GameDatabase();

    // Create a new database with schema and starter content
    status_t CreateNew(const char* path);

    // Open an existing database
    status_t Open(const char* path);

    // Close current database
    void Close();

    // Check if a database is currently open
    bool IsOpen() const { return fDatabase != nullptr; }

    // Get current database path
    const char* Path() const { return fDatabasePath.String(); }

    // Verify database has correct schema
    bool VerifySchema();

    // Get the SQLite database handle (for future queries)
    sqlite3* Handle() const { return fDatabase; }

private:
    sqlite3* fDatabase;
    BString fDatabasePath;

    // Helper to execute SQL statements
    status_t _ExecuteSQL(const char* sql);

    // Create the database schema
    status_t _CreateSchema();

    // Populate with starter content (cave and mountain path)
    status_t _CreateStarterContent();
};

#endif // GAME_DATABASE_H