#include "httplib.h"
#include <sqlite3.h>
#include <string>
#include <fstream>
#include <sstream>
#include <iostream>
#include <ctime>


// lightweight hashing algorithm for C++
unsigned long hashPassword(const std::string& str) {
    unsigned long hash = 5381;
    for (char c : str) {
        hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
    }
    return hash;
}

void logTrip(const std::string& badge_id) {
    sqlite3* db;
    if (sqlite3_open("transit.db", &db) != SQLITE_OK) return;

    // 1. Get current count for User 1
    int current_count = 0;
    std::string get_sql = "SELECT earned_count FROM user_badges WHERE user_id = 1 AND badge_id = '" + badge_id + "'";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, get_sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            current_count = sqlite3_column_int(stmt, 0);
        }
    }
    sqlite3_finalize(stmt);

    // 2. Increment & Rank
    current_count++;
    std::string new_rank = "Locked";
    if (current_count >= 200) new_rank = "Gold";
    else if (current_count >= 50) new_rank = "Silver";
    else if (current_count >= 10) new_rank = "Bronze";
    else if (current_count >= 1)  new_rank = "Wood";

    // 3. Update User 1's specific record
    std::string update_sql = "UPDATE user_badges SET earned_count = " + std::to_string(current_count) + 
                             ", current_rank = '" + new_rank + "', last_earned = strftime('%s', 'now') " +
                             "WHERE user_id = 1 AND badge_id = '" + badge_id + "'";
    sqlite3_exec(db, update_sql.c_str(), nullptr, nullptr, nullptr);
    
    sqlite3_close(db);
}

void simulateTimePass() {
    sqlite3* db;
    if (sqlite3_open("transit.db", &db) != SQLITE_OK) return;

    // 1. DECAY: Target only User 1's active temporary badges
    std::string decay_sql = R"(
        UPDATE user_badges SET current_rank = 'Inactive' 
        WHERE user_id = 1 AND current_rank = 'Active' 
        AND badge_id IN (SELECT id FROM badges WHERE badge_type = 'temporary')
    )";
    sqlite3_exec(db, decay_sql.c_str(), nullptr, nullptr, nullptr);

    // 2. PRUNE: Keep the 2 most recently deactivated for User 1, reset the rest
    std::string prune_sql = R"(
        UPDATE user_badges 
        SET current_rank = 'Locked', earned_count = 0, last_earned = 0 
        WHERE user_id = 1 AND badge_id IN (
            SELECT ub.badge_id FROM user_badges ub
            JOIN badges b ON b.id = ub.badge_id
            WHERE ub.user_id = 1 AND b.badge_type = 'temporary' AND ub.current_rank = 'Inactive' 
            ORDER BY ub.last_earned DESC 
            LIMIT -1 OFFSET 2
        )
    )";
    sqlite3_exec(db, prune_sql.c_str(), nullptr, nullptr, nullptr);

    sqlite3_close(db);
}


void unlockRandomBadgeFromStockpile() {
    sqlite3* db;
    if (sqlite3_open("transit.db", &db) != SQLITE_OK) return;

    // 1. Find a random badge that User 1 DOES NOT currently have active
    std::string find_sql = R"(
        SELECT id FROM badges 
        WHERE id NOT IN (SELECT badge_id FROM user_badges WHERE user_id = 1 AND current_rank != 'Locked') 
        ORDER BY RANDOM() LIMIT 1
    )";
    
    sqlite3_stmt* stmt;
    std::string badge_to_unlock = "";
    
    if (sqlite3_prepare_v2(db, find_sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            badge_to_unlock = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        }
    }
    sqlite3_finalize(stmt);

    // 2. If we found one, Insert or Update it for User 1
    if (!badge_to_unlock.empty()) {
        std::string upsert_sql = 
            "INSERT INTO user_badges (user_id, badge_id, earned_count, current_rank, last_earned) "
            "VALUES (1, '" + badge_to_unlock + "', 1, 'Earned', strftime('%s', 'now')) "
            "ON CONFLICT(user_id, badge_id) DO UPDATE SET "
            "current_rank = 'Earned', earned_count = 1, last_earned = strftime('%s', 'now')";
            
        sqlite3_exec(db, upsert_sql.c_str(), nullptr, nullptr, nullptr);
    }

    sqlite3_close(db);
}

// Function to query SQLite and build a JSON string
std::string getBadgesFromDB() {
    sqlite3* db;
    if (sqlite3_open("transit.db", &db)) return "[]";

    std::string json = "[";
    
    // The JOIN query links the global Stockpile to User 1's specific progress
    const char* sql = R"(
        SELECT b.id, b.name, b.description, b.badge_type, b.rarity_percentage,
               ub.earned_count, ub.current_rank 
        FROM badges b
        JOIN user_badges ub ON b.id = ub.badge_id
        WHERE ub.user_id = 1 AND ub.current_rank != 'Locked'
    )";
    
    sqlite3_stmt* stmt;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        bool first = true;
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            if (!first) json += ",";
            first = false;

            std::string id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            std::string name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            std::string desc = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
            std::string type = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
            double rarity = sqlite3_column_double(stmt, 4);
            int earned = sqlite3_column_int(stmt, 5);
            std::string rank = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));

            std::string progress_text = "";
            if (type == "upgradable") {
                if (earned >= 200) progress_text = "Achieved " + std::to_string(earned) + " times. MAX RANK ACHIEVED!";
                else if (earned >= 50) progress_text = "Achieved " + std::to_string(earned) + " times. Achieve this " + std::to_string(200 - earned) + " more times to rank up to Gold.";
                else if (earned >= 10) progress_text = "Achieved " + std::to_string(earned) + " times. Achieve this " + std::to_string(50 - earned) + " more times to rank up to Silver.";
                else if (earned >= 1) progress_text = "Achieved " + std::to_string(earned) + " times. Achieve this " + std::to_string(10 - earned) + " more times to rank up to Bronze.";
                else progress_text = "Achieved 0 times. Achieve this 1 time to rank up to Wood.";
            }

            json += "{";
            json += "\"id\":\"" + id + "\",";
            json += "\"name\":\"" + name + "\",";
            json += "\"description\":\"" + desc + "\",";
            json += "\"type\":\"" + type + "\",";
            json += "\"rarity\":" + std::to_string(rarity) + ",";
            json += "\"earned_count\":" + std::to_string(earned) + ",";
            json += "\"rank\":\"" + rank + "\",";
            json += "\"progress_text\":\"" + progress_text + "\""; 
            json += "}";
        }
    }
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    json += "]";
    
    return json;
}

std::string getUsersFromDB() {
    sqlite3* db;
    if (sqlite3_open("transit.db", &db)) return "[]"; // Return empty array if DB fails

    std::string json = "[";
    const char* sql = "SELECT id, username, role FROM users";
    sqlite3_stmt* stmt;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        bool first = true;
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            if (!first) json += ",";
            first = false;

            int id = sqlite3_column_int(stmt, 0);
            std::string username = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            std::string role = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));

            json += "{";
            json += "\"id\":" + std::to_string(id) + ",";
            json += "\"username\":\"" + username + "\",";
            json += "\"role\":\"" + role + "\"";
            json += "}";
        }
    }
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    json += "]";
    
    return json;
}



int main() {
    httplib::Server svr;

    // 1. Route for the Landing Portal
    svr.Get("/", [](const httplib::Request&, httplib::Response& res) {
        std::ifstream file("templates/index.html");
        std::stringstream buffer;
        buffer << file.rdbuf();
        res.set_content(buffer.str(), "text/html");
    });

    // 2. Route for the Commuter Passport
    svr.Get("/commuter", [](const httplib::Request&, httplib::Response& res) {
        std::ifstream file("templates/commuter.html");
        std::stringstream buffer;
        buffer << file.rdbuf();
        res.set_content(buffer.str(), "text/html");
    });

    // 3. Route for the Admin Login Page
    svr.Get("/admin_login", [](const httplib::Request&, httplib::Response& res) {
        std::ifstream file("templates/admin_login.html");
        std::stringstream buffer;
        buffer << file.rdbuf();
        res.set_content(buffer.str(), "text/html");
    });

    // 4. Route for the Admin Dashboard Page
    svr.Get("/admin_dashboard", [](const httplib::Request&, httplib::Response& res) {
        std::ifstream file("templates/admin_dashboard.html");
        std::stringstream buffer;
        buffer << file.rdbuf();
        res.set_content(buffer.str(), "text/html");
    });

    // 5. API Endpoint for Authentication Logging
    svr.Post("/api/auth", [](const httplib::Request& req, httplib::Response& res) {
        if (req.has_param("password")) {
            std::string attempt = req.get_param_value("password");
            
            // Hash the attempt and compare it to the hashed value of "H4H"
            if (hashPassword(attempt) == hashPassword("H4H")) {
                res.set_content("{\"status\":\"success\"}", "application/json");
            } else {
                res.set_content("{\"status\":\"fail\"}", "application/json");
            }
        } else {
            res.set_content("{\"status\":\"fail\"}", "application/json");
        }
    });

    // API Endpoint to fetch all registered users for the Admin Dashboard
    svr.Get("/api/admin/users", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(getUsersFromDB(), "application/json");
    });

    std::cout << "Server starting at http://localhost:8080..." << std::endl;
    svr.listen("localhost", 8080);
}