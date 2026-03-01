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

void logTrip(int user_id, const std::string& badge_id) {
    sqlite3* db;
    if (sqlite3_open("transit.db", &db) != SQLITE_OK) return;

    // 1. Get current count for User 1
    int current_count = 0;
    std::string get_sql = "SELECT earned_count FROM user_badges WHERE user_id = " + std::to_string(user_id) + " AND badge_id = '" + badge_id + "'";    sqlite3_stmt* stmt;
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
                             "WHERE user_id = " + std::to_string(user_id) + " AND badge_id = '" + badge_id + "'";
    sqlite3_exec(db, update_sql.c_str(), nullptr, nullptr, nullptr);
    
    sqlite3_close(db);
}

void simulateTimePass(int user_id) {
    sqlite3* db;
    if (sqlite3_open("transit.db", &db) != SQLITE_OK) return;

    // 1. Increment weeks_ago by 1 for ALL temporary badges this user owns
    std::string inc_sql = R"(
        UPDATE user_badges SET weeks_ago = weeks_ago + 1 
        WHERE user_id = )" + std::to_string(user_id) + R"( 
        AND badge_id IN (SELECT id FROM badges WHERE badge_type = 'temporary')
    )";
    sqlite3_exec(db, inc_sql.c_str(), nullptr, nullptr, nullptr);

    // 2. DECAY: Target User's temporary badges that are now 2 or more weeks old
    std::string decay_sql = R"(
        UPDATE user_badges SET current_rank = 'Inactive' 
        WHERE user_id = )" + std::to_string(user_id) + R"( AND current_rank IN ('Active', 'Earned') 
        AND weeks_ago >= 2
        AND badge_id IN (SELECT id FROM badges WHERE badge_type = 'temporary')
    )";
    sqlite3_exec(db, decay_sql.c_str(), nullptr, nullptr, nullptr);

    // 3. PRUNE: Keep the 2 most recently deactivated for this User, reset the rest
    std::string prune_sql = R"(
        UPDATE user_badges 
        SET current_rank = 'Locked', earned_count = 0, last_earned = 0, weeks_ago = 0 
        WHERE user_id = )" + std::to_string(user_id) + R"( AND badge_id IN (
            SELECT ub.badge_id FROM user_badges ub
            JOIN badges b ON b.id = ub.badge_id
            WHERE ub.user_id = )" + std::to_string(user_id) + R"( AND b.badge_type = 'temporary' AND ub.current_rank = 'Inactive' 
            ORDER BY ub.last_earned DESC 
            LIMIT -1 OFFSET 2
        )
    )";
    sqlite3_exec(db, prune_sql.c_str(), nullptr, nullptr, nullptr);
    sqlite3_close(db);
}


void unlockRandomBadgeFromStockpile(int user_id) {
    sqlite3* db;
    if (sqlite3_open("transit.db", &db) != SQLITE_OK) return;

    // 1. Find a random badge: Either a 'temporary' badge (even if owned), OR a badge not currently owned
    std::string find_sql = R"(
        SELECT id FROM badges 
        WHERE badge_type = 'temporary' 
           OR id NOT IN (SELECT badge_id FROM user_badges WHERE user_id = )" + std::to_string(user_id) + R"( AND current_rank != 'Locked') 
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

    // 2. Insert or Update the badge, resetting weeks_ago to 0
    if (!badge_to_unlock.empty()) {
        std::string upsert_sql = 
            "INSERT INTO user_badges (user_id, badge_id, earned_count, current_rank, last_earned, weeks_ago) "
            "VALUES (" + std::to_string(user_id) + ", '" + badge_to_unlock + "', 1, 'Earned', strftime('%s', 'now'), 0) "
            "ON CONFLICT(user_id, badge_id) DO UPDATE SET "
            "current_rank = 'Earned', earned_count = 1, last_earned = strftime('%s', 'now'), weeks_ago = 0";
            
        sqlite3_exec(db, upsert_sql.c_str(), nullptr, nullptr, nullptr);
    }
    sqlite3_close(db);
}

// Function to query SQLite and build a JSON string
std::string getBadgesFromDB(int user_id) { // Added parameter
    sqlite3* db;
    if (sqlite3_open("transit.db", &db)) return "[]";

    std::string json = "[";
    std::string sql_str = R"(
        SELECT b.id, b.name, b.description, b.badge_type, 
               COALESCE((SELECT CAST(COUNT(*) AS FLOAT) FROM user_badges ub2 WHERE ub2.badge_id = b.id AND ub2.current_rank != 'Locked') * 100.0 / NULLIF((SELECT COUNT(*) FROM users WHERE role != 'admin'), 0), 0.0) AS rarity,
               ub.earned_count, ub.current_rank, ub.weeks_ago 
        FROM badges b
        JOIN user_badges ub ON b.id = ub.badge_id
        WHERE ub.user_id = )" + std::to_string(user_id) + R"( AND ub.current_rank != 'Locked'
    )";
    
    // ... [Keep the rest of the function the same, just change sqlite3_prepare_v2 to use sql_str.c_str()]
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql_str.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
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
            int weeks_ago = sqlite3_column_int(stmt, 7); 


            std::string progress_text = "";
            if (type == "upgradable") {
                if (earned >= 200) progress_text = "Achieved " + std::to_string(earned) + " times. MAX RANK ACHIEVED!";
                else if (earned >= 50) progress_text = "Achieved " + std::to_string(earned) + " times. Achieve this " + std::to_string(200 - earned) + " more times to rank up to Gold.";
                else if (earned >= 10) progress_text = "Achieved " + std::to_string(earned) + " times. Achieve this " + std::to_string(50 - earned) + " more times to rank up to Silver.";
                else if (earned >= 1) progress_text = "Achieved " + std::to_string(earned) + " times. Achieve this " + std::to_string(10 - earned) + " more times to rank up to Bronze.";
                else progress_text = "Achieved 0 times. Achieve this 1 time to rank up to Wood.";
            } else if (type == "temporary") { // <--- ADD THIS BLOCK
                if (weeks_ago == 1) progress_text = "Obtained 1 week ago";
                else progress_text = "Obtained " + std::to_string(weeks_ago) + " weeks ago";
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
    const char* sql = "SELECT id, username, role FROM users WHERE role != 'admin'";
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

std::string getGlobalBadgesFromDB() {
    sqlite3* db;
    if (sqlite3_open("transit.db", &db)) return "[]";

    std::string json = "[";
    const char* sql = R"(
        SELECT b.id, b.name, b.description, b.badge_type, 
               COALESCE((SELECT CAST(COUNT(*) AS FLOAT) FROM user_badges ub2 WHERE ub2.badge_id = b.id AND ub2.current_rank != 'Locked') * 100.0 / NULLIF((SELECT COUNT(*) FROM users WHERE role != 'admin'), 0), 0.0) AS rarity
        FROM badges b
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

            json += "{";
            json += "\"id\":\"" + id + "\",";
            json += "\"name\":\"" + name + "\",";
            json += "\"description\":\"" + desc + "\",";
            json += "\"type\":\"" + type + "\",";
            json += "\"rarity\":" + std::to_string(rarity);
            json += "}";
        }
    }
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return json + "]";
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

    // route for commuter login page
        svr.Get("/commuter_login", [](const httplib::Request&, httplib::Response& res) {
        std::ifstream file("templates/commuter_login.html");
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


    svr.Get("/api/badges", [](const httplib::Request& req, httplib::Response& res) {
        if (req.has_param("user_id")) {
            int uid = std::stoi(req.get_param_value("user_id"));
            res.set_content(getBadgesFromDB(uid), "application/json");
        }
    });

    // --- AUTHENTICATION ROUTES ---
    svr.Post("/api/register", [](const httplib::Request& req, httplib::Response& res) {
        if (req.has_param("username") && req.has_param("password")) {
            std::string user = req.get_param_value("username");
            std::string pass = req.get_param_value("password");
            std::string hashed_pass = std::to_string(hashPassword(pass));

            sqlite3* db;
            sqlite3_open("transit.db", &db);
            std::string sql = "INSERT INTO users (username, password_hash, role) VALUES ('" + user + "', '" + hashed_pass + "', 'user')";
            char* err;
            if (sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &err) == SQLITE_OK) {
                int new_id = sqlite3_last_insert_rowid(db); // Get the ID of the new account
                res.set_content("{\"status\":\"success\", \"user_id\":" + std::to_string(new_id) + "}", "application/json");
            } else {
                res.set_content("{\"status\":\"fail\", \"error\":\"Username taken\"}", "application/json");
            }
            sqlite3_close(db);
        }
    });

    svr.Post("/api/commuter_login", [](const httplib::Request& req, httplib::Response& res) {
        if (req.has_param("username") && req.has_param("password")) {
            std::string user = req.get_param_value("username");
            std::string pass = req.get_param_value("password");
            std::string hashed_pass = std::to_string(hashPassword(pass));

            sqlite3* db;
            sqlite3_open("transit.db", &db);
            std::string sql = "SELECT id FROM users WHERE username = '" + user + "' AND password_hash = '" + hashed_pass + "'";
            sqlite3_stmt* stmt;
            int user_id = -1;
            
            if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
                if (sqlite3_step(stmt) == SQLITE_ROW) user_id = sqlite3_column_int(stmt, 0);
            }
            sqlite3_finalize(stmt);
            sqlite3_close(db);

            if (user_id != -1) {
                res.set_content("{\"status\":\"success\", \"user_id\":" + std::to_string(user_id) + "}", "application/json");
            } else {
                res.set_content("{\"status\":\"fail\"}", "application/json");
            }
        }
    });

    svr.Post("/api/log_trip", [](const httplib::Request& req, httplib::Response& res) {
        if (req.has_param("id") && req.has_param("user_id")) {
            int uid = std::stoi(req.get_param_value("user_id"));
            logTrip(uid, req.get_param_value("id"));
            res.set_content("{\"status\":\"success\"}", "application/json");
        }
    });

    svr.Post("/api/simulate_time", [](const httplib::Request& req, httplib::Response& res) {
        if (req.has_param("user_id")) {
            int uid = std::stoi(req.get_param_value("user_id"));
            simulateTimePass(uid);
            res.set_content("{\"status\":\"success\"}", "application/json");
        }
    });

    // Endpoint to swipe transit card and earn a random badge from the stockpile
    svr.Post("/api/swipe_card", [](const httplib::Request& req, httplib::Response& res) {
        if (req.has_param("user_id")) {
            unlockRandomBadgeFromStockpile(std::stoi(req.get_param_value("user_id")));
            res.set_content("{\"status\":\"success\"}", "application/json");
        }
    });

    // API Endpoint to fetch all registered users for the Admin Dashboard
    svr.Get("/api/admin/users", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(getUsersFromDB(), "application/json");
    });

    // --- ADMIN PORTAL ENDPOINTS ---
    
    // Get Global Badges
    svr.Get("/api/admin/global_badges", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(getGlobalBadgesFromDB(), "application/json");
    });


    // Save or Edit Global Badge
    svr.Post("/api/admin/save_global_badge", [](const httplib::Request& req, httplib::Response& res) {
        std::string id = req.get_param_value("id");
        std::string name = req.get_param_value("name");
        std::string desc = req.get_param_value("desc");
        std::string type = req.get_param_value("type");
        
        sqlite3* db;
        sqlite3_open("transit.db", &db);
        // We insert a default 0.0 for the old rarity column, and don't bother updating it on conflict!
        std::string sql = "INSERT INTO badges (id, name, description, badge_type, rarity_percentage) VALUES ('"+id+"', '"+name+"', '"+desc+"', '"+type+"', 0.0) ON CONFLICT(id) DO UPDATE SET name='"+name+"', description='"+desc+"', badge_type='"+type+"'";
        sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr);
        sqlite3_close(db);
        res.set_content("{\"status\":\"success\"}", "application/json");
    });

    // Delete Global Badge
    svr.Post("/api/admin/delete_global_badge", [](const httplib::Request& req, httplib::Response& res) {
        std::string id = req.get_param_value("id");
        sqlite3* db;
        sqlite3_open("transit.db", &db);
        std::string sql1 = "DELETE FROM user_badges WHERE badge_id = '" + id + "'";
        std::string sql2 = "DELETE FROM badges WHERE id = '" + id + "'";
        sqlite3_exec(db, sql1.c_str(), nullptr, nullptr, nullptr);
        sqlite3_exec(db, sql2.c_str(), nullptr, nullptr, nullptr);
        sqlite3_close(db);
        res.set_content("{\"status\":\"success\"}", "application/json");
    });

    // Delete Entire User Account
    svr.Post("/api/admin/remove_user", [](const httplib::Request& req, httplib::Response& res) {
        std::string uid = req.get_param_value("user_id");
        sqlite3* db;
        sqlite3_open("transit.db", &db);
        
        // 1. Delete all their badges first to prevent orphaned data
        std::string sql1 = "DELETE FROM user_badges WHERE user_id = " + uid;
        // 2. Delete the actual user account
        std::string sql2 = "DELETE FROM users WHERE id = " + uid;
        
        sqlite3_exec(db, sql1.c_str(), nullptr, nullptr, nullptr);
        sqlite3_exec(db, sql2.c_str(), nullptr, nullptr, nullptr);
        sqlite3_close(db);
        
        res.set_content("{\"status\":\"success\"}", "application/json");
    });

    // Give Badge to User
    svr.Post("/api/admin/give_badge", [](const httplib::Request& req, httplib::Response& res) {
        std::string uid = req.get_param_value("user_id");
        std::string bid = req.get_param_value("badge_id");
        sqlite3* db;
        sqlite3_open("transit.db", &db);
        std::string sql = "INSERT INTO user_badges (user_id, badge_id, earned_count, current_rank, last_earned) VALUES ("+uid+", '"+bid+"', 1, 'Earned', strftime('%s', 'now')) ON CONFLICT(user_id, badge_id) DO UPDATE SET current_rank='Earned', earned_count=1";
        sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr);
        sqlite3_close(db);
        res.set_content("{\"status\":\"success\"}", "application/json");
    });

    // Remove Badge from User
    svr.Post("/api/admin/remove_badge", [](const httplib::Request& req, httplib::Response& res) {
        std::string uid = req.get_param_value("user_id");
        std::string bid = req.get_param_value("badge_id");
        sqlite3* db;
        sqlite3_open("transit.db", &db);
        std::string sql = "DELETE FROM user_badges WHERE user_id = " + uid + " AND badge_id = '" + bid + "'";
        sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr);
        sqlite3_close(db);
        res.set_content("{\"status\":\"success\"}", "application/json");
    });

    // Update User Badge Count
    svr.Post("/api/admin/update_count", [](const httplib::Request& req, httplib::Response& res) {
        std::string uid = req.get_param_value("user_id");
        std::string bid = req.get_param_value("badge_id");
        int count = std::stoi(req.get_param_value("count"));
        
        std::string rank = "Locked";
        if (count >= 200) rank = "Gold";
        else if (count >= 50) rank = "Silver";
        else if (count >= 10) rank = "Bronze";
        else if (count >= 1) rank = "Wood";

        sqlite3* db;
        sqlite3_open("transit.db", &db);
        std::string sql = "UPDATE user_badges SET earned_count = " + std::to_string(count) + ", current_rank = '" + rank + "' WHERE user_id = " + uid + " AND badge_id = '" + bid + "'";
        sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr);
        sqlite3_close(db);
        res.set_content("{\"status\":\"success\"}", "application/json");
    });

    std::cout << "Server starting at http://localhost:8080..." << std::endl;
    svr.listen("localhost", 8080);
}