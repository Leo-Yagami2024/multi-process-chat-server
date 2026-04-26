# ifndef AUTH
# define AUTH
# include <sqlite3.h>

void db_init(sqlite3 *db);
int authenticate(const char *username, const char *password);
int  register_user(const char *username, const char *password);

# endif