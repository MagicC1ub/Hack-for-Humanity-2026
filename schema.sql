
DROP TABLE IF EXISTS badges;
CREATE TABLE badges (
    id TEXT PRIMARY KEY,
    name TEXT,
    description TEXT,
    badge_type TEXT, 
    rarity_percentage REAL,
    earned_count INTEGER,
    current_rank TEXT,
    last_earned INTEGER
);

INSERT INTO badges VALUES 
('early_bird', 'Dawn Commuter', 'Log a transit trip before 6:30 AM.', 'temporary', 12.5, 1, 'Active', strftime('%s', 'now')),
('night_owl', 'Midnight Train', 'Ride the light rail after 11:00 PM.', 'temporary', 8.2, 1, 'Active', strftime('%s', 'now') - 100),
('weekend_warrior', 'Weekend Warrior', 'Use transit 4 times over the weekend.', 'temporary', 15.0, 1, 'Active', strftime('%s', 'now') - 200),
('museum_buff', 'Culture Connoisseur', 'Visit various museums.', 'upgradable', 45.0, 15, 'Bronze', strftime('%s', 'now')),
('quilt_museum', 'Textile Tactician', 'Visited the San Jose Museum of Quilts & Textiles.', 'one_and_done', 25.5, 1, 'Earned', strftime('%s', 'now'));
