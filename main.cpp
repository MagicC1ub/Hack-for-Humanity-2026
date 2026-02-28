#include "crow_all.h"
#include <vector>
#include <unordered_map>

int main() {
    crow::SimpleApp app;

    // Route to serve the main HTML page
    CROW_ROUTE(app, "/")
    ([]() {
        auto page = crow::mustache::load("index.html");
        return page.render();
    });

    // API Route to send the badge data to the frontend
    CROW_ROUTE(app, "/api/badges")
    ([]() {
        // Simulating the data structure we discussed
        std::vector<crow::json::wvalue> badges;
        
        badges.push_back({
            {"id", "quilt_museum"}, {"name", "Textile Tactician"}, 
            {"earned", false}, {"rarity", 25.5}
        });
        badges.push_back({
            {"id", "early_bird"}, {"name", "Dawn Commuter"}, 
            {"earned", true}, {"rarity", 42.0}
        });
        badges.push_back({
            {"id", "caltrain_king"}, {"name", "Iron Horse"}, 
            {"earned", false}, {"rarity", 12.3}
        });

        crow::json::wvalue final_result = badges;
        return crow::response(final_result);
    });

    // Start the server on port 8080
    app.port(8080).multithreaded().run();
}