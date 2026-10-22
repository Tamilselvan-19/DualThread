CREATE DATABASE IF NOT EXISTS ChatDatabase;
USE ChatDatabase;

CREATE TABLE IF NOT EXISTS tbl_UserDetails (
    UserId VARCHAR(100) NOT NULL,
    PasswordHash VARCHAR(255) NOT NULL,
    IPAddress VARCHAR(100) NULL,
    Timestamp DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (UserId)
);

CREATE TABLE IF NOT EXISTS tbl_ChatDetails (
    UniqueId INT AUTO_INCREMENT PRIMARY KEY,
    UserId VARCHAR(100) NOT NULL,
    MessageId VARCHAR(255) NOT NULL UNIQUE,
    Timestamp DATETIME NOT NULL,
    ChatMessage TEXT NOT NULL,
    ConversationType VARCHAR(16) NOT NULL DEFAULT 'personal',
    TargetId VARCHAR(100) NULL,
    INDEX idx_user_timestamp (UserId, Timestamp),
    INDEX idx_conversation (ConversationType, TargetId, UniqueId)
);

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
    INDEX idx_group_member_user (UserId),
    CONSTRAINT fk_group_members_group
        FOREIGN KEY (GroupId) REFERENCES tbl_Groups(GroupId)
        ON DELETE CASCADE
);
