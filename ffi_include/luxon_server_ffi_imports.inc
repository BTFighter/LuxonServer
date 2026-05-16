// Copyright (c) 2026, the Luxon Server contributors
// SPDX-License-Identifier: BSD-3-Clause

#ifndef FFI_IMPORT
#if defined(FFI_WASM) || defined(__wasm__)
#define FFI_IMPORT(ret, name, params, ...) __attribute__((import_module("env"), import_name(#name))) extern ret name params;
#else
#define FFI_IMPORT(ret, name, params, ...) ret(*name) params;
#endif
#endif

#ifdef LUXON_SERVER_ENABLE_PLUGINS
/* --- GAME PLUGIN IMPORTS --- */
FFI_IMPORT(void, gamePluginOnAttach, (uint32_t plugin_id, GameHandle game), plugin_id, game)
FFI_IMPORT(uint8_t, gamePluginOnCreateGame,
           (uint32_t plugin_id, GameHandle game, SerMessageHandle req, PeerHandle creator, bool is_join, bool create_if_not_exist), plugin_id, game, req,
           creator, is_join, create_if_not_exist)
FFI_IMPORT(uint8_t, gamePluginBeforeJoin, (uint32_t plugin_id, GameHandle game, SerMessageHandle req, PeerHandle joiner), plugin_id, game, req, joiner)
FFI_IMPORT(uint8_t, gamePluginOnJoinGame, (uint32_t plugin_id, GameHandle game, SerMessageHandle req, GamePeerHandle joiner), plugin_id, game, req, joiner)
FFI_IMPORT(uint8_t, gamePluginOnLeave, (uint32_t plugin_id, GameHandle game, SerMessageHandle req, GamePeerHandle leaver), plugin_id, game, req, leaver)
FFI_IMPORT(uint8_t, gamePluginOnRaiseEvent,
           (uint32_t plugin_id, GameHandle game, SerMessageHandle req, GamePeerHandle raiser, EventHandle event, uint8_t cache_op), plugin_id, game, req,
           raiser, event, cache_op)
FFI_IMPORT(uint8_t, gamePluginBeforeSetProperties,
           (uint32_t plugin_id, GameHandle game, SerMessageHandle req, GamePeerHandle setter, bool broadcast, int32_t target_actor_id, SerValueHandle update,
            SerValueHandle expected),
           plugin_id, game, req, setter, broadcast, target_actor_id, update, expected)
FFI_IMPORT(uint8_t, gamePluginOnSetProperties,
           (uint32_t plugin_id, GameHandle game, SerMessageHandle req, GamePeerHandle setter, bool broadcast, int32_t target_actor_id, SerValueHandle update,
            SerValueHandle expected),
           plugin_id, game, req, setter, broadcast, target_actor_id, update, expected)
FFI_IMPORT(uint8_t, gamePluginBeforeCloseGame, (uint32_t plugin_id, GameHandle game, bool failed_on_create), plugin_id, game, failed_on_create)
FFI_IMPORT(uint8_t, gamePluginOnCloseGame, (uint32_t plugin_id, GameHandle game, bool failed_on_create), plugin_id, game, failed_on_create)

/* --- AUTH PLUGIN IMPORTS --- */
FFI_IMPORT(bool, authPluginOnAuthenticate,
           (unsigned type, const char *requested_user_id, const char *params, const char *data, const char *secret, const char *auth_url, char *out_user_id,
            ffi_size_t max_user_id_len, SerMessageHandle out_err_resp),
           type, requested_user_id, params, data, secret, auth_url, out_user_id, max_user_id_len, out_err_resp)
#endif

#ifdef LUXON_SERVER_ENABLE_HOOKPOINTS
/* --- HOOKPOINTS IMPORTS --- */
FFI_IMPORT(bool, hookpointMasterServerHandleOperationRequestJoinGame, (HandlerBaseHandle handler, const char *game_id, bool is_join), handler, game_id, is_join)
FFI_IMPORT(bool, hookpointMasterServerHandleOperationRequestCreateGame, (HandlerBaseHandle handler, const char *game_id), handler, game_id)
FFI_IMPORT(bool, hookpointHandlerBaseHandleENetCommandOnMessage, (HandlerBaseHandle handler, SerMessageHandle message), handler, message)
FFI_IMPORT(bool, hookpointAppLoadAppSettings, (void *app, bool *out_success), app, out_success)
#endif
