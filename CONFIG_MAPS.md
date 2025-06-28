Map
==========
# Supported map keys
## \`map.ahcl.file_name\`
- Type: string
- Constraints: Min length: 1. Max length: 15.
- Default value: Asuna.Dat
- Error handling: Use default value

## \`map.ahcl.mission\`
- Type: string
- Constraints: Min length: 1. Max length: 15.
- Default value: HostBot
- Error handling: Use default value

## \`map.ahcl.player_name\`
- Type: string
- Constraints: Min length: 1. Max length: 15.
- Default value: HostBot
- Error handling: Use default value

## \`map.ahcl.slot\`
- Type: slot
- Default value: 255
- Error handling: Use default value

## \`map.ahcl.supported\`
- Type: bool
- Default value: false
- Error handling: Use default value

## \`map.ahcl.toggle\`
- Type: enum
- Default value: MAP_FEATURE_TOGGLE_DISABLED
- Error handling: Use default value

## \`map.ai.type\`
- Type: enum
- Default value: Empty
- Error handling: Use default value

## \`map.auto_commands.init\`
- Type: list
- Default value: 
- Error handling: Use default value

## \`map.auto_start.players\`
- Type: uint8
- Default value: 2
- Error handling: Use default value

## \`map.auto_start.seconds\`
- Type: int64
- Default value: 180
- Error handling: Use default value

## \`map.cfg.hosting.game_versions.expansion.default\`
- Type: enum
- Default value: SELECT_EXPANSION_TFT
- Error handling: Use default value

## \`map.cfg.hosting.game_versions.main\`
- Type: version
- Error handling: Use default value

## \`map.cfg.partial\`
- Type: bool
- Default value: false
- Error handling: Use default value

## \`map.cfg.schema_number\`
- Type: uint8
- Default value: 0
- Error handling: Use default value

## \`map.custom_forces.observer_team\`
- Type: uint8
- Default value: Empty
- Error handling: Use default value

## \`map.data_set\`
- Type: uint8
- Default value: Empty
- Error handling: Use default value

## \`map.editor_version\`
- Type: uint32
- Default value: Empty
- Error handling: Use default value

## \`map.expansion\`
- Type: bool
- Default value: Empty
- Error handling: Use default value

## \`map.file_hash.crc32\`
- Type: uint8vector
- Default value: 4
- Error handling: Use default value

## \`map.file_hash.sha1\`
- Type: uint8vector
- Default value: 20
- Error handling: Use default value

## \`map.filter_maker\`
- Type: uint8
- Default value: MAPFILTER_MAKER_USER
- Error handling: Use default value

## \`map.filter_obs\`
- Type: uint8
- Default value: Empty
- Error handling: Abort operation

## \`map.filter_size\`
- Type: uint8
- Default value: MAPFILTER_SIZE_LARGE
- Error handling: Use default value

## \`map.filter_type\`
- Type: uint8
- Default value: Empty
- Error handling: Use default value

## \`map.flags\`
- Type: uint8
- Default value: Empty
- Error handling: Use default value

## \`map.game_result.constraints.allied_victory_except_leaver.required\`
- Type: bool
- Default value: Empty
- Error handling: Use default value

## \`map.game_result.constraints.controllers.losers.max\`
- Type: playercount
- Default value: Empty
- Error handling: Use default value

## \`map.game_result.constraints.controllers.losers.min\`
- Type: playercount
- Default value: Empty
- Error handling: Use default value

## \`map.game_result.constraints.controllers.max\`
- Type: playercount
- Default value: Empty
- Error handling: Use default value

## \`map.game_result.constraints.controllers.min\`
- Type: playercount
- Default value: Empty
- Error handling: Use default value

## \`map.game_result.constraints.controllers.winners.max\`
- Type: playercount
- Default value: Empty
- Error handling: Use default value

## \`map.game_result.constraints.controllers.winners.min\`
- Type: playercount
- Default value: Empty
- Error handling: Use default value

## \`map.game_result.constraints.draw_and_win.allowed\`
- Type: bool
- Default value: Empty
- Error handling: Use default value

## \`map.game_result.constraints.draw.allowed\`
- Type: bool
- Default value: Empty
- Error handling: Use default value

## \`map.game_result.constraints.drawing_leavers.allowed\`
- Type: bool
- Default value: Empty
- Error handling: Use default value

## \`map.game_result.constraints.shared_losers.all.allowed\`
- Type: bool
- Default value: Empty
- Error handling: Use default value

## \`map.game_result.constraints.shared_winners.all.allowed\`
- Type: bool
- Default value: Empty
- Error handling: Use default value

## \`map.game_result.constraints.teams.losers.max\`
- Type: playercount
- Default value: Empty
- Error handling: Use default value

## \`map.game_result.constraints.teams.losers.min\`
- Type: playercount
- Default value: Empty
- Error handling: Use default value

## \`map.game_result.constraints.teams.max\`
- Type: playercount
- Default value: Empty
- Error handling: Use default value

## \`map.game_result.constraints.teams.min\`
- Type: playercount
- Default value: Empty
- Error handling: Use default value

## \`map.game_result.constraints.teams.winners.max\`
- Type: playercount
- Default value: Empty
- Error handling: Use default value

## \`map.game_result.constraints.teams.winners.min\`
- Type: playercount
- Default value: Empty
- Error handling: Use default value

## \`map.game_result.constraints.winner_has_allied_leaver.allowed\`
- Type: bool
- Default value: Empty
- Error handling: Use default value

## \`map.game_result.constraints.winning_leavers.allowed\`
- Type: bool
- Default value: Empty
- Error handling: Use default value

## \`map.game_result.resolution.conflict_handler\`
- Type: enum\<gameresultconflicthandler\>
- Default value: Empty
- Error handling: Abort operation

## \`map.game_result.resolution.undecided_handler.computer\`
- Type: enum\<gameresultcomputerundecidedhandler\>
- Default value: Empty
- Error handling: Use default value

## \`map.game_result.resolution.undecided_handler.user\`
- Type: enum\<gameresultuserundecidedhandler\>
- Default value: Empty
- Error handling: Use default value

## \`map.game_result.resolution.undecided_handler.virtual\`
- Type: enum\<gameresultvirtualundecidedhandler\>
- Default value: Empty
- Error handling: Use default value

## \`map.game_result.source\`
- Type: enum\<gameresultsourceselect\>
- Default value: Empty
- Error handling: Abort operation

## \`map.game_version.min\`
- Type: version
- Error handling: Use default value

## \`map.game_version.suggested.min\`
- Type: version
- Error handling: Use default value

## \`map.hcl.default\`
- Type: string
- Constraints: Min length: 1. Max length: m_MapVersionMaxSlots.
- Default value: string(
- Error handling: Use default value

## \`map.hcl.info.virtual_players\`
- Type: bool
- Default value: false
- Error handling: Use default value

## \`map.hcl.supported\`
- Type: bool
- Default value: Empty
- Error handling: Use default value

## \`map.hcl.toggle\`
- Type: enum
- Default value: Empty
- Error handling: Use default value

## \`map.height\`
- Type: uint8vector
- Default value: 2
- Error handling: Use default value

## \`map.hosting.apm_limiter.max.average\`
- Type: uint16
- Default value: 800
- Error handling: Use default value

## \`map.hosting.apm_limiter.max.burst\`
- Type: uint16
- Default value: (uint16_t
- Error handling: Use default value

## \`map.hosting.autostart.requires_balance\`
- Type: bool
- Default value: false
- Error handling: Use default value

## \`map.hosting.chat_in_game.enabled\`
- Type: bool
- Default value: false
- Error handling: Use default value

## \`map.hosting.chat_lobby.enabled\`
- Type: bool
- Default value: false
- Error handling: Use default value

## \`map.hosting.expiry.loading.mode\`
- Type: enum\<gameloadingtimeoutmode\>
- Default value: GameLoadingTimeoutMode::kStrict
- Error handling: Use default value

## \`map.hosting.expiry.loading.timeout\`
- Type: uint32
- Default value: 900
- Error handling: Use default value

## \`map.hosting.expiry.lobby.mode\`
- Type: enum\<lobbytimeoutmode\>
- Default value: LobbyTimeoutMode::kOwnerMissing
- Error handling: Use default value

## \`map.hosting.expiry.lobby.timeout\`
- Type: uint32
- Default value: 600
- Error handling: Use default value

## \`map.hosting.expiry.owner.lan\`
- Type: bool
- Default value: true
- Error handling: Use default value

## \`map.hosting.expiry.owner.mode\`
- Type: enum\<lobbyownertimeoutmode\>
- Default value: LobbyOwnerTimeoutMode::kAbsent
- Error handling: Use default value

## \`map.hosting.expiry.owner.timeout\`
- Type: uint32
- Default value: 120
- Error handling: Use default value

## \`map.hosting.expiry.playing.mode\`
- Type: enum\<gameplayingtimeoutmode\>
- Default value: GamePlayingTimeoutMode::kStrict
- Error handling: Use default value

## \`map.hosting.expiry.playing.timeout\`
- Type: uint32
- Default value: 18000
- Error handling: Use default value

## \`map.hosting.expiry.playing.timeout.eager_interval\`
- Type: uint32
- Default value: 900
- Error handling: Use default value

## \`map.hosting.expiry.playing.timeout.eager_warnings\`
- Type: uint8
- Default value: 5
- Error handling: Use default value

## \`map.hosting.expiry.playing.timeout.soon_interval\`
- Type: uint32
- Default value: 60
- Error handling: Use default value

## \`map.hosting.expiry.playing.timeout.soon_warnings\`
- Type: uint8
- Default value: 10
- Error handling: Use default value

## \`map.hosting.fake_users.share_units.mode\`
- Type: enum\<fakeusersshareunitsmode\>
- Default value: FakeUsersShareUnitsMode::kAuto
- Error handling: Use default value

## \`map.hosting.game_over.player_count\`
- Type: uint8
- Default value: 1
- Error handling: Use default value

## \`map.hosting.game_protocol.leaver_handler\`
- Type: enum\<onplayerleavehandler\>
- Default value: OnPlayerLeaveHandler::kNative
- Error handling: Use default value

## \`map.hosting.game_protocol.share_handler\`
- Type: enum\<onshareunitshandler\>
- Default value: OnShareUnitsHandler::kNative
- Error handling: Use default value

## \`map.hosting.game_ready.mode\`
- Type: enum\<playersreadymode\>
- Default value: PlayersReadyMode::kExpectRace
- Error handling: Use default value

## \`map.hosting.game_start.count_down_interval\`
- Type: uint32
- Default value: 500
- Error handling: Use default value

## \`map.hosting.game_start.count_down_ticks\`
- Type: uint32
- Default value: 5
- Error handling: Use default value

## \`map.hosting.game_versions.expansion.default\`
- Type: enum
- Default value: SELECT_EXPANSION_TFT
- Error handling: Use default value

## \`map.hosting.game_versions.main\`
- Type: version
- Error handling: Use default value

## \`map.hosting.high_ping.kick_ms\`
- Type: uint32
- Default value: 300
- Error handling: Use default value

## \`map.hosting.high_ping.safe_ms\`
- Type: uint32
- Default value: 150
- Error handling: Use default value

## \`map.hosting.high_ping.warn_ms\`
- Type: uint32
- Default value: 200
- Error handling: Use default value

## \`map.hosting.ip_filter.flood_handler\`
- Type: enum\<onipfloodhandler\>
- Default value: OnIPFloodHandler::kDeny
- Error handling: Use default value

## \`map.hosting.join_in_progress.observers\`
- Type: bool
- Default value: false
- Error handling: Use default value

## \`map.hosting.join_in_progress.players\`
- Type: bool
- Default value: false
- Error handling: Use default value

## \`map.hosting.latency.default\`
- Type: uint16
- Default value: 100
- Error handling: Use default value

## \`map.hosting.latency.equalizer.enabled\`
- Type: bool
- Default value: false
- Error handling: Use default value

## \`map.hosting.latency.equalizer.frames\`
- Type: uint8
- Default value: PING_EQUALIZER_DEFAULT_FRAMES
- Error handling: Use default value

## \`map.hosting.load_in_game.enabled\`
- Type: bool
- Default value: false
- Error handling: Use default value

## \`map.hosting.log_commands\`
- Type: bool
- Default value: false
- Error handling: Use default value

## \`map.hosting.name_filter.is_pipe_harmful\`
- Type: bool
- Default value: false
- Error handling: Use default value

## \`map.hosting.name_filter.unsafe_handler\`
- Type: enum\<onunsafenamehandler\>
- Default value: OnUnsafeNameHandler::kDeny
- Error handling: Use default value

## \`map.hosting.nicknames.hide_in_game\`
- Type: enum\<hideignmode\>
- Default value: HideIGNMode::kAuto
- Error handling: Use default value

## \`map.hosting.nicknames.hide_lobby\`
- Type: bool
- Default value: false
- Error handling: Use default value

## \`map.hosting.realm_broadcast.error_handler\`
- Type: enum\<onrealmbroadcasterrorhandler\>
- Default value: OnRealmBroadcastErrorHandler::kExitOnMaxErrors
- Error handling: Use default value

## \`map.hosting.save_game.allowed\`
- Type: bool
- Default value: false
- Error handling: Use default value

## \`map.load_screen.image.mime_type\`
- Type: string
- Default value: string(
- Error handling: Use default value

## \`map.load_screen.image.path\`
- Type: string
- Default value: string(
- Error handling: Use default value

## \`map.load_screen.image.size\`
- Type: uint32
- Default value: 0
- Error handling: Use default value

## \`map.local_mod_time\`
- Type: int64
- Error handling: Use default value

## \`map.local_path\`
- Type: path
- Default value: Empty
- Error handling: Use default value

## \`map.locale.lang_id\`
- Type: uint16
- Default value: deductedLangId.value_or(m_GameLocaleLangID
- Error handling: Use default value

## \`map.lua\`
- Type: bool
- Default value: false
- Error handling: Use default value

## \`map.melee\`
- Type: bool
- Default value: Empty
- Error handling: Use default value

## \`map.meta.author\`
- Type: string
- Default value: Unknown
- Error handling: Use default value

## \`map.meta.desc\`
- Type: string
- Default value: Nondescript
- Error handling: Use default value

## \`map.net.lag_screen.enabled\`
- Type: bool
- Default value: false
- Error handling: Abort operation

## \`map.net.start_lag.sync_limit\`
- Type: uint32
- Default value: 32
- Error handling: Use default value

## \`map.net.stop_lag.sync_limit\`
- Type: uint32
- Default value: 8
- Error handling: Use default value

## \`map.num_disabled\`
- Type: uint8
- Default value: Empty
- Error handling: Use default value

## \`map.num_players\`
- Type: uint8
- Default value: Empty
- Error handling: Use default value

## \`map.num_teams\`
- Type: uint8
- Default value: Empty
- Error handling: Use default value

## \`map.observers\`
- Type: enum\<gameobserversmode\>
- Default value: Empty
- Error handling: Use default value

## \`map.options\`
- Type: uint32
- Default value: Empty
- Error handling: Use default value

## \`map.preview.image.mime_type\`
- Type: string
- Default value: string(
- Error handling: Use default value

## \`map.preview.image.path\`
- Type: string
- Default value: string(
- Error handling: Use default value

## \`map.preview.image.size\`
- Type: uint32
- Default value: 0
- Error handling: Use default value

## \`map.preview.image.source\`
- Type: enum
- Default value: MAP_FILE_SOURCE_CATEGORY_NONE
- Error handling: Use default value

## \`map.prologue.image.mime_type\`
- Type: string
- Default value: string(
- Error handling: Use default value

## \`map.prologue.image.path\`
- Type: string
- Default value: string(
- Error handling: Use default value

## \`map.prologue.image.size\`
- Type: uint32
- Default value: 0
- Error handling: Use default value

## \`map.reconnection.mode\`
- Type: enum
- Default value: RECONNECT_DISABLED
- Error handling: Use default value

## \`map.size\`
- Type: uint32
- Error handling: Use default value

## \`map.speed\`
- Type: enum\<gamespeed\>
- Default value: GameSpeed::kFast
- Error handling: Use default value

## \`map.standard_path\`
- Type: bool
- Default value: false
- Error handling: Use default value

## \`map.title\`
- Type: string
- Default value: Just another Warcraft 3 Map
- Error handling: Use default value

## \`map.visibility\`
- Type: enum\<gamevisibilitymode\>
- Default value: GameVisibilityMode::kDefault
- Error handling: Use default value

## \`map.w3hmc.file_name\`
- Type: string
- Constraints: Min length: 1. Max length: 15.
- Default value: W3HMC
- Error handling: Use default value

## \`map.w3hmc.player_name\`
- Type: string
- Constraints: Min length: 1. Max length: 15.
- Default value: [HMC]Aura
- Error handling: Use default value

## \`map.w3hmc.secret\`
- Type: string
- Constraints: Min length: 1. Max length: 15.
- Default value: string(
- Error handling: Use default value

## \`map.w3hmc.slot\`
- Type: slot
- Default value: 255
- Error handling: Use default value

## \`map.w3hmc.supported\`
- Type: bool
- Default value: false
- Error handling: Use default value

## \`map.w3hmc.toggle\`
- Type: enum
- Default value: MAP_FEATURE_TOGGLE_DISABLED
- Error handling: Use default value

## \`map.w3hmc.trigger.complement\`
- Type: uint32
- Default value: Empty
- Error handling: Use default value

## \`map.w3hmc.trigger.main\`
- Type: uint32
- Default value: 0
- Error handling: Use default value

## \`map.w3mmd.enabled\`
- Type: bool
- Default value: Empty
- Error handling: Use default value

## \`map.w3mmd.features.game_over\`
- Type: bool
- Default value: false
- Error handling: Use default value

## \`map.w3mmd.features.prioritize_players\`
- Type: bool
- Default value: Empty
- Error handling: Use default value

## \`map.w3mmd.features.virtual_players\`
- Type: bool
- Default value: false
- Error handling: Use default value

## \`map.w3mmd.subjects.computers.enabled\`
- Type: bool
- Default value: Empty
- Error handling: Use default value

## \`map.w3mmd.supported\`
- Type: bool
- Default value: Empty
- Error handling: Use default value

## \`map.w3mmd.type\`
- Type: enum
- Default value: Empty
- Error handling: Use default value

## \`map.width\`
- Type: uint8vector
- Default value: 2
- Error handling: Use default value
