#include "httplib.h"
#include <sqlite3.h>
#include <string>
#include <fstream>
#include <sstream>
#include <iostream>
#include <ctime>


void logTrip(const std::string& badge_id) {
    sqlite3* db;
    if (sqlite3_open("transit.db", &db) != SQLITE_OK) return;

    // 1. Get the current earned_count for this specific badge
    int current_count = 0;
    std::string get_sql = "SELECT earned_count FROM badges WHERE id = '" + badge_id + "'";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, get_sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            current_count = sqlite3_column_int(stmt, 0);
        }
    }
    sqlite3_finalize(stmt);

    // 2. Increment the count and determine the new rank
    current_count++;
    std::string new_rank = "Locked";
    if (current_count >= 200) new_rank = "Gold";
    else if (current_count >= 50) new_rank = "Silver";
    else if (current_count >= 10) new_rank = "Bronze";
    else if (current_count >= 1)  new_rank = "Wood";

    // 3. Save the new stats back to the database
    std::string update_sql = "UPDATE badges SET earned_count = " + std::to_string(current_count) + 
                             ", current_rank = '" + new_rank + "', last_earned = strftime('%s', 'now') WHERE id = '" + badge_id + "'";
    char* err_msg = nullptr;
    sqlite3_exec(db, update_sql.c_str(), nullptr, nullptr, &err_msg);
    
    sqlite3_close(db);
}

void simulateTimePass() {
    sqlite3* db;
    if (sqlite3_open("transit.db", &db) != SQLITE_OK) return;

    // 1. DECAY: Turn all 'Active' temporary badges into 'Inactive' to simulate time passing
    std::string decay_sql = "UPDATE badges SET current_rank = 'Inactive' WHERE badge_type = 'temporary' AND current_rank = 'Active'";
    sqlite3_exec(db, decay_sql.c_str(), nullptr, nullptr, nullptr);

    // 2. PRUNE: Keep the 2 most recently deactivated, reset the rest to 'Locked'
    // We use SQL's OFFSET command to skip the first 2 highest timestamps, and target everything else.
    std::string prune_sql = R"(
        UPDATE badges 
        SET current_rank = 'Locked', earned_count = 0, last_earned = 0 
        WHERE id IN (
            SELECT id FROM badges 
            WHERE badge_type = 'temporary' AND current_rank = 'Inactive' 
            ORDER BY last_earned DESC 
            LIMIT -1 OFFSET 2
        )
    )";
    sqlite3_exec(db, prune_sql.c_str(), nullptr, nullptr, nullptr);

    sqlite3_close(db);
}


// Function to query SQLite and build a JSON string
std::string getBadgesFromDB() {
    sqlite3* db;
    if (sqlite3_open("transit.db", &db)) return "[]"; // Fallback if DB fails

    std::string json = "[";
    const char* sql = "SELECT id, name, description, badge_type, rarity_percentage, earned_count, current_rank FROM badges";
    sqlite3_stmt* stmt;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        bool first = true;
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            if (!first) json += ",";
            first = false;

            // Extract data from the database row
            std::string id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            std::string name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            std::string desc = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
            std::string type = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
            double rarity = sqlite3_column_double(stmt, 4);
            int earned = sqlite3_column_int(stmt, 5);
            std::string rank = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
            // Generate dynamic progress text for upgradable badges
            std::string progress_text = "";
            if (type == "upgradable") {
                if (earned >= 200) {
                    progress_text = "Achieved " + std::to_string(earned) + " times. MAX RANK ACHIEVED!";
                } else if (earned >= 50) {
                    progress_text = "Achieved " + std::to_string(earned) + " times. Achieve this " + std::to_string(200 - earned) + " more times to rank up to Gold.";
                } else if (earned >= 10) {
                    progress_text = "Achieved " + std::to_string(earned) + " times. Achieve this " + std::to_string(50 - earned) + " more times to rank up to Silver.";
                } else if (earned >= 1) {
                    progress_text = "Achieved " + std::to_string(earned) + " times. Achieve this " + std::to_string(10 - earned) + " more times to rank up to Bronze.";
                } else {
                    progress_text = "Achieved 0 times. Achieve this 1 time to rank up to Wood.";
                }
            }
            // Build the JSON object
            json += "{";
            json += "\"id\":\"" + id + "\",";
            json += "\"name\":\"" + name + "\",";
            json += "\"description\":\"" + desc + "\",";
            json += "\"type\":\"" + type + "\",";
            json += "\"rarity\":" + std::to_string(rarity) + ",";
            json += "\"earned_count\":" + std::to_string(earned) + ",";
            json += "\"rank\":\"" + rank + "\",";
            json += "\"progress_text\":\"" + progress_text + "\""; // ADD THIS LINE
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

    svr.Get("/", [](const httplib::Request&, httplib::Response& res) {
        std::ifstream file("templates/index.html");
        std::stringstream buffer;
        buffer << file.rdbuf();
        res.set_content(buffer.str(), "text/html");
    });

    svr.Get("/api/badges", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(getBadgesFromDB(), "application/json");
    });

    // MOVE THIS UP: Register the POST endpoint before starting the server
    svr.Post("/api/log_trip", [](const httplib::Request& req, httplib::Response& res) {
        if (req.has_param("id")) {
            logTrip(req.get_param_value("id"));
            res.set_content("{\"status\":\"success\"}", "application/json");
        }
    });

    // Endpoint to simulate time passing (Decay Logic)
    svr.Post("/api/simulate_time", [](const httplib::Request&, httplib::Response& res) {
        simulateTimePass();
        res.set_content("{\"status\":\"success\"}", "application/json");
    });

    std::cout << "Server starting at http://localhost:8080..." << std::endl;
    // The listen call must be the last thing you do in main()
    svr.listen("localhost", 8080); 
}