#ifndef EZQUAKE_R_DEBUG_PRIMITIVE_HEADER
#define EZQUAKE_R_DEBUG_PRIMITIVE_HEADER

typedef struct r_debug_line_vertex_s {
	float position[3];
	unsigned char color[4];
} r_debug_line_vertex_t;

void R_Draw3DLines(const r_debug_line_vertex_t* vertices, int vertex_count, float thickness);
void R_Draw3DLinesXRay(const r_debug_line_vertex_t* vertices, int vertex_count, float thickness);
void R_Draw3DPolygons(const r_debug_line_vertex_t* vertices, int vertex_count, qbool xray);

#endif // EZQUAKE_R_DEBUG_PRIMITIVE_HEADER
