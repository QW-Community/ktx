/*
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 *
 *  $Id$
 */

/*
 * Server-authoritative spray policy for KTX.
 *
 * MVDSV owns the network protocol, pixel storage, and demo messages. KTX only
 * decides when a player may spray and which previously placed spray should be
 * removed when that player exceeds the current game-mode limit.
 */

#include "g_local.h"

// Hard storage cap for the per-player visible spray queue. Prewar uses this
// cap directly; active matches use k_player_spray_limit clamped to this size.
#define KTX_MAX_SPRAYS_PER_PLAYER 10

typedef struct
{
	int ids[KTX_MAX_SPRAYS_PER_PLAYER];
	int count;
} ktx_player_sprays_t;

static ktx_player_sprays_t ktx_player_sprays[MAX_CLIENTS];

// k_player_spray_limit is the per-player visible spray limit while a match is
// active. Prewar uses the hard queue cap to prevent spam from exhausting the
// server's global spray storage.
static int KTX_SprayLimit(void)
{
	int limit;

	if (!match_in_progress) {
		return KTX_MAX_SPRAYS_PER_PLAYER;
	}

	limit = (int)cvar("k_player_spray_limit");

	limit = bound(0, limit, KTX_MAX_SPRAYS_PER_PLAYER);

	return limit;
}

static int KTX_SprayPlayerIndex(gedict_t *player)
{
	int index;

	if (!player) {
		return -1;
	}

	index = NUM_FOR_EDICT(player) - 1;
	if (index < 0 || index >= MAX_CLIENTS) {
		return -1;
	}

	return index;
}

static qbool KTX_SpraysForgetOldest(ktx_player_sprays_t *sprays)
{
	int i;

	if (!sprays || sprays->count <= 0) {
		return false;
	}

	if (!HAVEEXTENSION(G_SPRAYCLEAR) || !trap_SprayClear(sprays->ids[0])) {
		return false;
	}

	for (i = 1; i < sprays->count; ++i) {
		sprays->ids[i - 1] = sprays->ids[i];
	}
	--sprays->count;

	return true;
}

static qbool KTX_SpraysMakeRoomForPlayer(gedict_t *player)
{
	ktx_player_sprays_t *sprays;
	int index = KTX_SprayPlayerIndex(player);
	int limit = KTX_SprayLimit();

	if (index < 0 || limit <= 0) {
		return false;
	}

	sprays = &ktx_player_sprays[index];
	while (sprays->count >= limit) {
		if (!KTX_SpraysForgetOldest(sprays)) {
			return false;
		}
	}

	return true;
}

void KTX_SpraysClearAll(void)
{
	memset(ktx_player_sprays, 0, sizeof(ktx_player_sprays));

	if (HAVEEXTENSION(G_SPRAYCLEARALL)) {
		trap_SprayClearAll();
	}
}

// Clear every visible spray KTX has tracked for this player.
// Use this only when the sprays should be removed from the map.
void KTX_SpraysClearPlayer(gedict_t *player)
{
	ktx_player_sprays_t *sprays;
	int index = KTX_SprayPlayerIndex(player);
	int i;

	if (index < 0) {
		return;
	}

	sprays = &ktx_player_sprays[index];
	if (HAVEEXTENSION(G_SPRAYCLEAR)) {
		for (i = 0; i < sprays->count; ++i) {
			trap_SprayClear(sprays->ids[i]);
		}
	}
	memset(sprays, 0, sizeof(*sprays));
}

// Drop only this client slot's spray bookkeeping.
// Use this on disconnect so visible sprays stay on the map.
void KTX_SpraysForgetPlayer(gedict_t *player)
{
	int index = KTX_SprayPlayerIndex(player);

	if (index < 0) {
		return;
	}

	memset(&ktx_player_sprays[index], 0, sizeof(ktx_player_sprays[index]));
}

qbool KTX_CanSpray(void)
{
	qbool allowed = true;

	// Reject invalid entities and spectators.
	if (KTX_SprayPlayerIndex(self) < 0 || self->ct != ctPlayer) {
		return false;
	}

	// A zero or negative limit disables sprays.
	if (KTX_SprayLimit() <= 0) {
		return false;
	}

	// No sprays during intermission or end-of-match.
	if (intermission_running || match_over) {
		return false;
	}

	// No sprays while the server is paused.
	if (cvar("sv_paused")) {
		return false;
	}

	// No sprays during countdown.
	if (match_in_progress == 1) {
		return false;
	}

	// No sprays if nospray mode is enabled.
	if (match_in_progress && cvar("k_nospray")) {
		return false;
	}

	// CA/Wipeout decides its own in-match spray policy.
	if (isCA()) {
		allowed = CA_CanSpray();
	}
	else if (isRA()) {
		// RA decides its own in-match spray policy.
		allowed = RA_CanSpray();
	}

	if (!allowed) {
		return false;
	}

	// Free this player's own oldest spray before MVDSV allocates the next
	// global slot, so one player cannot force global eviction of others' sprays.
	if (!KTX_SpraysMakeRoomForPlayer(self)) {
		return false;
	}
	
	return true;
}

void KTX_SprayPlaced(int spray_id)
{
	ktx_player_sprays_t *sprays;
	int index = KTX_SprayPlayerIndex(self);
	int limit = KTX_SprayLimit();

	if (index < 0 || spray_id <= 0) {
		return;
	}

	sprays = &ktx_player_sprays[index];
	if (limit <= 0) {
		if (HAVEEXTENSION(G_SPRAYCLEAR)) {
			trap_SprayClear(spray_id);
		}
		return;
	}

	while (sprays->count >= limit) {
		if (!KTX_SpraysForgetOldest(sprays)) {
			if (HAVEEXTENSION(G_SPRAYCLEAR)) {
				trap_SprayClear(spray_id);
			}
			return;
		}
	}

	sprays->ids[sprays->count++] = spray_id;
}
