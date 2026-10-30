-- Migration v2: group chat + routed personal messages.
-- Safe to run once on the existing ChatDatabase. The dynamic SQL makes the
-- column additions idempotent on MySQL 8.0.

USE ChatDatabase;

SET @add_type = (
  SELECT IF(COUNT(*) = 0,
    'ALTER TABLE tbl_ChatDetails ADD COLUMN ConversationType VARCHAR(16) NOT NULL DEFAULT ''personal'' AFTER ChatMessage',
    'SELECT 1')
  FROM information_schema.COLUMNS
  WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'tbl_ChatDetails'
    AND COLUMN_NAME = 'ConversationType'
);
PREPARE stmt FROM @add_type; EXECUTE stmt; DEALLOCATE PREPARE stmt;

SET @add_target = (
  SELECT IF(COUNT(*) = 0,
    'ALTER TABLE tbl_ChatDetails ADD COLUMN TargetId VARCHAR(100) NULL AFTER ConversationType',
    'SELECT 1')
  FROM information_schema.COLUMNS
  WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'tbl_ChatDetails'
    AND COLUMN_NAME = 'TargetId'
);
PREPARE stmt FROM @add_target; EXECUTE stmt; DEALLOCATE PREPARE stmt;

SET @add_index = (
  SELECT IF(COUNT(*) = 0,
    'CREATE INDEX idx_conversation ON tbl_ChatDetails (ConversationType, TargetId, UniqueId)',
    'SELECT 1')
  FROM information_schema.STATISTICS
  WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'tbl_ChatDetails'
    AND INDEX_NAME = 'idx_conversation'
);
PREPARE stmt FROM @add_index; EXECUTE stmt; DEALLOCATE PREPARE stmt;

CREATE TABLE IF NOT EXISTS tbl_Groups (
    GroupId VARCHAR(64) NOT NULL,
    GroupName VARCHAR(128) NOT NULL,
    OwnerId VARCHAR(100) NOT NULL,
    CreatedAt DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (GroupId),
    INDEX idx_group_name (GroupName)
);

CREATE TABLE IF NOT EXISTS tbl_GroupMembers (
    GroupId VARCHAR(64) NOT NULL,
    UserId VARCHAR(100) NOT NULL,
    JoinedAt DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (GroupId, UserId),
    INDEX idx_group_member_user (UserId)
);

-- Existing broadcast rows are kept as-is. New personal and group rows carry
-- ConversationType/TargetId so they can be addressed and replayed.
