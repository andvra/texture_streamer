#include <iostream>
#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include "render.h"

unsigned int vao;
unsigned int ebo;
unsigned int shader_program;

bool key_state[GLFW_KEY_LAST]{};

bool wireframes = false;
void framebuffer_size_callback(GLFWwindow* window, int width, int height)
{
	glViewport(0, 0, width, height);
}

void process_input(GLFWwindow* window) {
	unsigned int keycodes_of_interest[] = { GLFW_KEY_A ,GLFW_KEY_ESCAPE };
	bool new_key_state[GLFW_KEY_LAST]{};

	for (auto kc : keycodes_of_interest) {
		new_key_state[kc] = (glfwGetKey(window, kc) == GLFW_PRESS);
	}

	if (!key_state[GLFW_KEY_A] && new_key_state[GLFW_KEY_A]) {
		wireframes = !wireframes;
	}

	if (new_key_state[GLFW_KEY_ESCAPE]) {
		glfwSetWindowShouldClose(window, true);
	}

	for (auto kc : keycodes_of_interest) {
		key_state[kc] = new_key_state[kc];
	}
}

void init_triangle() {
	float vertices[] = {
		 0.5f,  0.5f, 0.0f,
		 0.5f, -0.5f, 0.0f,
		-0.5f, -0.5f, 0.0f,
		-0.5f,  0.5f, 0.0f
	};
	unsigned int indices[] = {
		0, 1, 3,
		1, 2, 3
	};

	const char* vertex_shader_source = "#version 430 core\n"
		"layout (location = 0) in vec3 aPos;\n"
		"void main()\n"
		"{\n"
		"   gl_Position = vec4(aPos.x, aPos.y, aPos.z, 1.0);\n"
		"}\0";
	unsigned int vertex_shader = glCreateShader(GL_VERTEX_SHADER);
	glShaderSource(vertex_shader, 1, &vertex_shader_source, nullptr);
	glCompileShader(vertex_shader);

	int  success;
	char infoLog[512];
	glGetShaderiv(vertex_shader, GL_COMPILE_STATUS, &success);

	if (!success)
	{
		glGetShaderInfoLog(vertex_shader, 512, NULL, infoLog);
		std::cout << "ERROR::SHADER::VERTEX::COMPILATION_FAILED\n" << infoLog << std::endl;
	}

	const char* fragment_shader_source = "#version 330 core\n"
		"out vec4 FragColor;\n"
		"uniform vec4 custom_color;"
		"void main()\n"
		"{\n"
		"	// Position in window, [-.5, .5]\n"
		"	vec2 pos = vec2(gl_FragCoord.x/800 - 0.5, gl_FragCoord.y/600 - 0.5);\n"
		"	float dist = distance(pos, vec2(0.5, 0.5));\n"
		"   FragColor = custom_color * vec4(gl_FragCoord.x/800, gl_FragCoord.y/600, dist, 1);\n"
		"}\n";
	unsigned int fragment_shader;
	fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);
	glShaderSource(fragment_shader, 1, &fragment_shader_source, NULL);
	glCompileShader(fragment_shader);

	glGetShaderiv(fragment_shader, GL_COMPILE_STATUS, &success);
	if (!success)
	{
		glGetShaderInfoLog(fragment_shader, 512, NULL, infoLog);
		std::cout << "ERROR::SHADER::FRAGMENT::COMPILATION_FAILED\n" << infoLog << std::endl;
	}

	shader_program = glCreateProgram();
	glAttachShader(shader_program, vertex_shader);
	glAttachShader(shader_program, fragment_shader);
	glLinkProgram(shader_program);

	glGetProgramiv(shader_program, GL_LINK_STATUS, &success);
	if (!success) {
		glGetProgramInfoLog(shader_program, 512, NULL, infoLog);
		std::cout << "ERROR::SHADER::PROGRAM::LINK_FAILED\n" << infoLog << std::endl;
	}

	glUseProgram(shader_program);
	glDeleteShader(vertex_shader);
	glDeleteShader(fragment_shader);

	unsigned int vbo;
	glGenBuffers(1, &vbo);
	glGenVertexArrays(1, &vao);
	glBindVertexArray(vao);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
	glEnableVertexAttribArray(0);
	glBindVertexArray(0);

	glGenBuffers(1, &ebo);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);
}

void render_triangle() {
	glUseProgram(shader_program);
	auto color_location = glGetUniformLocation(shader_program, "custom_color");
	auto the_time = glfwGetTime();
	auto r = 0.5f + 0.5f * std::sin(0.1 * the_time);
	auto g = 0.5f + 0.5f * std::sin(the_time + 1.0);
	glUniform4f(color_location, r, g, 0.9f, 1.0f);
	glBindVertexArray(vao);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
	glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
}

void render(GLFWwindow* window) {
	glClearColor(0.2f, 0.3f, 0.3f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);

	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	if (wireframes) {
		glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
	}

	render_triangle();

	glfwSwapBuffers(window);
}

void init() {
	init_triangle();
}

void main_loop() {
	int width = 800;
	int height = 600;

	glfwInit();
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

	auto window = glfwCreateWindow(width, height, "Rendering window", nullptr, nullptr);

	if (!window) {
		std::cout << "Could not create window" << std::endl;
		glfwTerminate();
		return;
	}

	glfwMakeContextCurrent(window);

	if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
	{
		std::cout << "Failed to initialize GLAD" << std::endl;
		return;
	}

	glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);

	init();

	while (!glfwWindowShouldClose(window))
	{
		process_input(window);
		render(window);
		glfwPollEvents();
	}

	glfwTerminate();
}