#include "quakedef.h"
#include "gl_local.h"
#include "glc_local.h"
#include "r_debug_primitive.h"
#include "r_renderer.h"
#include "r_program.h"
#include "r_buffers.h"
#include "glm_vao.h"

#define DEBUG_LINE_BUFFER_SIZE (16 * 1024 * 1024)

void R_Draw3DLines(const r_debug_line_vertex_t* vertices, int vertex_count, float thickness)
{
	if (!vertices || vertex_count <= 0)
		return;

	renderer.Draw3DLines(vertices, vertex_count, thickness);
}

#ifdef RENDERER_OPTION_CLASSIC_OPENGL
void GLC_Draw3DLines(const r_debug_line_vertex_t* vertices, int vertex_count, float thickness)
{
	int i;

	R_ProgramUse(r_program_none);
	R_ApplyRenderingState(r_state_debug_lines);
	R_CustomLineWidth(thickness);
	GLC_Begin(GL_LINES);
	for (i = 0; i < vertex_count; ++i) {
		R_CustomColor4ubv(vertices[i].color);
		GLC_Vertex3fv(vertices[i].position);
	}
	GLC_End();
}
#endif

#ifdef RENDERER_OPTION_MODERN_OPENGL
static void GLM_ConfigureDebugLineVAO(void)
{
	GLM_ConfigureVertexAttribPointer(vao_debug_lines,
		r_buffer_debug_line_vertex_data, 0, 3, GL_FLOAT, GL_FALSE,
		sizeof(r_debug_line_vertex_t),
		VBO_FIELDOFFSET(r_debug_line_vertex_t, position), 0);
	GLM_ConfigureVertexAttribPointer(vao_debug_lines,
		r_buffer_debug_line_vertex_data, 1, 4, GL_UNSIGNED_BYTE, GL_TRUE,
		sizeof(r_debug_line_vertex_t),
		VBO_FIELDOFFSET(r_debug_line_vertex_t, color), 0);
}

qbool GLM_CompileDebugLinesProgram(void)
{
	if (R_ProgramRecompileNeeded(r_program_debug_lines, 0)) {
		R_ProgramCompile(r_program_debug_lines);
	}

	return R_ProgramReady(r_program_debug_lines);
}

static qbool GLM_EnsureDebugLineResources(int size)
{
	static qbool size_warning_printed;

	if (size > DEBUG_LINE_BUFFER_SIZE) {
		if (!size_warning_printed) {
			Com_Printf("EvoBot debug lines exceed the %d MB render buffer\n",
				DEBUG_LINE_BUFFER_SIZE / (1024 * 1024));
			size_warning_printed = true;
		}
		return false;
	}

	if (!R_BufferReferenceIsValid(r_buffer_debug_line_vertex_data)) {
		buffers.Create(r_buffer_debug_line_vertex_data, buffertype_vertex,
			"debug-line-vbo", DEBUG_LINE_BUFFER_SIZE, NULL,
			bufferusage_reuse_per_frame);
	}

	if (!R_VertexArrayCreated(vao_debug_lines)) {
		R_GenVertexArray(vao_debug_lines);
		GLM_ConfigureDebugLineVAO();
		R_BindVertexArray(vao_none);
	}

	return R_BufferReferenceIsValid(r_buffer_debug_line_vertex_data)
		&& R_VertexArrayCreated(vao_debug_lines);
}

void GLM_Draw3DLines(const r_debug_line_vertex_t* vertices, int vertex_count, float thickness)
{
	int size = vertex_count * sizeof(*vertices);
	uintptr_t offset;

	if (!GLM_CompileDebugLinesProgram())
		return;

	if (!GLM_EnsureDebugLineResources(size))
		return;

	buffers.Update(r_buffer_debug_line_vertex_data, size, (void*)vertices);
	offset = buffers.BufferOffset(r_buffer_debug_line_vertex_data) / sizeof(*vertices);

	R_ProgramUse(r_program_debug_lines);
	R_ApplyRenderingState(r_state_debug_lines);
	R_CustomLineWidth(thickness);
	GL_DrawArrays(GL_LINES, offset, vertex_count);
}
#endif
