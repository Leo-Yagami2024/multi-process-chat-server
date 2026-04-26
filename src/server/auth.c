# include <stdio.h>
# include <string.h>
# include <stdlib.h>
# include "auth.h"
# include <sqlite3.h>

# define DB_FILE  "authenticate.db"

void db_init(sqlite3 *db) {
    const char *sql =
        "CREATE TABLE IF NOT EXISTS users ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "name TEXT UNIQUE NOT NULL,"
        "pass TEXT NOT NULL);";

    char *err_msg = 0;

    if (sqlite3_exec(db, sql, 0, 0, &err_msg) != SQLITE_OK) {
        fprintf(stderr, "Init error: %s\n", err_msg);
        sqlite3_free(err_msg);
        exit(1);
    }
}

int authenticate(const char *username, const char *password) {
    sqlite3 *db;
    sqlite3_stmt *stmt;

    if (sqlite3_open(DB_FILE, &db) != SQLITE_OK) {
        perror("Error opening DB");
        return 0;
    }

    const char *sql = "SELECT 1 FROM users WHERE name = ? AND pass = ?;";

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        sqlite3_close(db);
        return 0;
    }

    sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, password, -1, SQLITE_STATIC);

    int result = sqlite3_step(stmt);

    sqlite3_finalize(stmt);
    sqlite3_close(db);   

    return (result == SQLITE_ROW);
}

int register_user(const char *username, const char *password) {
    sqlite3 *db;
    sqlite3_stmt *stmt;

    if (sqlite3_open(DB_FILE, &db) != SQLITE_OK) return 0;

    const char *sql = "INSERT INTO users (name, pass) VALUES (?, ?);";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        sqlite3_close(db);
        return 0;
    }

    sqlite3_bind_text(stmt, 1, username, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, password, -1, SQLITE_TRANSIENT);

    int ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return ok;
}