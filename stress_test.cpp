#include <sqlite3.h>
#include <iostream>
#include <string>
#include <vector>
#include <random>

int main() {
    sqlite3* db;
    if (sqlite3_open("transit.db", &db) != SQLITE_OK) {
        std::cerr << "Cannot open database." << std::endl;
        return 1;
    }

    // 1. Get all the badge IDs from the global stockpile
    std::vector<std::string> badge_ids;
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, "SELECT id FROM badges", -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            badge_ids.push_back(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
        }
    }
    sqlite3_finalize(stmt);

    std::cout << "Found " << badge_ids.size() << " badges in the stockpile." << std::endl;
    std::cout << "Simulating 500 commuters logging thousands of transit trips..." << std::endl;

    // We use a transaction to make inserting thousands of rows happen instantly
    sqlite3_exec(db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> badge_dist(0, badge_ids.size() - 1);
    std::uniform_int_distribution<> count_dist(1, 8); // Bots will earn between 1 and 8 random badges

    // 2. Generate 500 bot accounts
    for (int i = 1; i <= 500; ++i) {
        std::string username = "commuter_bot_" + std::to_string(i);
        
        // Insert the bot user
        std::string user_sql = "INSERT INTO users (username, password_hash, role) VALUES ('" + username + "', '0000', 'user')";
        sqlite3_exec(db, user_sql.c_str(), nullptr, nullptr, nullptr);
        
        int user_id = sqlite3_last_insert_rowid(db);
        int num_badges = count_dist(gen);

        // 3. Randomly award badges to this bot
        for (int j = 0; j < num_badges; ++j) {
            std::string badge = badge_ids[badge_dist(gen)];
            
            // Insert the badge progress (using OR IGNORE so we don't crash if a bot rolls the same badge twice)
            std::string ub_sql = "INSERT OR IGNORE INTO user_badges (user_id, badge_id, earned_count, current_rank, last_earned, weeks_ago) "
                                 "VALUES (" + std::to_string(user_id) + ", '" + badge + "', 1, 'Earned', strftime('%s', 'now'), 0)";
            sqlite3_exec(db, ub_sql.c_str(), nullptr, nullptr, nullptr);
        }
    }

    sqlite3_exec(db, "COMMIT;", nullptr, nullptr, nullptr);
    sqlite3_close(db);

    std::cout << "Stress test complete! Your VTA transit network is now heavily populated." << std::endl;
    return 0;
}