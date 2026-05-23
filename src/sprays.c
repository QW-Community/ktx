/*
 * Server-authoritative spray policy for KTX.
 *
 * MVDSV owns the network protocol, pixel storage, and demo messages. KTX only
 * decides when a player may spray and which previously placed spray should be
 * removed when that player exceeds the current game-mode limit.
 */

#include "g_local.h"

// global limit - modes should specify a lower value 
#define KTX_MAX_SPRAYS_PER_PLAYER 10

typedef struct
{
	int ids[KTX_MAX_SPRAYS_PER_PLAYER];
	int count;
} ktx_player_sprays_t;

static ktx_player_sprays_t ktx_player_sprays[MAX_CLIENTS];

// k_player_spray_limit is a per-player policy value; clamp it to the fixed size of
// each player's tracked spray-id queue.
static int KTX_SprayLimit(void)
{
	int limit = (int)cvar("k_player_spray_limit");

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
	if (KTX_SprayPlayerIndex(self) < 0 || self->ct != ctPlayer) {
		return false;
	}

	if (isCA()) {
		return CA_CanSpray();
	}

	return false;
}

void KTX_SprayPlaced(int spray_id)
{
	ktx_player_sprays_t *sprays;
	int index = KTX_SprayPlayerIndex(self);
	int limit = KTX_SprayLimit();
	int i;

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
		if (HAVEEXTENSION(G_SPRAYCLEAR)) {
			trap_SprayClear(sprays->ids[0]);
		}

		for (i = 1; i < sprays->count; ++i) {
			sprays->ids[i - 1] = sprays->ids[i];
		}
		--sprays->count;
	}

	sprays->ids[sprays->count++] = spray_id;
}
