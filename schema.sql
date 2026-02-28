CREATE TABLE IF NOT EXISTS badges (
    id TEXT PRIMARY KEY,
    name TEXT,
    description TEXT,
    rarity_percentage REAL,
    earned_count INTEGER
);

INSERT INTO badges (id, name, description, rarity_percentage, earned_count) VALUES 
('quilt_museum', 'Textile Tactician', 'Visited the San Jose Museum of Quilts & Textiles.', 25.5, 0),
('early_bird', 'Dawn Commuter', 'Logged a transit trip before 6:30 AM.', 42.0, 1),
('caltrain_king', 'Iron Horse', 'Rode Caltrain end-to-end.', 12.3, 0);