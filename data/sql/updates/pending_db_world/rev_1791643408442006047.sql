-- Appearance selection adapted from ilusixn/azerothcore-wotlk-coa, tag coa-custom-1.0.
-- Empty by default: importing appearance rows is a separate, explicit data step.
CREATE TABLE IF NOT EXISTS `custom_race_display` (
  `race` tinyint unsigned NOT NULL,
  `gender` tinyint unsigned NOT NULL,
  `idx` tinyint unsigned NOT NULL,
  `displayId` int unsigned NOT NULL,
  PRIMARY KEY (`race`, `gender`, `idx`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
