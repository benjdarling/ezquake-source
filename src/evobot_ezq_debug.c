#include "quakedef.h"
#include "qwsvdef.h"

#include <evobot/evobot.h>

#include "evobot_ezq_debug.h"
#include "r_debug_primitive.h"

typedef enum evobot_ezq_debug_layer_e
{
	EVOBOT_EZQ_DEBUG_AREA,
	EVOBOT_EZQ_DEBUG_PORTAL,
	EVOBOT_EZQ_DEBUG_LEDGE,
	EVOBOT_EZQ_DEBUG_LIQUID,
	EVOBOT_EZQ_DEBUG_INTERACTOR,
	EVOBOT_EZQ_DEBUG_REACH_WALK,
	EVOBOT_EZQ_DEBUG_REACH_DROP,
	EVOBOT_EZQ_DEBUG_REACH_SWIM,
	EVOBOT_EZQ_DEBUG_ROUTE,
	EVOBOT_EZQ_DEBUG_ROUTE_BLOCKED,
	EVOBOT_EZQ_DEBUG_ROUTE_CONDITIONAL,
	EVOBOT_EZQ_DEBUG_ROUTE_FRONTIER,
	EVOBOT_EZQ_DEBUG_PROBLEM_REACHABLE,
	EVOBOT_EZQ_DEBUG_PROBLEM_ADJACENT,
	EVOBOT_EZQ_DEBUG_PROBLEM_PORTAL,
	EVOBOT_EZQ_DEBUG_PROBLEM_GROUND_SOURCE,
	EVOBOT_EZQ_DEBUG_PROBLEM_GROUND_DESTINATION,
	EVOBOT_EZQ_DEBUG_PROBLEM_CROSSING,
	EVOBOT_EZQ_DEBUG_PROBLEM_START,
	EVOBOT_EZQ_DEBUG_PROBLEM_DESIRED,
	EVOBOT_EZQ_DEBUG_PROBLEM_ACTUAL,
	EVOBOT_EZQ_DEBUG_PROBLEM_PATH
} evobot_ezq_debug_layer_t;

typedef struct evobot_ezq_debug_line_s
{
	float start[3];
	float end[3];
	uint32_t area_a;
	uint32_t area_b;
	evobot_ezq_debug_layer_t layer;
	evobot_contents_t contents;
	evobot_interactor_kind_t interactor_kind;
	evobot_nav_travel_type_t travel_type;
	int supported;
} evobot_ezq_debug_line_t;

typedef struct evobot_ezq_debug_triangle_s
{
	float vertices[3][3];
	evobot_ezq_debug_layer_t layer;
} evobot_ezq_debug_triangle_t;

static cvar_t evobot_nav_show = { "evobot_nav_show", "0" };
static cvar_t evobot_nav_show_areas = { "evobot_nav_show_areas", "0" };
static cvar_t evobot_nav_show_portals = { "evobot_nav_show_portals", "0" };
static cvar_t evobot_nav_show_ledges = { "evobot_nav_show_ledges", "0" };
static cvar_t evobot_nav_show_liquids = { "evobot_nav_show_liquids", "0" };
static cvar_t evobot_nav_show_interactors = { "evobot_nav_show_interactors", "0" };
static cvar_t evobot_nav_show_reach = { "evobot_nav_show_reach", "0" };
static cvar_t evobot_nav_show_walk = { "evobot_nav_show_walk", "1" };
static cvar_t evobot_nav_show_drop = { "evobot_nav_show_drop", "1" };
static cvar_t evobot_nav_show_swim = { "evobot_nav_show_swim", "1" };
static cvar_t evobot_nav_show_route = { "evobot_nav_show_route", "0" };
static cvar_t evobot_nav_show_problem = { "evobot_nav_show_problem", "0" };
static cvar_t evobot_nav_show_problem_xray = {
	"evobot_nav_show_problem_xray", "0"
};
static cvar_t evobot_nav_show_radius = { "evobot_nav_show_radius", "1024" };

static evobot_ezq_debug_line_t *evobot_ezq_debug_lines;
static size_t evobot_ezq_debug_line_count;
static size_t evobot_ezq_debug_line_capacity;
static evobot_ezq_debug_triangle_t *evobot_ezq_debug_triangles;
static size_t evobot_ezq_debug_triangle_count;
static size_t evobot_ezq_debug_triangle_capacity;
static r_debug_line_vertex_t *evobot_ezq_debug_vertices;
static size_t evobot_ezq_debug_vertex_capacity;
static r_debug_line_vertex_t *evobot_ezq_debug_triangle_vertices;
static size_t evobot_ezq_debug_triangle_vertex_capacity;
static uint64_t evobot_ezq_debug_revision;
static uint64_t evobot_ezq_debug_route_revision;
static uint32_t evobot_ezq_debug_selected_area;
static uint32_t evobot_ezq_debug_selected_portal;
static uint64_t evobot_ezq_debug_selected_nav_revision;
static uint64_t evobot_ezq_debug_selected_route_revision;

static void EvoBot_EZQ_DebugSegmentCenter(const evobot_nav_segment_t *segment,
	evobot_vec3_t *center);

static const char *EvoBot_EZQ_DebugContentsName(evobot_contents_t contents)
{
	switch (contents)
	{
	case EVOBOT_CONTENTS_SOLID:
		return "solid";
	case EVOBOT_CONTENTS_AIR:
		return "air";
	case EVOBOT_CONTENTS_WATER:
		return "water";
	case EVOBOT_CONTENTS_SLIME:
		return "slime";
	case EVOBOT_CONTENTS_LAVA:
		return "lava";
	default:
		return "other";
	}
}

static void EvoBot_EZQ_DebugClearCache(void)
{
	Q_free(evobot_ezq_debug_lines);
	Q_free(evobot_ezq_debug_triangles);
	Q_free(evobot_ezq_debug_vertices);
	Q_free(evobot_ezq_debug_triangle_vertices);
	evobot_ezq_debug_lines = NULL;
	evobot_ezq_debug_triangles = NULL;
	evobot_ezq_debug_vertices = NULL;
	evobot_ezq_debug_triangle_vertices = NULL;
	evobot_ezq_debug_line_count = 0;
	evobot_ezq_debug_triangle_count = 0;
	evobot_ezq_debug_line_capacity = 0;
	evobot_ezq_debug_triangle_capacity = 0;
	evobot_ezq_debug_vertex_capacity = 0;
	evobot_ezq_debug_triangle_vertex_capacity = 0;
	evobot_ezq_debug_revision = 0;
	evobot_ezq_debug_route_revision = 0;
}

static void EvoBot_EZQ_DebugAddLine(const evobot_vec3_t *start,
	const evobot_vec3_t *end, uint32_t area_a, uint32_t area_b,
	evobot_ezq_debug_layer_t layer, evobot_contents_t contents,
	evobot_interactor_kind_t interactor_kind, int supported)
{
	evobot_ezq_debug_line_t *line;

	if (evobot_ezq_debug_line_count == evobot_ezq_debug_line_capacity)
	{
		size_t new_capacity = evobot_ezq_debug_line_capacity ?
			evobot_ezq_debug_line_capacity * 2 : 4096;

		evobot_ezq_debug_lines = Q_realloc(evobot_ezq_debug_lines,
			new_capacity * sizeof(*evobot_ezq_debug_lines));
		evobot_ezq_debug_line_capacity = new_capacity;
	}

	line = &evobot_ezq_debug_lines[evobot_ezq_debug_line_count++];
	line->start[0] = start->v[0];
	line->start[1] = start->v[1];
	line->start[2] = start->v[2] +
		(layer >= EVOBOT_EZQ_DEBUG_ROUTE ? 1.0f : 0.125f);
	line->end[0] = end->v[0];
	line->end[1] = end->v[1];
	line->end[2] = end->v[2] +
		(layer >= EVOBOT_EZQ_DEBUG_ROUTE ? 1.0f : 0.125f);
	line->area_a = area_a;
	line->area_b = area_b;
	line->layer = layer;
	line->contents = contents;
	line->interactor_kind = interactor_kind;
	line->supported = supported;
}

static void EvoBot_EZQ_DebugAddTriangle(const evobot_vec3_t *a,
	const evobot_vec3_t *b, const evobot_vec3_t *c,
	evobot_ezq_debug_layer_t layer)
{
	evobot_ezq_debug_triangle_t *triangle;

	if (evobot_ezq_debug_triangle_count == evobot_ezq_debug_triangle_capacity)
	{
		size_t new_capacity = evobot_ezq_debug_triangle_capacity ?
			evobot_ezq_debug_triangle_capacity * 2 : 256;

		evobot_ezq_debug_triangles = Q_realloc(evobot_ezq_debug_triangles,
			new_capacity * sizeof(*evobot_ezq_debug_triangles));
		evobot_ezq_debug_triangle_capacity = new_capacity;
	}
	triangle = &evobot_ezq_debug_triangles[evobot_ezq_debug_triangle_count++];
	memcpy(triangle->vertices[0], a->v, sizeof(triangle->vertices[0]));
	memcpy(triangle->vertices[1], b->v, sizeof(triangle->vertices[1]));
	memcpy(triangle->vertices[2], c->v, sizeof(triangle->vertices[2]));
	triangle->layer = layer;
}

static void EvoBot_EZQ_DebugAddProblemLine(const evobot_vec3_t *start,
	const evobot_vec3_t *end, uint32_t source_area, uint32_t destination_area,
	evobot_ezq_debug_layer_t layer)
{
	EvoBot_EZQ_DebugAddLine(start, end, source_area, destination_area, layer,
		EVOBOT_CONTENTS_OTHER, EVOBOT_INTERACTOR_OTHER, 1);
}

static void EvoBot_EZQ_DebugAddMarker(const evobot_vec3_t *point,
	uint32_t source_area, uint32_t destination_area,
	evobot_ezq_debug_layer_t layer)
{
	evobot_vec3_t start = *point;
	evobot_vec3_t end = *point;
	int axis;

	for (axis = 0; axis < 3; axis++)
	{
		start = *point;
		end = *point;
		start.v[axis] -= 5.0f;
		end.v[axis] += 5.0f;
		EvoBot_EZQ_DebugAddProblemLine(&start, &end, source_area,
			destination_area, layer);
	}
}

static int EvoBot_EZQ_DebugAreaIndex(uint32_t area_id, size_t area_count,
	size_t *area_index, evobot_nav_debug_area_t *area)
{
	size_t i;

	for (i = 0; i < area_count; i++)
	{
		if (EvoBot_NavDebugArea(i, area) && area->id == area_id)
		{
			*area_index = i;
			return 1;
		}
	}
	return 0;
}

static int EvoBot_EZQ_DebugAddProblemArea(uint32_t area_id,
	size_t area_count, evobot_ezq_debug_layer_t area_layer,
	evobot_ezq_debug_layer_t ground_layer)
{
	evobot_nav_debug_area_t area;
	size_t area_index;
	size_t face_index;

	if (!EvoBot_EZQ_DebugAreaIndex(area_id, area_count, &area_index, &area))
		return 0;
	for (face_index = 0; face_index < area.face_count; face_index++)
	{
		evobot_nav_debug_face_t face;
		evobot_vec3_t first;
		size_t vertex_index;

		if (!EvoBot_NavDebugAreaFace(area_index, face_index, &face) ||
			face.vertex_count < 3 ||
			!EvoBot_NavDebugAreaFaceVertex(area_index, face_index, 0, &first))
			return 0;
		for (vertex_index = 0; vertex_index < face.vertex_count; vertex_index++)
		{
			evobot_vec3_t start;
			evobot_vec3_t end;
			evobot_ezq_debug_layer_t line_layer =
				face.normal.v[2] < -0.7f ? ground_layer : area_layer;

			if (!EvoBot_NavDebugAreaFaceVertex(area_index, face_index,
				vertex_index, &start) ||
				!EvoBot_NavDebugAreaFaceVertex(area_index, face_index,
					(vertex_index + 1) % face.vertex_count, &end))
				return 0;
			EvoBot_EZQ_DebugAddProblemLine(&start, &end, area_id, 0,
				line_layer);
		}
		for (vertex_index = 1; vertex_index + 1 < face.vertex_count;
			vertex_index++)
		{
			evobot_vec3_t b;
			evobot_vec3_t c;

			if (!EvoBot_NavDebugAreaFaceVertex(area_index, face_index,
				vertex_index, &b) ||
				!EvoBot_NavDebugAreaFaceVertex(area_index, face_index,
					vertex_index + 1, &c))
				return 0;
			EvoBot_EZQ_DebugAddTriangle(&first, &b, &c, area_layer);
		}
	}
	return 1;
}

static int EvoBot_EZQ_DebugAddProblemPortal(uint32_t portal_id,
	uint32_t source_area, uint32_t destination_area)
{
	evobot_nav_debug_portal_t portal;
	evobot_vec3_t first;
	size_t vertex_index;

	if (!portal_id || !EvoBot_NavDebugPortal(portal_id - 1, &portal) ||
		portal.vertex_count < 3 ||
		!EvoBot_NavDebugPortalVertex(portal_id - 1, 0, &first))
		return 0;
	for (vertex_index = 0; vertex_index < portal.vertex_count; vertex_index++)
	{
		evobot_vec3_t start;
		evobot_vec3_t end;

		if (!EvoBot_NavDebugPortalVertex(portal_id - 1, vertex_index, &start) ||
			!EvoBot_NavDebugPortalVertex(portal_id - 1,
				(vertex_index + 1) % portal.vertex_count, &end))
			return 0;
		EvoBot_EZQ_DebugAddProblemLine(&start, &end, source_area,
			destination_area, EVOBOT_EZQ_DEBUG_PROBLEM_PORTAL);
	}
	for (vertex_index = 1; vertex_index + 1 < portal.vertex_count; vertex_index++)
	{
		evobot_vec3_t b;
		evobot_vec3_t c;

		if (!EvoBot_NavDebugPortalVertex(portal_id - 1, vertex_index, &b) ||
			!EvoBot_NavDebugPortalVertex(portal_id - 1, vertex_index + 1, &c))
			return 0;
		EvoBot_EZQ_DebugAddTriangle(&first, &b, &c,
			EVOBOT_EZQ_DEBUG_PROBLEM_PORTAL);
	}
	return 1;
}

static void EvoBot_EZQ_DebugAddProblemCandidate(uint32_t portal_id,
	uint32_t source_area, uint32_t destination_area)
{
	evobot_nav_debug_walk_candidate_t candidate;
	evobot_vec3_t start_center;
	evobot_vec3_t destination_center;

	if (!EvoBot_NavDebugWalkCandidate(portal_id, source_area,
		destination_area, &candidate) || !candidate.present)
		return;
	EvoBot_EZQ_DebugAddProblemLine(&candidate.start.start,
		&candidate.start.end, source_area, destination_area,
		EVOBOT_EZQ_DEBUG_PROBLEM_CROSSING);
	EvoBot_EZQ_DebugAddProblemLine(&candidate.destination.start,
		&candidate.destination.end, source_area, destination_area,
		EVOBOT_EZQ_DEBUG_PROBLEM_CROSSING);
	EvoBot_EZQ_DebugSegmentCenter(&candidate.start, &start_center);
	EvoBot_EZQ_DebugSegmentCenter(&candidate.destination, &destination_center);
	EvoBot_EZQ_DebugAddProblemLine(&start_center, &destination_center,
		source_area, destination_area, EVOBOT_EZQ_DEBUG_PROBLEM_CROSSING);
	EvoBot_EZQ_DebugAddMarker(&candidate.start_origin, source_area,
		destination_area, EVOBOT_EZQ_DEBUG_PROBLEM_START);
	EvoBot_EZQ_DebugAddMarker(&candidate.desired_destination, source_area,
		destination_area, EVOBOT_EZQ_DEBUG_PROBLEM_DESIRED);
	EvoBot_EZQ_DebugAddProblemLine(&candidate.start_origin,
		&candidate.desired_destination, source_area, destination_area,
		EVOBOT_EZQ_DEBUG_PROBLEM_PATH);
	if (candidate.movement_succeeded)
	{
		EvoBot_EZQ_DebugAddMarker(&candidate.result.state.origin, source_area,
			destination_area, EVOBOT_EZQ_DEBUG_PROBLEM_ACTUAL);
		EvoBot_EZQ_DebugAddProblemLine(&candidate.start_origin,
			&candidate.result.state.origin, source_area, destination_area,
			EVOBOT_EZQ_DEBUG_PROBLEM_ACTUAL);
	}
}

static void EvoBot_EZQ_DebugSelectedPortalDirection(
	const evobot_nav_debug_summary_t *summary,
	const evobot_nav_debug_portal_t *portal, uint32_t *source_area,
	uint32_t *destination_area)
{
	size_t i;

	if (evobot_ezq_debug_selected_area == portal->area_a ||
		evobot_ezq_debug_selected_area == portal->area_b)
	{
		*source_area = evobot_ezq_debug_selected_area;
		*destination_area = *source_area == portal->area_a ?
			portal->area_b : portal->area_a;
		return;
	}
	for (i = 0; i < summary->reachability_count; i++)
	{
		evobot_nav_reachability_t reachability;

		if (EvoBot_NavDebugReachability(i, &reachability) &&
			reachability.portal_id == portal->id &&
			reachability.travel_type == EVOBOT_NAV_TRAVEL_WALK)
		{
			*source_area = reachability.source_area;
			*destination_area = reachability.destination_area;
			return;
		}
	}
	*source_area = portal->area_a;
	*destination_area = portal->area_b;
}

static int EvoBot_EZQ_DebugAddProblem(
	const evobot_nav_debug_summary_t *summary,
	const evobot_nav_route_summary_t *route_summary)
{
	evobot_nav_debug_portal_t portal;
	uint32_t portal_id = 0;
	uint32_t source_area = 0;
	uint32_t destination_area = 0;

	if (evobot_ezq_debug_selected_portal)
		portal_id = evobot_ezq_debug_selected_portal;
	else if (route_summary &&
		route_summary->result == EVOBOT_NAV_ROUTE_UNREACHABLE)
	{
		portal_id = route_summary->frontier_portal;
		source_area = route_summary->frontier_area;
		destination_area = route_summary->frontier_gap_area;
	}
	if (!portal_id || !EvoBot_NavDebugPortal(portal_id - 1, &portal))
		return 1;
	if (!source_area || !destination_area)
		EvoBot_EZQ_DebugSelectedPortalDirection(summary, &portal,
			&source_area, &destination_area);
	if (!EvoBot_EZQ_DebugAddProblemArea(source_area, summary->area_count,
		EVOBOT_EZQ_DEBUG_PROBLEM_REACHABLE,
		EVOBOT_EZQ_DEBUG_PROBLEM_GROUND_SOURCE) ||
		!EvoBot_EZQ_DebugAddProblemArea(destination_area, summary->area_count,
			EVOBOT_EZQ_DEBUG_PROBLEM_ADJACENT,
			EVOBOT_EZQ_DEBUG_PROBLEM_GROUND_DESTINATION) ||
		!EvoBot_EZQ_DebugAddProblemPortal(portal_id, source_area,
			destination_area))
		return 0;
	EvoBot_EZQ_DebugAddProblemCandidate(portal_id, source_area,
		destination_area);
	return 1;
}

static void EvoBot_EZQ_DebugAddBounds(const evobot_bounds_t *bounds,
	evobot_interactor_kind_t kind)
{
	static const int edges[12][2] = {
		{ 0, 1 }, { 1, 3 }, { 3, 2 }, { 2, 0 },
		{ 4, 5 }, { 5, 7 }, { 7, 6 }, { 6, 4 },
		{ 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 }
	};
	evobot_vec3_t corners[8];
	int i;

	for (i = 0; i < 8; ++i)
	{
		corners[i].v[0] = (i & 1) ? bounds->maxs.v[0] : bounds->mins.v[0];
		corners[i].v[1] = (i & 2) ? bounds->maxs.v[1] : bounds->mins.v[1];
		corners[i].v[2] = (i & 4) ? bounds->maxs.v[2] : bounds->mins.v[2];
	}
	for (i = 0; i < 12; ++i)
	{
		EvoBot_EZQ_DebugAddLine(&corners[edges[i][0]], &corners[edges[i][1]],
			0, 0, EVOBOT_EZQ_DEBUG_INTERACTOR, EVOBOT_CONTENTS_OTHER,
			kind, 0);
	}
}

static void EvoBot_EZQ_DebugAddReachLine(const evobot_vec3_t *start,
	const evobot_vec3_t *end, const evobot_nav_reachability_t *reachability,
	evobot_ezq_debug_layer_t layer)
{
	EvoBot_EZQ_DebugAddLine(start, end, reachability->source_area,
		reachability->destination_area, layer, reachability->destination_contents,
		EVOBOT_INTERACTOR_OTHER, 1);
	evobot_ezq_debug_lines[evobot_ezq_debug_line_count - 1].travel_type =
		reachability->travel_type;
}

static void EvoBot_EZQ_DebugSegmentCenter(const evobot_nav_segment_t *segment,
	evobot_vec3_t *center)
{
	int axis;

	for (axis = 0; axis < 3; axis++)
		center->v[axis] = (segment->start.v[axis] + segment->end.v[axis]) * 0.5f;
}

static void EvoBot_EZQ_DebugAddReachability(
	const evobot_nav_reachability_t *reachability, int requested_layer)
{
	evobot_ezq_debug_layer_t layer;
	evobot_vec3_t start;
	evobot_vec3_t end;
	evobot_vec3_t direction;
	evobot_vec3_t side;
	evobot_vec3_t arrow_a;
	evobot_vec3_t arrow_b;
	float length;
	int axis;

	if (requested_layer >= 0)
		layer = (evobot_ezq_debug_layer_t)requested_layer;
	else if (reachability->travel_type == EVOBOT_NAV_TRAVEL_WALK)
		layer = EVOBOT_EZQ_DEBUG_REACH_WALK;
	else if (reachability->travel_type == EVOBOT_NAV_TRAVEL_DROP)
		layer = EVOBOT_EZQ_DEBUG_REACH_DROP;
	else
		layer = EVOBOT_EZQ_DEBUG_REACH_SWIM;
	EvoBot_EZQ_DebugAddReachLine(&reachability->start.start,
		&reachability->start.end, reachability, layer);
	EvoBot_EZQ_DebugSegmentCenter(&reachability->start, &start);
	EvoBot_EZQ_DebugSegmentCenter(&reachability->destination, &end);
	EvoBot_EZQ_DebugAddReachLine(&start, &end, reachability, layer);
	for (axis = 0; axis < 3; axis++)
		direction.v[axis] = end.v[axis] - start.v[axis];
	length = sqrtf(direction.v[0] * direction.v[0] +
		direction.v[1] * direction.v[1] + direction.v[2] * direction.v[2]);
	if (length < 0.001f)
		return;
	for (axis = 0; axis < 3; axis++)
		direction.v[axis] /= length;
	side.v[0] = -direction.v[1];
	side.v[1] = direction.v[0];
	side.v[2] = 0;
	length = sqrtf(side.v[0] * side.v[0] + side.v[1] * side.v[1]);
	if (length < 0.001f)
	{
		side.v[0] = 1;
		side.v[1] = 0;
	}
	else
	{
		side.v[0] /= length;
		side.v[1] /= length;
	}
	for (axis = 0; axis < 3; axis++)
	{
		arrow_a.v[axis] = end.v[axis] - direction.v[axis] * 6.0f +
			side.v[axis] * 3.0f;
		arrow_b.v[axis] = end.v[axis] - direction.v[axis] * 6.0f -
			side.v[axis] * 3.0f;
	}
	EvoBot_EZQ_DebugAddReachLine(&end, &arrow_a, reachability, layer);
	EvoBot_EZQ_DebugAddReachLine(&end, &arrow_b, reachability, layer);
}

static int EvoBot_EZQ_DebugBuildCache(const evobot_nav_debug_summary_t *summary,
	const evobot_nav_route_summary_t *route_summary)
{
	size_t i;

	evobot_ezq_debug_line_count = 0;
	evobot_ezq_debug_triangle_count = 0;

	for (i = 0; i < summary->area_count; ++i)
	{
		evobot_nav_debug_area_t area;
		size_t face_index;

		if (!EvoBot_NavDebugArea(i, &area))
			return 0;
		for (face_index = 0; face_index < area.face_count; ++face_index)
		{
			evobot_nav_debug_face_t face;
			size_t vertex_index;

			if (!EvoBot_NavDebugAreaFace(i, face_index, &face))
				return 0;
			for (vertex_index = 0; vertex_index < face.vertex_count; ++vertex_index)
			{
				evobot_vec3_t start;
				evobot_vec3_t end;

				if (!EvoBot_NavDebugAreaFaceVertex(i, face_index, vertex_index, &start) ||
					!EvoBot_NavDebugAreaFaceVertex(i, face_index,
						(vertex_index + 1) % face.vertex_count, &end))
					return 0;
				EvoBot_EZQ_DebugAddLine(&start, &end, area.id, 0,
					EVOBOT_EZQ_DEBUG_AREA, area.contents,
					EVOBOT_INTERACTOR_OTHER, area.supported);
			}
		}
	}

	for (i = 0; i < summary->portal_count; ++i)
	{
		evobot_nav_debug_portal_t portal;
		evobot_ezq_debug_layer_t layer;
		size_t vertex_index;

		if (!EvoBot_NavDebugPortal(i, &portal))
			return 0;
		layer = portal.kind == EVOBOT_NAV_DEBUG_FACE_LEDGE ? EVOBOT_EZQ_DEBUG_LEDGE :
			portal.kind == EVOBOT_NAV_DEBUG_FACE_LIQUID ? EVOBOT_EZQ_DEBUG_LIQUID :
			EVOBOT_EZQ_DEBUG_PORTAL;
		for (vertex_index = 0; vertex_index < portal.vertex_count; ++vertex_index)
		{
			evobot_vec3_t start;
			evobot_vec3_t end;

			if (!EvoBot_NavDebugPortalVertex(i, vertex_index, &start) ||
				!EvoBot_NavDebugPortalVertex(i,
					(vertex_index + 1) % portal.vertex_count, &end))
				return 0;
			EvoBot_EZQ_DebugAddLine(&start, &end, portal.area_a, portal.area_b,
				layer, EVOBOT_CONTENTS_OTHER, EVOBOT_INTERACTOR_OTHER, 1);
		}
	}

	for (i = 0; i < summary->interactor_count; ++i)
	{
		evobot_nav_debug_interactor_t interactor;

		if (!EvoBot_NavDebugInteractor(i, &interactor))
			return 0;
		EvoBot_EZQ_DebugAddBounds(&interactor.bounds, interactor.kind);
	}

	for (i = 0; i < summary->reachability_count; ++i)
	{
		evobot_nav_reachability_t reachability;

		if (!EvoBot_NavDebugReachability(i, &reachability))
			return 0;
		EvoBot_EZQ_DebugAddReachability(&reachability, -1);
	}
	if (route_summary && route_summary->result != EVOBOT_NAV_ROUTE_NONE)
	{
		for (i = 0; i < route_summary->route_length; ++i)
		{
			evobot_nav_route_step_t step;
			evobot_nav_reachability_t reachability;
			evobot_ezq_debug_layer_t layer = EVOBOT_EZQ_DEBUG_ROUTE;

			if (!EvoBot_NavRouteDebugStep(i, &step, &reachability))
				return 0;
			if (step.segment_kind == EVOBOT_NAV_ROUTE_SEGMENT_BLOCKED)
				layer = EVOBOT_EZQ_DEBUG_ROUTE_BLOCKED;
			else if (step.segment_kind == EVOBOT_NAV_ROUTE_SEGMENT_CONDITIONAL)
				layer = EVOBOT_EZQ_DEBUG_ROUTE_CONDITIONAL;
			EvoBot_EZQ_DebugAddReachability(&reachability, layer);
		}
		{
			evobot_nav_route_step_t step;
			evobot_nav_reachability_t reachability;

			if (EvoBot_NavRouteDebugFrontier(&step, &reachability))
				EvoBot_EZQ_DebugAddReachability(&reachability,
					EVOBOT_EZQ_DEBUG_ROUTE_FRONTIER);
			else if (route_summary->frontier_portal)
			{
				evobot_nav_debug_portal_t portal;
				size_t vertex_index;

				if (!EvoBot_NavDebugPortal(route_summary->frontier_portal - 1,
					&portal))
					return 0;
				for (vertex_index = 0; vertex_index < portal.vertex_count;
					vertex_index++)
				{
					evobot_vec3_t start;
					evobot_vec3_t end;

					if (!EvoBot_NavDebugPortalVertex(portal.id - 1,
						vertex_index, &start) ||
						!EvoBot_NavDebugPortalVertex(portal.id - 1,
							(vertex_index + 1) % portal.vertex_count, &end))
						return 0;
					EvoBot_EZQ_DebugAddLine(&start, &end, portal.area_a,
						portal.area_b, EVOBOT_EZQ_DEBUG_ROUTE_FRONTIER,
						EVOBOT_CONTENTS_OTHER, EVOBOT_INTERACTOR_OTHER, 1);
				}
			}
		}
	}
	if (!EvoBot_EZQ_DebugAddProblem(summary, route_summary))
		return 0;

	evobot_ezq_debug_revision = summary->revision;
	evobot_ezq_debug_route_revision = route_summary ?
		route_summary->route_revision : 0;
	return 1;
}

static int EvoBot_EZQ_DebugLineEnabled(const evobot_ezq_debug_line_t *line)
{
	switch (line->layer)
	{
	case EVOBOT_EZQ_DEBUG_AREA:
		if (line->contents == EVOBOT_CONTENTS_WATER ||
			line->contents == EVOBOT_CONTENTS_SLIME ||
			line->contents == EVOBOT_CONTENTS_LAVA)
			return evobot_nav_show_liquids.integer;
		return evobot_nav_show_areas.integer;
	case EVOBOT_EZQ_DEBUG_PORTAL:
		return evobot_nav_show_portals.integer;
	case EVOBOT_EZQ_DEBUG_LEDGE:
		return evobot_nav_show_ledges.integer;
	case EVOBOT_EZQ_DEBUG_LIQUID:
		return evobot_nav_show_liquids.integer;
	case EVOBOT_EZQ_DEBUG_INTERACTOR:
		return evobot_nav_show_interactors.integer;
	case EVOBOT_EZQ_DEBUG_REACH_WALK:
		return evobot_nav_show_reach.integer && evobot_nav_show_walk.integer;
	case EVOBOT_EZQ_DEBUG_REACH_DROP:
		return evobot_nav_show_reach.integer && evobot_nav_show_drop.integer;
	case EVOBOT_EZQ_DEBUG_REACH_SWIM:
		return evobot_nav_show_reach.integer && evobot_nav_show_swim.integer;
	case EVOBOT_EZQ_DEBUG_ROUTE:
	case EVOBOT_EZQ_DEBUG_ROUTE_BLOCKED:
	case EVOBOT_EZQ_DEBUG_ROUTE_CONDITIONAL:
	case EVOBOT_EZQ_DEBUG_ROUTE_FRONTIER:
		return evobot_nav_show_route.integer;
	case EVOBOT_EZQ_DEBUG_PROBLEM_REACHABLE:
	case EVOBOT_EZQ_DEBUG_PROBLEM_ADJACENT:
	case EVOBOT_EZQ_DEBUG_PROBLEM_PORTAL:
	case EVOBOT_EZQ_DEBUG_PROBLEM_GROUND_SOURCE:
	case EVOBOT_EZQ_DEBUG_PROBLEM_GROUND_DESTINATION:
	case EVOBOT_EZQ_DEBUG_PROBLEM_CROSSING:
	case EVOBOT_EZQ_DEBUG_PROBLEM_START:
	case EVOBOT_EZQ_DEBUG_PROBLEM_DESIRED:
	case EVOBOT_EZQ_DEBUG_PROBLEM_ACTUAL:
	case EVOBOT_EZQ_DEBUG_PROBLEM_PATH:
		return evobot_nav_show_problem.integer;
	default:
		return 0;
	}
}

static void EvoBot_EZQ_DebugLineColor(const evobot_ezq_debug_line_t *line,
	unsigned char color[4])
{
	static const unsigned char area_supported[4] = { 64, 255, 96, 255 };
	static const unsigned char area_unsupported[4] = { 255, 160, 48, 255 };
	static const unsigned char portal[4] = { 48, 224, 255, 255 };
	static const unsigned char ledge[4] = { 255, 48, 224, 255 };
	static const unsigned char water[4] = { 48, 112, 255, 255 };
	static const unsigned char slime[4] = { 96, 255, 48, 255 };
	static const unsigned char lava[4] = { 255, 64, 16, 255 };
	static const unsigned char door[4] = { 255, 224, 48, 255 };
	static const unsigned char teleporter[4] = { 192, 64, 255, 255 };
	static const unsigned char destination[4] = { 255, 255, 255, 255 };
	static const unsigned char level_exit[4] = { 255, 96, 96, 255 };
	static const unsigned char interactor[4] = { 255, 176, 64, 255 };
	static const unsigned char reach_walk[4] = { 255, 240, 64, 255 };
	static const unsigned char reach_drop[4] = { 255, 64, 192, 255 };
	static const unsigned char reach_swim[4] = { 64, 160, 255, 255 };
	static const unsigned char reach_water_entry[4] = { 64, 255, 255, 255 };
	static const unsigned char reach_water_exit[4] = { 192, 255, 255, 255 };
	static const unsigned char reach_unresolved[4] = { 255, 96, 64, 255 };
	static const unsigned char route[4] = { 64, 255, 160, 255 };
	static const unsigned char route_blocked[4] = { 255, 48, 48, 255 };
	static const unsigned char route_conditional[4] = { 255, 160, 32, 255 };
	static const unsigned char route_frontier[4] = { 255, 64, 255, 255 };
	static const unsigned char problem_reachable[4] = { 255, 32, 32, 255 };
	static const unsigned char problem_adjacent[4] = { 255, 112, 32, 255 };
	static const unsigned char problem_portal[4] = { 255, 255, 255, 255 };
	static const unsigned char problem_ground_source[4] = { 255, 176, 32, 255 };
	static const unsigned char problem_ground_destination[4] = { 96, 160, 255, 255 };
	static const unsigned char problem_crossing[4] = { 255, 255, 64, 255 };
	static const unsigned char problem_start[4] = { 64, 255, 64, 255 };
	static const unsigned char problem_desired[4] = { 64, 255, 255, 255 };
	static const unsigned char problem_actual[4] = { 255, 64, 255, 255 };
	static const unsigned char problem_path[4] = { 192, 192, 192, 255 };
	const unsigned char *source = interactor;

	if (line->layer == EVOBOT_EZQ_DEBUG_ROUTE)
		source = route;
	else if (line->layer == EVOBOT_EZQ_DEBUG_ROUTE_BLOCKED)
		source = route_blocked;
	else if (line->layer == EVOBOT_EZQ_DEBUG_ROUTE_CONDITIONAL)
		source = route_conditional;
	else if (line->layer == EVOBOT_EZQ_DEBUG_ROUTE_FRONTIER)
		source = route_frontier;
	else if (line->layer == EVOBOT_EZQ_DEBUG_PROBLEM_REACHABLE)
		source = problem_reachable;
	else if (line->layer == EVOBOT_EZQ_DEBUG_PROBLEM_ADJACENT)
		source = problem_adjacent;
	else if (line->layer == EVOBOT_EZQ_DEBUG_PROBLEM_PORTAL)
		source = problem_portal;
	else if (line->layer == EVOBOT_EZQ_DEBUG_PROBLEM_GROUND_SOURCE)
		source = problem_ground_source;
	else if (line->layer == EVOBOT_EZQ_DEBUG_PROBLEM_GROUND_DESTINATION)
		source = problem_ground_destination;
	else if (line->layer == EVOBOT_EZQ_DEBUG_PROBLEM_CROSSING)
		source = problem_crossing;
	else if (line->layer == EVOBOT_EZQ_DEBUG_PROBLEM_START)
		source = problem_start;
	else if (line->layer == EVOBOT_EZQ_DEBUG_PROBLEM_DESIRED)
		source = problem_desired;
	else if (line->layer == EVOBOT_EZQ_DEBUG_PROBLEM_ACTUAL)
		source = problem_actual;
	else if (line->layer == EVOBOT_EZQ_DEBUG_PROBLEM_PATH)
		source = problem_path;
	else if (line->layer == EVOBOT_EZQ_DEBUG_AREA)
	{
		if (line->contents == EVOBOT_CONTENTS_WATER)
			source = water;
		else if (line->contents == EVOBOT_CONTENTS_SLIME)
			source = slime;
		else if (line->contents == EVOBOT_CONTENTS_LAVA)
			source = lava;
		else
			source = line->supported ? area_supported : area_unsupported;
	}
	else if (line->layer == EVOBOT_EZQ_DEBUG_PORTAL)
		source = portal;
	else if (line->layer == EVOBOT_EZQ_DEBUG_LEDGE)
		source = ledge;
	else if (line->layer == EVOBOT_EZQ_DEBUG_LIQUID)
		source = water;
	else if (line->layer == EVOBOT_EZQ_DEBUG_REACH_WALK)
		source = reach_walk;
	else if (line->layer == EVOBOT_EZQ_DEBUG_REACH_DROP)
		source = reach_drop;
	else if (line->layer == EVOBOT_EZQ_DEBUG_REACH_SWIM)
	{
		if (line->travel_type == EVOBOT_NAV_TRAVEL_WATER_ENTRY)
			source = reach_water_entry;
		else if (line->travel_type == EVOBOT_NAV_TRAVEL_WATER_EXIT)
			source = reach_water_exit;
		else if (line->travel_type == EVOBOT_NAV_TRAVEL_UNRESOLVED_WATER_JUMP)
			source = reach_unresolved;
		else
			source = reach_swim;
	}
	else if (line->interactor_kind == EVOBOT_INTERACTOR_DOOR ||
		line->interactor_kind == EVOBOT_INTERACTOR_PLATFORM ||
		line->interactor_kind == EVOBOT_INTERACTOR_TRAIN)
		source = door;
	else if (line->interactor_kind == EVOBOT_INTERACTOR_TELEPORTER)
		source = teleporter;
	else if (line->interactor_kind == EVOBOT_INTERACTOR_TELEPORT_DESTINATION)
		source = destination;
	else if (line->interactor_kind == EVOBOT_INTERACTOR_LEVEL_EXIT)
		source = level_exit;

	memcpy(color, source, 4);
}

static void EvoBot_EZQ_DebugTriangleColor(evobot_ezq_debug_layer_t layer,
	unsigned char color[4])
{
	static const unsigned char reachable[4] = { 88, 10, 10, 96 };
	static const unsigned char adjacent[4] = { 96, 36, 8, 112 };
	static const unsigned char portal[4] = { 160, 160, 24, 176 };
	const unsigned char *source = portal;

	if (layer == EVOBOT_EZQ_DEBUG_PROBLEM_REACHABLE)
		source = reachable;
	else if (layer == EVOBOT_EZQ_DEBUG_PROBLEM_ADJACENT)
		source = adjacent;
	memcpy(color, source, 4);
}

static int EvoBot_EZQ_DebugLineVisible(const evobot_ezq_debug_line_t *line,
	float radius_squared)
{
	float dx;
	float dy;
	float dz;

	if (!EvoBot_EZQ_DebugLineEnabled(line))
		return 0;
	if (line->layer >= EVOBOT_EZQ_DEBUG_PROBLEM_REACHABLE)
		return 1;
	if (evobot_ezq_debug_selected_area && line->layer < EVOBOT_EZQ_DEBUG_ROUTE &&
		line->area_a != evobot_ezq_debug_selected_area &&
		line->area_b != evobot_ezq_debug_selected_area)
		return 0;
	if (radius_squared <= 0)
		return 1;

	dx = (line->start[0] + line->end[0]) * 0.5f - cl.simorg[0];
	dy = (line->start[1] + line->end[1]) * 0.5f - cl.simorg[1];
	dz = (line->start[2] + line->end[2]) * 0.5f - cl.simorg[2];
	return dx * dx + dy * dy + dz * dz <= radius_squared;
}

static int EvoBot_EZQ_DebugFindArea(uint32_t id, size_t *area_index,
	evobot_nav_debug_area_t *area)
{
	evobot_nav_debug_summary_t summary;
	size_t i;

	if (!EvoBot_NavDebugSummary(&summary) || !summary.present)
		return 0;
	for (i = 0; i < summary.area_count; ++i)
	{
		if (EvoBot_NavDebugArea(i, area) && area->id == id)
		{
			if (area_index)
				*area_index = i;
			return 1;
		}
	}
	return 0;
}

static void EvoBot_EZQ_DebugPrintArea(const evobot_nav_debug_area_t *area)
{
	Con_Printf("EvoBot area %u\n", area->id);
	Con_Printf("contents: %s\n", EvoBot_EZQ_DebugContentsName(area->contents));
	Con_Printf("supported: %s\n", area->supported ? "yes" : "no");
	Con_Printf("faces: %u\n", (unsigned int)area->face_count);
	Con_Printf("portals: %u\n", (unsigned int)area->portal_count);
	Con_Printf("center: %.1f %.1f %.1f\n", area->center.v[0], area->center.v[1],
		area->center.v[2]);
}

static void EvoBot_EZQ_DebugShowArea_f(void)
{
	evobot_nav_debug_area_t area;
	uint32_t id;

	if (Cmd_Argc() != 2)
	{
		Con_Printf("usage: evobot_nav_show_area <id|all>\n");
		return;
	}
	if (!strcasecmp(Cmd_Argv(1), "all") || atoi(Cmd_Argv(1)) <= 0)
	{
		evobot_ezq_debug_selected_area = 0;
		Con_Printf("EvoBot area selection cleared\n");
		return;
	}
	id = (uint32_t)strtoul(Cmd_Argv(1), NULL, 10);
	if (!EvoBot_EZQ_DebugFindArea(id, NULL, &area))
	{
		Con_Printf("EvoBot area %u not found\n", id);
		return;
	}
	evobot_ezq_debug_selected_area = id;
	EvoBot_EZQ_DebugPrintArea(&area);
}

static void EvoBot_EZQ_DebugCurrentArea_f(void)
{
	evobot_vec3_t point;
	evobot_nav_debug_area_t area;
	uint32_t id;

	if (sv.state != ss_active)
	{
		Con_Printf("EvoBot current area is available only on a local server\n");
		return;
	}
	point.v[0] = cl.simorg[0];
	point.v[1] = cl.simorg[1];
	point.v[2] = cl.simorg[2];
	if (!EvoBot_NavDebugFindArea(&point, &id) ||
		!EvoBot_EZQ_DebugFindArea(id, NULL, &area))
	{
		Con_Printf("EvoBot current area: none\n");
		return;
	}
	EvoBot_EZQ_DebugPrintArea(&area);
}

static const char *EvoBot_EZQ_DebugTravelName(evobot_nav_travel_type_t type)
{
	switch (type)
	{
	case EVOBOT_NAV_TRAVEL_WALK: return "WALK";
	case EVOBOT_NAV_TRAVEL_DROP: return "DROP";
	case EVOBOT_NAV_TRAVEL_SWIM: return "SWIM";
	case EVOBOT_NAV_TRAVEL_WATER_ENTRY: return "WATER_ENTRY";
	case EVOBOT_NAV_TRAVEL_WATER_EXIT: return "WATER_EXIT";
	case EVOBOT_NAV_TRAVEL_UNRESOLVED_WATER_JUMP:
		return "UNRESOLVED_WATER_JUMP";
	}
	return "UNKNOWN";
}

static void EvoBot_EZQ_DebugShowAreaReach_f(void)
{
	evobot_nav_debug_summary_t summary;
	evobot_nav_debug_area_t area;
	uint32_t id;
	size_t i;

	if (Cmd_Argc() != 2 || atoi(Cmd_Argv(1)) <= 0)
	{
		Con_Printf("usage: evobot_nav_show_area_reach <area id>\n");
		return;
	}
	id = (uint32_t)strtoul(Cmd_Argv(1), NULL, 10);
	if (!EvoBot_EZQ_DebugFindArea(id, NULL, &area) ||
		!EvoBot_NavDebugSummary(&summary))
	{
		Con_Printf("EvoBot area %u not found\n", id);
		return;
	}
	evobot_ezq_debug_selected_area = id;
	Con_Printf("area %u\noutgoing:\n", id);
	for (i = 0; i < summary.reachability_count; i++)
	{
		evobot_nav_reachability_t reachability;

		if (EvoBot_NavDebugReachability(i, &reachability) &&
			reachability.source_area == id)
			Con_Printf("  %s -> %u\n",
				EvoBot_EZQ_DebugTravelName(reachability.travel_type),
				reachability.destination_area);
	}
	Con_Printf("incoming:\n");
	for (i = 0; i < summary.reachability_count; i++)
	{
		evobot_nav_reachability_t reachability;

		if (EvoBot_NavDebugReachability(i, &reachability) &&
			reachability.destination_area == id)
			Con_Printf("  %s <- %u\n",
				EvoBot_EZQ_DebugTravelName(reachability.travel_type),
				reachability.source_area);
	}
}

static void EvoBot_EZQ_DebugPortal_f(void)
{
	evobot_nav_debug_summary_t summary;
	evobot_nav_debug_portal_t portal;
	evobot_nav_debug_walk_candidate_t candidate;
	uint32_t portal_id;
	uint32_t source_area;
	uint32_t destination_area;

	if (Cmd_Argc() != 2 || atoi(Cmd_Argv(1)) <= 0)
	{
		Con_Printf("usage: evobot_nav_debug_portal <portal id>\n");
		return;
	}
	portal_id = (uint32_t)strtoul(Cmd_Argv(1), NULL, 10);
	if (!EvoBot_NavDebugSummary(&summary) || !summary.present ||
		portal_id > summary.portal_count ||
		!EvoBot_NavDebugPortal(portal_id - 1, &portal))
	{
		Con_Printf("EvoBot portal %u not found\n", portal_id);
		return;
	}
	EvoBot_EZQ_DebugSelectedPortalDirection(&summary, &portal,
		&source_area, &destination_area);
	evobot_ezq_debug_selected_portal = portal_id;
	evobot_ezq_debug_selected_nav_revision = summary.revision;
	{
		evobot_nav_route_summary_t route_summary;

		memset(&route_summary, 0, sizeof(route_summary));
		EvoBot_NavRouteDebugSummary(&route_summary);
		evobot_ezq_debug_selected_route_revision = route_summary.route_revision;
	}
	evobot_ezq_debug_revision = 0;
	Cvar_SetValue(&evobot_nav_show_problem, 1);
	Con_Printf("EvoBot portal %u selected\nsource area: %u\n"
		"destination area: %u\n", portal_id, source_area, destination_area);
	memset(&candidate, 0, sizeof(candidate));
	if (!EvoBot_NavDebugWalkCandidate(portal_id, source_area,
		destination_area, &candidate) || !candidate.present)
	{
		Con_Printf("WALK candidate: none\n");
		return;
	}
	Con_Printf("WALK candidate edge: %u\nstart: %.1f %.1f %.1f\n"
		"desired: %.1f %.1f %.1f\n",
		candidate.edge_index, candidate.start_origin.v[0],
		candidate.start_origin.v[1], candidate.start_origin.v[2],
		candidate.desired_destination.v[0],
		candidate.desired_destination.v[1],
		candidate.desired_destination.v[2]);
	if (candidate.movement_succeeded)
		Con_Printf("actual: %.1f %.1f %.1f\nPM steps: %u\n"
			"reached destination: %s\n",
			candidate.result.state.origin.v[0],
			candidate.result.state.origin.v[1],
			candidate.result.state.origin.v[2], candidate.move_steps,
			candidate.reached_destination ? "yes" : "no");
	else
		Con_Printf("PM_PlayerMove result: unavailable\n");
}

void EvoBot_EZQ_DebugInit(void)
{
	Cvar_Register(&evobot_nav_show);
	Cvar_Register(&evobot_nav_show_areas);
	Cvar_Register(&evobot_nav_show_portals);
	Cvar_Register(&evobot_nav_show_ledges);
	Cvar_Register(&evobot_nav_show_liquids);
	Cvar_Register(&evobot_nav_show_interactors);
	Cvar_Register(&evobot_nav_show_reach);
	Cvar_Register(&evobot_nav_show_walk);
	Cvar_Register(&evobot_nav_show_drop);
	Cvar_Register(&evobot_nav_show_swim);
	Cvar_Register(&evobot_nav_show_route);
	Cvar_Register(&evobot_nav_show_problem);
	Cvar_Register(&evobot_nav_show_problem_xray);
	Cvar_Register(&evobot_nav_show_radius);
	Cmd_AddCommand("evobot_nav_show_area", EvoBot_EZQ_DebugShowArea_f);
	Cmd_AddCommand("evobot_nav_current_area", EvoBot_EZQ_DebugCurrentArea_f);
	Cmd_AddCommand("evobot_nav_show_area_reach",
		EvoBot_EZQ_DebugShowAreaReach_f);
	Cmd_AddCommand("evobot_nav_debug_portal", EvoBot_EZQ_DebugPortal_f);
}

void EvoBot_EZQ_DebugDraw(void)
{
	evobot_nav_debug_summary_t summary;
	evobot_nav_route_summary_t route_summary;
	size_t triangle_vertex_count = 0;
	size_t i;
	float radius_squared;
	int problem_pass;

	if ((!evobot_nav_show.integer && !evobot_nav_show_problem.integer) ||
		sv.state != ss_active || !cl.worldmodel)
		return;
	memset(&route_summary, 0, sizeof(route_summary));
	EvoBot_NavRouteDebugSummary(&route_summary);
	if (!EvoBot_NavDebugSummary(&summary) || !summary.present)
	{
		evobot_ezq_debug_selected_portal = 0;
		return;
	}
	if (evobot_ezq_debug_selected_portal &&
		(evobot_ezq_debug_selected_nav_revision != summary.revision ||
		 evobot_ezq_debug_selected_route_revision != route_summary.route_revision))
	{
		evobot_ezq_debug_selected_portal = 0;
		evobot_ezq_debug_revision = 0;
	}
	if ((summary.revision != evobot_ezq_debug_revision ||
		route_summary.route_revision != evobot_ezq_debug_route_revision) &&
		!EvoBot_EZQ_DebugBuildCache(&summary, &route_summary))
		return;
	if (evobot_ezq_debug_vertex_capacity < evobot_ezq_debug_line_count * 2)
	{
		evobot_ezq_debug_vertex_capacity = evobot_ezq_debug_line_count * 2;
		evobot_ezq_debug_vertices = Q_realloc(evobot_ezq_debug_vertices,
			evobot_ezq_debug_vertex_capacity * sizeof(*evobot_ezq_debug_vertices));
	}
	if (evobot_ezq_debug_triangle_vertex_capacity <
		evobot_ezq_debug_triangle_count * 3)
	{
		evobot_ezq_debug_triangle_vertex_capacity =
			evobot_ezq_debug_triangle_count * 3;
		evobot_ezq_debug_triangle_vertices = Q_realloc(
			evobot_ezq_debug_triangle_vertices,
			evobot_ezq_debug_triangle_vertex_capacity *
				sizeof(*evobot_ezq_debug_triangle_vertices));
	}
	radius_squared = evobot_nav_show_radius.value > 0 ?
		evobot_nav_show_radius.value * evobot_nav_show_radius.value : 0;
	if (evobot_nav_show_problem.integer)
	{
		for (i = 0; i < evobot_ezq_debug_triangle_count; i++)
		{
			const evobot_ezq_debug_triangle_t *triangle =
				&evobot_ezq_debug_triangles[i];
			int vertex_index;

			for (vertex_index = 0; vertex_index < 3; vertex_index++)
			{
				r_debug_line_vertex_t *vertex =
					&evobot_ezq_debug_triangle_vertices[triangle_vertex_count++];

				memcpy(vertex->position, triangle->vertices[vertex_index],
					sizeof(vertex->position));
				EvoBot_EZQ_DebugTriangleColor(triangle->layer, vertex->color);
			}
		}
		R_Draw3DPolygons(evobot_ezq_debug_triangle_vertices,
			(int)triangle_vertex_count, evobot_nav_show_problem_xray.integer);
	}
	for (problem_pass = 0; problem_pass < 2; problem_pass++)
	{
		size_t vertex_count = 0;

		for (i = 0; i < evobot_ezq_debug_line_count; ++i)
		{
			const evobot_ezq_debug_line_t *line = &evobot_ezq_debug_lines[i];
			int problem = line->layer >= EVOBOT_EZQ_DEBUG_PROBLEM_REACHABLE;
			r_debug_line_vertex_t *start;
			r_debug_line_vertex_t *end;

			if (problem != problem_pass ||
				!EvoBot_EZQ_DebugLineVisible(line, radius_squared))
				continue;
			start = &evobot_ezq_debug_vertices[vertex_count++];
			end = &evobot_ezq_debug_vertices[vertex_count++];
			memcpy(start->position, line->start, sizeof(start->position));
			memcpy(end->position, line->end, sizeof(end->position));
			EvoBot_EZQ_DebugLineColor(line, start->color);
			memcpy(end->color, start->color, sizeof(end->color));
		}
		if (problem_pass && evobot_nav_show_problem_xray.integer)
			R_Draw3DLinesXRay(evobot_ezq_debug_vertices, (int)vertex_count, 3.0f);
		else
			R_Draw3DLines(evobot_ezq_debug_vertices, (int)vertex_count,
				problem_pass ? 3.0f :
				(evobot_ezq_debug_selected_area ? 2.5f : 1.0f));
	}
}

void EvoBot_EZQ_DebugShutdown(void)
{
	EvoBot_EZQ_DebugClearCache();
	evobot_ezq_debug_selected_area = 0;
	evobot_ezq_debug_selected_portal = 0;
	evobot_ezq_debug_selected_nav_revision = 0;
	evobot_ezq_debug_selected_route_revision = 0;
}
