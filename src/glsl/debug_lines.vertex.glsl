#ezquake-definitions

layout(location = 0) in vec3 position;
layout(location = 1) in vec4 color;

out vec4 vertexColor;

void main(void)
{
	gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0);
	vertexColor = color;
}
