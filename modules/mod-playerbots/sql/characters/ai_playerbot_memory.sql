-- Phase 1 persistent LLM memory. One row = one explicit fact a REAL player
-- asked a specific bot to remember about that player. Written only from
-- ChatReplyAction::HandleMemoryCommand() (strategy/actions/SayAction.cpp),
-- which requires IsRealPlayer(sender) == true before any INSERT/UPDATE here
-- is reached -- see PlayerbotMemoryStore.h/.cpp. Never written from bot-
-- generated chat, llmContext, or Option-A context-only messages.
--
-- `bot_guid`/`subject_guid` are the same "raw ObjectGuid" values already used
-- by `ai_playerbot_db_store.guid` (Player::GetObjectGuid().GetRawValue()),
-- not character names, so a name change or duplicate name never causes a
-- lookup miss or cross-player leak.
CREATE TABLE IF NOT EXISTS `ai_playerbot_memory` (
  `id`               bigint(20)   NOT NULL AUTO_INCREMENT,
  `bot_guid`         bigint(20)   NOT NULL,
  `subject_guid`     bigint(20)   NOT NULL,
  `category`         varchar(32)  NOT NULL DEFAULT 'agreement',   -- Phase 1 only ever writes 'agreement'; 'relationship'/'preference'/'joke' reserved for a later phase's finer-grained triggers.
  `fact_normalized`  varchar(1024) NOT NULL,                      -- trimmed/collapsed/lowercased, trailing punctuation stripped -- dedup + forget-lookup key.
  `fact_original`    varchar(1024) NOT NULL,                      -- verbatim captured text, shown to the LLM and to diagnostics.
  `status`           varchar(16)  NOT NULL DEFAULT 'active',      -- 'active' | 'retracted'. Phase 1 never writes 'superseded' (no correction-inference trigger yet).
  `source_type`      varchar(16)  NOT NULL DEFAULT 'human_statement', -- always 'human_statement' in Phase 1; 'human_correction'/'command' reserved.
  `source_guid`      bigint(20)   NOT NULL,                       -- the real player who spoke; equals subject_guid for every Phase-1 trigger.
  `supersedes_id`    bigint(20)   NULL,                           -- unused in Phase 1, reserved for a later correction/Supersede() path.
  `created_at`       timestamp    NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at`       timestamp    NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  `last_used_at`      timestamp   NULL,                           -- unused in Phase 1, reserved for later ranking/eviction.
  `importance`       tinyint      NOT NULL DEFAULT 0,             -- unused in Phase 1.
  PRIMARY KEY (`id`),
  KEY `bot_guid` (`bot_guid`),
  KEY `subject_guid` (`subject_guid`),
  KEY `bot_subject_status` (`bot_guid`, `subject_guid`, `status`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8 COLLATE=utf8mb3_general_ci;
