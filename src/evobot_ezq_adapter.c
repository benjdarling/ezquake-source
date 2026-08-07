#include "qwsvdef.h"

#include <limits.h>

#include "evobot_ezq_adapter.h"

static evobot_host_api_t evobot_ezq_host;
static int evobot_ezq_initialized;
static int evobot_ezq_map_loaded;
static evobot_client_handle_t evobot_ezq_client_handles[MAX_CLIENTS];
static evobot_client_handle_t evobot_ezq_next_client_handle = 1;
static double evobot_ezq_command_msec_remainder;

static void EvoBot_EZQ_Print(const char *message)
{
	Con_Printf("%s", message);
}

static double EvoBot_EZQ_ServerTime(void)
{
	return sv.time;
}

static double EvoBot_EZQ_MonotonicTime(void)
{
	return Sys_DoubleTime();
}

static void EvoBot_EZQ_CopyVector(const vec3_t source, evobot_vec3_t *destination)
{
	destination->v[0] = source[0];
	destination->v[1] = source[1];
	destination->v[2] = source[2];
}

static int EvoBot_EZQ_WorldBounds(evobot_bounds_t *bounds)
{
	if (!bounds || sv.state != ss_active || !sv.worldmodel)
		return 0;

	EvoBot_EZQ_CopyVector(sv.worldmodel->mins, &bounds->mins);
	EvoBot_EZQ_CopyVector(sv.worldmodel->maxs, &bounds->maxs);
	return 1;
}

static int EvoBot_EZQ_PlayerBounds(evobot_bounds_t *bounds)
{
	extern vec3_t player_mins;
	extern vec3_t player_maxs;

	if (!bounds)
		return 0;
	EvoBot_EZQ_CopyVector(player_mins, &bounds->mins);
	EvoBot_EZQ_CopyVector(player_maxs, &bounds->maxs);
	return 1;
}

static int EvoBot_EZQ_TracePlayerWorld(const evobot_vec3_t *start,
	const evobot_vec3_t *end, evobot_trace_t *result)
{
	extern vec3_t player_mins;
	extern vec3_t player_maxs;
	vec3_t trace_start;
	vec3_t trace_end;
	trace_t trace;

	if (!start || !end || !result || sv.state != ss_active || !sv.worldmodel)
		return 0;

	VectorSet(trace_start, start->v[0], start->v[1], start->v[2]);
	VectorSet(trace_end, end->v[0], end->v[1], end->v[2]);
	trace = SV_ClipMoveToEntity(EDICT_NUM(0), NULL, trace_start,
		player_mins, player_maxs, trace_end);
	memset(result, 0, sizeof(*result));
	result->all_solid = trace.allsolid;
	result->start_solid = trace.startsolid;
	result->fraction = trace.fraction;
	EvoBot_EZQ_CopyVector(trace.endpos, &result->end);
	result->normal.v[0] = trace.plane.normal[0];
	result->normal.v[1] = trace.plane.normal[1];
	result->normal.v[2] = trace.plane.normal[2];
	return 1;
}

static evobot_contents_t EvoBot_EZQ_PointContents(const evobot_vec3_t *point)
{
	vec3_t position;
	int contents;

	if (!point || sv.state != ss_active || !sv.worldmodel)
		return EVOBOT_CONTENTS_OTHER;
	VectorSet(position, point->v[0], point->v[1], point->v[2]);
	contents = SV_PointContents(position);
	switch (contents)
	{
	case CONTENTS_EMPTY:
		return EVOBOT_CONTENTS_AIR;
	case CONTENTS_SOLID:
		return EVOBOT_CONTENTS_SOLID;
	case CONTENTS_WATER:
		return EVOBOT_CONTENTS_WATER;
	case CONTENTS_SLIME:
		return EVOBOT_CONTENTS_SLIME;
	case CONTENTS_LAVA:
		return EVOBOT_CONTENTS_LAVA;
	default:
		return EVOBOT_CONTENTS_OTHER;
	}
}

static int EvoBot_EZQ_CollisionLeaf(int contents)
{
	switch (contents)
	{
	case CONTENTS_SOLID:
		return EVOBOT_COLLISION_LEAF_SOLID;
	case CONTENTS_EMPTY:
		return EVOBOT_COLLISION_LEAF_AIR;
	case CONTENTS_WATER:
		return EVOBOT_COLLISION_LEAF_WATER;
	case CONTENTS_SLIME:
		return EVOBOT_COLLISION_LEAF_SLIME;
	case CONTENTS_LAVA:
		return EVOBOT_COLLISION_LEAF_LAVA;
	default:
		return EVOBOT_COLLISION_LEAF_OTHER;
	}
}

static hull_t *EvoBot_EZQ_CollisionHull(evobot_collision_tree_kind_t kind)
{
	if (sv.state != ss_active || !sv.worldmodel)
		return NULL;
	return &sv.worldmodel->hulls[kind == EVOBOT_COLLISION_TREE_PLAYER ? 1 : 0];
}

static int EvoBot_EZQ_CollisionTree(evobot_collision_tree_kind_t kind,
	evobot_collision_tree_t *tree)
{
	hull_t *hull = EvoBot_EZQ_CollisionHull(kind);

	if (!hull || !tree)
		return 0;
	tree->root_node = hull->firstclipnode;
	tree->first_node = hull->firstclipnode;
	tree->last_node = hull->lastclipnode;
	return 1;
}

static int EvoBot_EZQ_CollisionNode(evobot_collision_tree_kind_t kind,
	int node_index, evobot_collision_node_t *result)
{
	hull_t *hull = EvoBot_EZQ_CollisionHull(kind);
	mclipnode_t *node;
	mplane_t *plane;
	int side;

	if (!hull || !result || node_index < hull->firstclipnode ||
		node_index > hull->lastclipnode)
		return 0;
	node = &hull->clipnodes[node_index];
	plane = &hull->planes[node->planenum];
	EvoBot_EZQ_CopyVector(plane->normal, &result->normal);
	result->distance = plane->dist;
	for (side = 0; side < 2; side++)
	{
		result->children[side] = node->children[side] < 0 ?
			EvoBot_EZQ_CollisionLeaf(node->children[side]) : node->children[side];
	}
	return 1;
}

static evobot_interactor_kind_t EvoBot_EZQ_InteractorKind(const char *classname)
{
	if (!strcmp(classname, "door") || !strcmp(classname, "func_door") ||
		!strcmp(classname, "func_door_secret"))
		return EVOBOT_INTERACTOR_DOOR;
	if (!strcmp(classname, "func_button"))
		return EVOBOT_INTERACTOR_BUTTON;
	if (!strcmp(classname, "plat") || !strcmp(classname, "func_plat"))
		return EVOBOT_INTERACTOR_PLATFORM;
	if (!strcmp(classname, "train") || !strcmp(classname, "func_train"))
		return EVOBOT_INTERACTOR_TRAIN;
	if (!strcmp(classname, "trigger_teleport"))
		return EVOBOT_INTERACTOR_TELEPORTER;
	if (!strcmp(classname, "info_teleport_destination"))
		return EVOBOT_INTERACTOR_TELEPORT_DESTINATION;
	if (!strcmp(classname, "trigger_changelevel"))
		return EVOBOT_INTERACTOR_LEVEL_EXIT;
	if (!strcmp(classname, "trigger_counter") || !strcmp(classname, "trigger_once") ||
		!strcmp(classname, "trigger_multiple"))
		return EVOBOT_INTERACTOR_LOGIC;
	return EVOBOT_INTERACTOR_OTHER;
}

static int EvoBot_EZQ_IsNavigationInteractor(edict_t *entity,
	evobot_interactor_kind_t *kind)
{
	const char *classname;

	if (!entity || entity->e.free || !entity->v->classname)
		return 0;
	classname = PR_GetEntityString(entity->v->classname);
	*kind = EvoBot_EZQ_InteractorKind(classname);
	if (*kind == EVOBOT_INTERACTOR_OTHER)
		return 0;
	if (*kind == EVOBOT_INTERACTOR_LOGIC &&
		!PR_GetEntityString(entity->v->target)[0] &&
		!PR_GetEntityString(entity->v->targetname)[0])
		return 0;
	return 1;
}

static int EvoBot_EZQ_InteractorCount(void)
{
	int count = 0;
	int i;

	if (sv.state != ss_active)
		return 0;
	for (i = 0; i < sv.num_edicts; i++)
	{
		evobot_interactor_kind_t kind;

		if (EvoBot_EZQ_IsNavigationInteractor(EDICT_NUM(i), &kind))
			count++;
	}
	return count;
}

static void EvoBot_EZQ_ExpandSweptBounds(evobot_host_interactor_t *interactor,
	const float *position)
{
	int axis;

	for (axis = 0; axis < 3; axis++)
	{
		float delta = position[axis] - interactor->origin.v[axis];
		float minimum = interactor->bounds.mins.v[axis] + delta;
		float maximum = interactor->bounds.maxs.v[axis] + delta;

		if (minimum < interactor->swept_bounds.mins.v[axis])
			interactor->swept_bounds.mins.v[axis] = minimum;
		if (maximum > interactor->swept_bounds.maxs.v[axis])
			interactor->swept_bounds.maxs.v[axis] = maximum;
	}
}

static void EvoBot_EZQ_CopyOptionalString(edict_t *entity, const char *field,
	char *destination, size_t destination_size)
{
	eval_t *value = PR_GetEdictFieldValue(entity, (char *)field);

	if (!value)
	{
		destination[0] = '\0';
		return;
	}
	strlcpy(destination, PR_GetEntityString(value->string), destination_size);
}

static int EvoBot_EZQ_GetInteractor(int index, evobot_host_interactor_t *interactor)
{
	int found = 0;
	int i;

	if (index < 0 || !interactor || sv.state != ss_active)
		return 0;
	for (i = 0; i < sv.num_edicts; i++)
	{
		edict_t *entity = EDICT_NUM(i);
		evobot_interactor_kind_t kind;
		const char *classname;

		if (!EvoBot_EZQ_IsNavigationInteractor(entity, &kind))
			continue;
		if (found++ != index)
			continue;

		memset(interactor, 0, sizeof(*interactor));
		interactor->kind = kind;
		interactor->dynamic_brush = (kind == EVOBOT_INTERACTOR_DOOR ||
			kind == EVOBOT_INTERACTOR_BUTTON || kind == EVOBOT_INTERACTOR_PLATFORM ||
			kind == EVOBOT_INTERACTOR_TRAIN) && entity->v->solid == SOLID_BSP;
		EvoBot_EZQ_CopyVector(entity->v->absmin, &interactor->bounds.mins);
		EvoBot_EZQ_CopyVector(entity->v->absmax, &interactor->bounds.maxs);
		interactor->swept_bounds = interactor->bounds;
		EvoBot_EZQ_CopyVector(entity->v->origin, &interactor->origin);
		classname = PR_GetEntityString(entity->v->classname);
		strlcpy(interactor->classname, classname, sizeof(interactor->classname));
		strlcpy(interactor->model, PR_GetEntityString(entity->v->model),
			sizeof(interactor->model));
		strlcpy(interactor->target, PR_GetEntityString(entity->v->target),
			sizeof(interactor->target));
		strlcpy(interactor->targetname, PR_GetEntityString(entity->v->targetname),
			sizeof(interactor->targetname));
		EvoBot_EZQ_CopyOptionalString(entity, "map", interactor->destination_map,
			sizeof(interactor->destination_map));

		if (interactor->dynamic_brush)
		{
			eval_t *pos1 = PR_GetEdictFieldValue(entity, "pos1");
			eval_t *pos2 = PR_GetEdictFieldValue(entity, "pos2");

			if (pos1)
				EvoBot_EZQ_ExpandSweptBounds(interactor, pos1->vector);
			if (pos2)
				EvoBot_EZQ_ExpandSweptBounds(interactor, pos2->vector);
		}
		return 1;
	}

	return 0;
}

static int EvoBot_EZQ_FileSize(const char *path, size_t *size)
{
	vfsfile_t *file;
	unsigned long length;

	if (!path || !size)
		return 0;
	file = FS_OpenVFS(path, "rb", FS_GAME_OS);
	if (!file)
		return 0;
	length = VFS_GETLEN(file);
	VFS_CLOSE(file);
	*size = (size_t)length;
	return 1;
}

static int EvoBot_EZQ_ReadFile(const char *path, void *data, size_t size)
{
	vfsfile_t *file;
	byte *destination = (byte *)data;
	size_t total = 0;

	if (!path || (!data && size))
		return 0;
	file = FS_OpenVFS(path, "rb", FS_GAME_OS);
	if (!file)
		return 0;
	while (total < size)
	{
		int request = (int)min(size - total, (size_t)INT_MAX);
		int received = VFS_READ(file, destination + total, request, NULL);

		if (received <= 0)
			break;
		total += (size_t)received;
	}
	VFS_CLOSE(file);
	return total == size;
}

static int EvoBot_EZQ_WriteFile(const char *path, const void *data, size_t size)
{
	if (!path || (!data && size) || size > INT_MAX)
		return 0;
	return FS_WriteFile(path, data, (int)size);
}

static int EvoBot_EZQ_FindClientSlot(evobot_client_handle_t handle)
{
	int i;

	if (handle == EVOBOT_CLIENT_HANDLE_INVALID)
		return -1;

	for (i = 0; i < MAX_CLIENTS; i++)
	{
		if (evobot_ezq_client_handles[i] == handle)
			return i;
	}

	return -1;
}

static evobot_client_handle_t EvoBot_EZQ_NewClientHandle(void)
{
	evobot_client_handle_t handle;

	do
	{
		handle = evobot_ezq_next_client_handle++;
		if (evobot_ezq_next_client_handle == EVOBOT_CLIENT_HANDLE_INVALID)
			evobot_ezq_next_client_handle++;
	}
	while (handle == EVOBOT_CLIENT_HANDLE_INVALID || EvoBot_EZQ_FindClientSlot(handle) >= 0);

	return handle;
}

static int EvoBot_EZQ_IsBotClientValid(evobot_client_handle_t handle)
{
#ifdef USE_PR2
	client_t *client;
	int slot = EvoBot_EZQ_FindClientSlot(handle);

	if (slot < 0)
		return 0;

	client = &svs.clients[slot];
	if (client->state == cs_free || !client->isBot || client->gamecodeBot ||
		client->edict != EDICT_NUM(slot + 1))
	{
		evobot_ezq_client_handles[slot] = EVOBOT_CLIENT_HANDLE_INVALID;
		return 0;
	}

	return 1;
#else
	(void)handle;
	return 0;
#endif
}

static evobot_create_bot_result_t EvoBot_EZQ_CreateBotClient(const char *name)
{
	evobot_create_bot_result_t result;
	int edictnum;
	int slot;

	result.status = EVOBOT_CREATE_BOT_UNAVAILABLE;
	result.handle = EVOBOT_CLIENT_HANDLE_INVALID;
	if (!evobot_ezq_initialized || !evobot_ezq_map_loaded)
		return result;

#ifdef USE_PR2
	edictnum = SV_AddBotClient(name, 0, 0, "base", false);
	if (!edictnum)
	{
		result.status = EVOBOT_CREATE_BOT_NO_FREE_SLOT;
		return result;
	}

	slot = edictnum - 1;
	if (slot < 0 || slot >= MAX_CLIENTS || evobot_ezq_client_handles[slot] != EVOBOT_CLIENT_HANDLE_INVALID)
	{
		if (slot >= 0 && slot < MAX_CLIENTS)
		{
			int old_self = pr_global_struct->self;

			RemoveBot(&svs.clients[slot]);
			pr_global_struct->self = old_self;
		}
		return result;
	}

	result.handle = EvoBot_EZQ_NewClientHandle();
	evobot_ezq_client_handles[slot] = result.handle;
	result.status = EVOBOT_CREATE_BOT_OK;
#else
	(void)name;
	(void)edictnum;
	(void)slot;
	result.status = EVOBOT_CREATE_BOT_UNSUPPORTED_GAMECODE;
#endif

	return result;
}

static int EvoBot_EZQ_RemoveBotClient(evobot_client_handle_t handle)
{
#ifdef USE_PR2
	int old_self;
	int slot = EvoBot_EZQ_FindClientSlot(handle);

	if (slot < 0 || !EvoBot_EZQ_IsBotClientValid(handle))
		return 0;

	old_self = pr_global_struct->self;
	RemoveBot(&svs.clients[slot]);
	pr_global_struct->self = old_self;
	evobot_ezq_client_handles[slot] = EVOBOT_CLIENT_HANDLE_INVALID;
	return 1;
#else
	(void)handle;
	return 0;
#endif
}

static void EvoBot_EZQ_Status_f(void)
{
	EvoBot_PrintStatus();
}

static void EvoBot_EZQ_Version_f(void)
{
	EvoBot_PrintVersion();
}

static void EvoBot_EZQ_Add_f(void)
{
	if (Cmd_Argc() != 2 || !Cmd_Argv(1)[0])
	{
		Con_Printf("usage: evobot_add <name>\n");
		return;
	}

	EvoBot_AddBot(Cmd_Argv(1));
}

static void EvoBot_EZQ_Remove_f(void)
{
	if (Cmd_Argc() != 2 || !Cmd_Argv(1)[0])
	{
		Con_Printf("usage: evobot_remove <name>\n");
		return;
	}

	EvoBot_RemoveBot(Cmd_Argv(1));
}

static void EvoBot_EZQ_NavGenerate_f(void)
{
	EvoBot_NavConvexGenerate();
}

static void EvoBot_EZQ_NavStatus_f(void)
{
	EvoBot_NavConvexPrintStatus();
}

static void EvoBot_EZQ_NavSave_f(void)
{
	EvoBot_NavConvexSave();
}

static void EvoBot_EZQ_NavLoad_f(void)
{
	if (Cmd_Argc() > 2)
	{
		Con_Printf("usage: evobot_nav_load [mapname]\n");
		return;
	}
	EvoBot_NavConvexLoad(Cmd_Argc() == 2 ? Cmd_Argv(1) : NULL);
}

static void EvoBot_EZQ_NavClear_f(void)
{
	EvoBot_NavConvexClear();
}

static void EvoBot_EZQ_NavExportObj_f(void)
{
	EvoBot_NavConvexExportObj();
}

void EvoBot_EZQ_Init(void)
{
	if (evobot_ezq_initialized)
		return;

	evobot_ezq_host.print = EvoBot_EZQ_Print;
	evobot_ezq_host.server_time = EvoBot_EZQ_ServerTime;
	evobot_ezq_host.create_bot_client = EvoBot_EZQ_CreateBotClient;
	evobot_ezq_host.remove_bot_client = EvoBot_EZQ_RemoveBotClient;
	evobot_ezq_host.is_bot_client_valid = EvoBot_EZQ_IsBotClientValid;
	evobot_ezq_host.monotonic_time = EvoBot_EZQ_MonotonicTime;
	evobot_ezq_host.world_bounds = EvoBot_EZQ_WorldBounds;
	evobot_ezq_host.player_bounds = EvoBot_EZQ_PlayerBounds;
	evobot_ezq_host.trace_player_world = EvoBot_EZQ_TracePlayerWorld;
	evobot_ezq_host.point_contents = EvoBot_EZQ_PointContents;
	evobot_ezq_host.collision_tree = EvoBot_EZQ_CollisionTree;
	evobot_ezq_host.collision_node = EvoBot_EZQ_CollisionNode;
	evobot_ezq_host.interactor_count = EvoBot_EZQ_InteractorCount;
	evobot_ezq_host.get_interactor = EvoBot_EZQ_GetInteractor;
	evobot_ezq_host.file_size = EvoBot_EZQ_FileSize;
	evobot_ezq_host.read_file = EvoBot_EZQ_ReadFile;
	evobot_ezq_host.write_file = EvoBot_EZQ_WriteFile;
	EvoBot_Init(&evobot_ezq_host);

	Cmd_AddCommand("evobot_status", EvoBot_EZQ_Status_f);
	Cmd_AddCommand("evobot_version", EvoBot_EZQ_Version_f);
	Cmd_AddCommand("evobot_add", EvoBot_EZQ_Add_f);
	Cmd_AddCommand("evobot_remove", EvoBot_EZQ_Remove_f);
	Cmd_AddCommand("evobot_nav_generate", EvoBot_EZQ_NavGenerate_f);
	Cmd_AddCommand("evobot_nav_status", EvoBot_EZQ_NavStatus_f);
	Cmd_AddCommand("evobot_nav_save", EvoBot_EZQ_NavSave_f);
	Cmd_AddCommand("evobot_nav_load", EvoBot_EZQ_NavLoad_f);
	Cmd_AddCommand("evobot_nav_clear", EvoBot_EZQ_NavClear_f);
	Cmd_AddCommand("evobot_nav_export_obj", EvoBot_EZQ_NavExportObj_f);
	evobot_ezq_initialized = 1;
}

void EvoBot_EZQ_MapLoaded(void)
{
	evobot_map_info_t map;

	if (!evobot_ezq_initialized || evobot_ezq_map_loaded || sv.state != ss_active)
		return;

	map.name = sv.mapname;
	map.checksum = sv.map_checksum;
	EvoBot_MapLoaded(&map);
	evobot_ezq_map_loaded = 1;
}

void EvoBot_EZQ_Frame(void)
{
	if (!evobot_ezq_initialized)
		return;

	EvoBot_Frame(sv.time);
}

void EvoBot_EZQ_PrepareBotCommands(double frame_time)
{
#ifdef USE_PR2
	double command_msec;
	int msec;
	int i;

	if (!evobot_ezq_initialized || !evobot_ezq_map_loaded)
		return;

	command_msec = frame_time * 1000.0 + evobot_ezq_command_msec_remainder;
	msec = (int)command_msec;
	evobot_ezq_command_msec_remainder = command_msec - msec;
	msec = bound(1, msec, 255);

	for (i = 0; i < MAX_CLIENTS; i++)
	{
		client_t *client;

		if (evobot_ezq_client_handles[i] == EVOBOT_CLIENT_HANDLE_INVALID ||
			!EvoBot_EZQ_IsBotClientValid(evobot_ezq_client_handles[i]))
			continue;

		client = &svs.clients[i];
		memset(&client->botcmd, 0, sizeof(client->botcmd));
		client->botcmd.msec = (byte)msec;
		VectorCopy(client->edict->v->v_angle, client->botcmd.angles);

		if (client->edict->v->fixangle)
		{
			VectorCopy(client->edict->v->angles, client->botcmd.angles);
			client->botcmd.angles[PITCH] *= -3;
			client->edict->v->fixangle = 0;
		}
	}
#else
	(void)frame_time;
#endif
}

void EvoBot_EZQ_MapCleared(void)
{
	if (!evobot_ezq_initialized || !evobot_ezq_map_loaded)
		return;

	EvoBot_MapCleared();
	memset(evobot_ezq_client_handles, 0, sizeof(evobot_ezq_client_handles));
	evobot_ezq_command_msec_remainder = 0;
	evobot_ezq_map_loaded = 0;
}

void EvoBot_EZQ_Shutdown(void)
{
	if (!evobot_ezq_initialized)
		return;

	EvoBot_EZQ_MapCleared();
	EvoBot_Shutdown();
	memset(&evobot_ezq_host, 0, sizeof(evobot_ezq_host));
	memset(evobot_ezq_client_handles, 0, sizeof(evobot_ezq_client_handles));
	evobot_ezq_command_msec_remainder = 0;
	evobot_ezq_initialized = 0;
}
