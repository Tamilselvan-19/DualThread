USE ChatDatabase;

INSERT INTO tbl_UserDetails (UserId, PasswordHash, IPAddress, Timestamp)
VALUES
('user1', SHA2('pass123', 256), '127.0.0.1', NOW()),
('user2', SHA2('pass123', 256), '127.0.0.1', NOW()),
('user3', SHA2('pass456', 256), '127.0.0.1', NOW());

INSERT INTO tbl_ChatDetails (UserId, MessageId, Timestamp, ChatMessage)
VALUES
('user1', 'msg-001', NOW(), 'Hello everyone!'),
('user2', 'msg-002', NOW(), 'Hi team!'),
('user3', 'msg-003', NOW(), 'Welcome!');
