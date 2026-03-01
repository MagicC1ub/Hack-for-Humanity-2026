DROP TABLE IF EXISTS user_badges;
DROP TABLE IF EXISTS badges;
DROP TABLE IF EXISTS users;

-- Table 1: Users
CREATE TABLE users (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    username TEXT UNIQUE,
    role TEXT
);

-- Table 2: The Global Badge Stockpile
CREATE TABLE badges (
    id TEXT PRIMARY KEY,
    name TEXT,
    description TEXT,
    badge_type TEXT, 
    rarity_percentage REAL
);

-- Table 3: The Bridge (User Progress)
CREATE TABLE user_badges (
    user_id INTEGER,
    badge_id TEXT,
    earned_count INTEGER,
    current_rank TEXT,
    last_earned INTEGER,
    PRIMARY KEY (user_id, badge_id),
    FOREIGN KEY (user_id) REFERENCES users(id),
    FOREIGN KEY (badge_id) REFERENCES badges(id)
);

-- Insert our two test accounts
INSERT INTO users (username, role) VALUES ('chris_transit', 'user');
INSERT INTO users (username, role) VALUES ('city_admin', 'admin');

-- Insert the global Stockpile
INSERT INTO badges VALUES 
('early_bird', 'Dawn Commuter', 'Log a transit trip before 6:30 AM.', 'temporary', 12.5),
('night_owl', 'Midnight Train', 'Ride the light rail after 11:00 PM.', 'temporary', 8.2),
('museum_buff', 'Culture Connoisseur', 'Visit various museums.', 'upgradable', 45.0),
('quilt_museum', 'Textile Tactician', 'Visited the San Jose Museum of Quilts & Textiles.', 'one_and_done', 25.5),
('tech_trek', 'Silicon Valley Local', 'Rode transit to the Tech Interactive.', 'one_and_done', 30.1);

-- Give 'chris_transit' (User 1) some starting progress
INSERT INTO user_badges VALUES 
(1, 'early_bird', 1, 'Active', strftime('%s', 'now')),
(1, 'museum_buff', 15, 'Bronze', strftime('%s', 'now'));