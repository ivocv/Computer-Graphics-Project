#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <numbers>
#include <sstream>
#include <string>
#include <vector>

#ifndef SAFE_SHADER_DIR
#define SAFE_SHADER_DIR "shaders"
#endif

namespace
{

constexpr float kDialStep = 360.0f / 10.0f;
constexpr std::array<int, 3> kCombination = {4, 7, 2};

enum class LockState
{
    Locked,
    Unlocking,
    Unlocked,
    Error
};

struct Vertex
{
    glm::vec3 position;
    glm::vec3 normal;
};

struct Mesh
{
    GLuint vao = 0;
    GLuint vbo = 0;
    GLsizei vertexCount = 0;
};

struct Material
{
    glm::vec3 baseColor;
    float metalness;
    float roughness;
    glm::vec3 emissive = glm::vec3(0.0f);
    bool brushed = false;
};

struct App
{
    GLFWwindow* window = nullptr;
    GLuint program = 0;
    Mesh cube;
    Mesh cylinder;
    Mesh goldIngot;

    int framebufferWidth = 1280;
    int framebufferHeight = 720;

    std::array<int, 3> dialValues = {0, 0, 0};
    std::array<float, 3> dialAngles = {0.0f, 0.0f, 0.0f};
    std::array<float, 3> dialTargets = {0.0f, 0.0f, 0.0f};
    int selectedDial = 0;

    LockState state = LockState::Locked;
    float unlockProgress = 0.0f;
    float errorTimeLeft = 0.0f;

    bool perspectiveProjection = true;
    bool inspectionMode = false;
    bool wireframe = false;
    float inspectionAmount = 0.0f;

    float cameraYaw = -31.0f;
    float cameraPitch = 17.0f;
    float cameraDistance = 13.5f;
};

float smooth01(float value)
{
    const float t = std::clamp(value, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

float approach(float current, float target, float speed, float deltaSeconds)
{
    const float distance = target - current;
    const float maxStep = speed * deltaSeconds;
    if (std::abs(distance) <= maxStep)
        return target;
    return current + (distance > 0.0f ? maxStep : -maxStep);
}

void printControls()
{
    std::cout << "\nMechanical safe controls\n"
              << "  1 / 2 / 3   Select a code dial\n"
              << "  A / D       Rotate the selected dial\n"
              << "  Enter       Test the current combination\n"
              << "  Arrow keys  Orbit the camera\n"
              << "  W / S       Zoom camera in / out\n"
              << "  P           Perspective / orthographic projection\n"
              << "  T           Exploded technical view\n"
              << "  F           Wireframe overlay mode\n"
              << "  R           Reset the lock\n"
              << "  H           Print this help again\n"
              << "  Esc         Exit\n\n"
              << "Demo combination: 4 - 7 - 2\n\n";
}

std::string readTextFile(const std::string& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        std::cerr << "Could not read shader: " << path << '\n';
        return {};
    }

    std::ostringstream stream;
    stream << input.rdbuf();
    return stream.str();
}

GLuint compileShader(GLenum type, const std::string& source)
{
    const GLuint shader = glCreateShader(type);
    const char* shaderSource = source.c_str();
    glShaderSource(shader, 1, &shaderSource, nullptr);
    glCompileShader(shader);

    GLint compiled = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (compiled == GL_TRUE)
        return shader;

    GLint length = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
    std::string log(static_cast<std::size_t>(std::max(length, 1)), '\0');
    glGetShaderInfoLog(shader, length, nullptr, log.data());
    std::cerr << "Shader compilation failed:\n" << log << '\n';
    glDeleteShader(shader);
    return 0;
}

GLuint createProgram(const std::string& vertexPath, const std::string& fragmentPath)
{
    const std::string vertexSource = readTextFile(vertexPath);
    const std::string fragmentSource = readTextFile(fragmentPath);
    if (vertexSource.empty() || fragmentSource.empty())
        return 0;

    const GLuint vertex = compileShader(GL_VERTEX_SHADER, vertexSource);
    const GLuint fragment = compileShader(GL_FRAGMENT_SHADER, fragmentSource);
    if (vertex == 0 || fragment == 0)
    {
        if (vertex != 0)
            glDeleteShader(vertex);
        if (fragment != 0)
            glDeleteShader(fragment);
        return 0;
    }

    const GLuint program = glCreateProgram();
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glLinkProgram(program);
    glDeleteShader(vertex);
    glDeleteShader(fragment);

    GLint linked = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (linked == GL_TRUE)
        return program;

    GLint length = 0;
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
    std::string log(static_cast<std::size_t>(std::max(length, 1)), '\0');
    glGetProgramInfoLog(program, length, nullptr, log.data());
    std::cerr << "Shader link failed:\n" << log << '\n';
    glDeleteProgram(program);
    return 0;
}
Mesh createMesh(const std::vector<Vertex>& vertices)
{
    Mesh mesh;
    mesh.vertexCount = static_cast<GLsizei>(vertices.size());

    glGenVertexArrays(1, &mesh.vao);
    glGenBuffers(1, &mesh.vbo);

    glBindVertexArray(mesh.vao);
    glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(vertices.size() * sizeof(Vertex)),
                 vertices.data(),
                 GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0,
                          3,
                          GL_FLOAT,
                          GL_FALSE,
                          sizeof(Vertex),
                          reinterpret_cast<void*>(offsetof(Vertex, position)));

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1,
                          3,
                          GL_FLOAT,
                          GL_FALSE,
                          sizeof(Vertex),
                          reinterpret_cast<void*>(offsetof(Vertex, normal)));

    glBindVertexArray(0);
    return mesh;
}
void addQuad(std::vector<Vertex>& vertices,
             const glm::vec3& a,
             const glm::vec3& b,
             const glm::vec3& c,
             const glm::vec3& d,
             const glm::vec3& normal)
{
    vertices.insert(vertices.end(), {
        {a, normal}, {b, normal}, {c, normal},
        {c, normal}, {d, normal}, {a, normal}
    });
}
Mesh createCube()
{
    constexpr float h = 0.5f;
    std::vector<Vertex> vertices;
    vertices.reserve(36);

    addQuad(vertices, {-h, -h,  h}, { h, -h,  h}, { h,  h,  h}, {-h,  h,  h}, {0, 0, 1});
    addQuad(vertices, { h, -h, -h}, {-h, -h, -h}, {-h,  h, -h}, { h,  h, -h}, {0, 0, -1});
    addQuad(vertices, { h, -h,  h}, { h, -h, -h}, { h,  h, -h}, { h,  h,  h}, {1, 0, 0});
    addQuad(vertices, {-h, -h, -h}, {-h, -h,  h}, {-h,  h,  h}, {-h,  h, -h}, {-1, 0, 0});
    addQuad(vertices, {-h,  h,  h}, { h,  h,  h}, { h,  h, -h}, {-h,  h, -h}, {0, 1, 0});
    addQuad(vertices, {-h, -h, -h}, { h, -h, -h}, { h, -h,  h}, {-h, -h,  h}, {0, -1, 0});

    return createMesh(vertices);
}

Mesh createGoldIngot()
{
    constexpr float bottomHalfWidth = 0.50f;
    constexpr float topHalfWidth = 0.35f;
    constexpr float halfHeight = 0.50f;
    constexpr float halfDepth = 0.50f;

    const glm::vec3 frontBottomLeft(-bottomHalfWidth, -halfHeight,  halfDepth);
    const glm::vec3 frontBottomRight( bottomHalfWidth, -halfHeight,  halfDepth);
    const glm::vec3 frontTopRight( topHalfWidth, halfHeight, halfDepth);
    const glm::vec3 frontTopLeft(-topHalfWidth, halfHeight, halfDepth);
    const glm::vec3 backBottomLeft(-bottomHalfWidth, -halfHeight, -halfDepth);
    const glm::vec3 backBottomRight( bottomHalfWidth, -halfHeight, -halfDepth);
    const glm::vec3 backTopRight( topHalfWidth, halfHeight, -halfDepth);
    const glm::vec3 backTopLeft(-topHalfWidth, halfHeight, -halfDepth);

    std::vector<Vertex> vertices;
    vertices.reserve(36);
    addQuad(vertices, frontBottomLeft, frontBottomRight, frontTopRight, frontTopLeft, {0.0f, 0.0f, 1.0f});
    addQuad(vertices, backBottomLeft, backTopLeft, backTopRight, backBottomRight, {0.0f, 0.0f, -1.0f});
    addQuad(vertices, frontBottomLeft, backBottomLeft, backBottomRight, frontBottomRight, {0.0f, -1.0f, 0.0f});
    addQuad(vertices, frontTopLeft, frontTopRight, backTopRight, backTopLeft, {0.0f, 1.0f, 0.0f});
    addQuad(vertices, frontBottomLeft, frontTopLeft, backTopLeft, backBottomLeft, {-0.99f, 0.15f, 0.0f});
    addQuad(vertices, frontBottomRight, backBottomRight, backTopRight, frontTopRight, {0.99f, 0.15f, 0.0f});
    return createMesh(vertices);
}

Mesh createCylinder(int segments)
{
    constexpr float radius = 0.5f;
    constexpr float halfDepth = 0.5f;
    const float tau = 2.0f * std::numbers::pi_v<float>;

    std::vector<Vertex> vertices;
    vertices.reserve(static_cast<std::size_t>(segments) * 12);

    for (int index = 0; index < segments; ++index)
    {
        const float angle0 = tau * static_cast<float>(index) / static_cast<float>(segments);
        const float angle1 = tau * static_cast<float>(index + 1) / static_cast<float>(segments);
        const glm::vec3 radial0(std::cos(angle0), std::sin(angle0), 0.0f);
        const glm::vec3 radial1(std::cos(angle1), std::sin(angle1), 0.0f);

        const glm::vec3 front0 = radius * radial0 + glm::vec3(0.0f, 0.0f, halfDepth);
        const glm::vec3 front1 = radius * radial1 + glm::vec3(0.0f, 0.0f, halfDepth);
        const glm::vec3 back0 = radius * radial0 - glm::vec3(0.0f, 0.0f, halfDepth);
        const glm::vec3 back1 = radius * radial1 - glm::vec3(0.0f, 0.0f, halfDepth);

        vertices.insert(vertices.end(), {
            {front0, radial0}, {back0, radial0}, {back1, radial1},
            {back1, radial1}, {front1, radial1}, {front0, radial0},

            {{0.0f, 0.0f, halfDepth}, {0.0f, 0.0f, 1.0f}},
            {front0, {0.0f, 0.0f, 1.0f}},
            {front1, {0.0f, 0.0f, 1.0f}},

            {{0.0f, 0.0f, -halfDepth}, {0.0f, 0.0f, -1.0f}},
            {back1, {0.0f, 0.0f, -1.0f}},
            {back0, {0.0f, 0.0f, -1.0f}}
        });
    }

    return createMesh(vertices);
}

void destroyMesh(Mesh& mesh)
{
    if (mesh.vbo != 0)
        glDeleteBuffers(1, &mesh.vbo);
    if (mesh.vao != 0)
        glDeleteVertexArrays(1, &mesh.vao);
    mesh = {};
}

void setMat4(GLuint program, const char* name, const glm::mat4& value)
{
    glUniformMatrix4fv(glGetUniformLocation(program, name), 1, GL_FALSE, glm::value_ptr(value));
}

void setVec3(GLuint program, const char* name, const glm::vec3& value)
{
    glUniform3fv(glGetUniformLocation(program, name), 1, glm::value_ptr(value));
}

void setFloat(GLuint program, const char* name, float value)
{
    glUniform1f(glGetUniformLocation(program, name), value);
}

void setInt(GLuint program, const char* name, int value)
{
    glUniform1i(glGetUniformLocation(program, name), value);
}
void drawMesh(const App& app,
              const Mesh& mesh,
              const glm::mat4& model,
              const Material& material)
{
    setMat4(app.program, "uModel", model);
    setVec3(app.program, "uBaseColor", material.baseColor);
    setVec3(app.program, "uEmissive", material.emissive);
    setFloat(app.program, "uMetalness", material.metalness);
    setFloat(app.program, "uRoughness", material.roughness);
    setInt(app.program, "uBrushed", material.brushed ? 1 : 0);

    glBindVertexArray(mesh.vao);
    glDrawArrays(GL_TRIANGLES, 0, mesh.vertexCount);
}

glm::mat4 translated(glm::mat4 matrix, const glm::vec3& offset)
{
    return glm::translate(matrix, offset);
}

glm::mat4 scaled(glm::mat4 matrix, const glm::vec3& scale)
{
    return glm::scale(matrix, scale);
}

glm::mat4 rotated(glm::mat4 matrix, float degrees, const glm::vec3& axis)
{
    return glm::rotate(matrix, glm::radians(degrees), axis);
}

glm::mat4 transform(const glm::vec3& position, const glm::vec3& scale)
{
    return scaled(translated(glm::mat4(1.0f), position), scale);
}

const char* stateName(const App& app)
{
    switch (app.state)
    {
        case LockState::Locked: return "LOCKED";
        case LockState::Unlocking: return "UNLOCKING";
        case LockState::Unlocked: return "OPEN";
        case LockState::Error: return "INVALID CODE";
    }
    return "UNKNOWN";
}

void updateWindowTitle(const App& app)
{
    std::ostringstream title;
    title << "Mechanical Safe | Code: "
          << app.dialValues[0] << " - "
          << app.dialValues[1] << " - "
          << app.dialValues[2] << " | " << stateName(app)
          << " | Dial " << (app.selectedDial + 1)
          << " | [H] Controls";
    glfwSetWindowTitle(app.window, title.str().c_str());
}

void resetLock(App& app)
{
    app.dialValues = {0, 0, 0};
    app.dialAngles = {0.0f, 0.0f, 0.0f};
    app.dialTargets = {0.0f, 0.0f, 0.0f};
    app.selectedDial = 0;
    app.state = LockState::Locked;
    app.unlockProgress = 0.0f;
    app.errorTimeLeft = 0.0f;
}

void rotateSelectedDial(App& app, int direction)
{
    if (app.state == LockState::Unlocking || app.state == LockState::Unlocked)
        return;

    app.state = LockState::Locked;
    app.errorTimeLeft = 0.0f;
    const int index = app.selectedDial;
    app.dialValues[index] = (app.dialValues[index] + direction + 10) % 10;
    app.dialTargets[index] -= static_cast<float>(direction) * kDialStep;
}

void attemptUnlock(App& app)
{
    if (app.state == LockState::Unlocking || app.state == LockState::Unlocked)
        return;

    if (app.dialValues == kCombination)
    {
        app.state = LockState::Unlocking;
        app.unlockProgress = 0.0f;
        app.errorTimeLeft = 0.0f;
        std::cout << "Correct code. Releasing the locking mechanism...\n";
    }
    else
    {
        app.state = LockState::Error;
        app.errorTimeLeft = 1.2f;
        std::cout << "Incorrect code. The lock remains engaged.\n";
    }
}

void glfwErrorCallback(int, const char* description)
{
    std::cerr << "GLFW error: " << description << '\n';
}

void framebufferSizeCallback(GLFWwindow* window, int width, int height)
{
    auto* app = static_cast<App*>(glfwGetWindowUserPointer(window));
    if (app == nullptr || width <= 0 || height <= 0)
        return;
    app->framebufferWidth = width;
    app->framebufferHeight = height;
    glViewport(0, 0, width, height);
}

void scrollCallback(GLFWwindow* window, double, double yOffset)
{
    auto* app = static_cast<App*>(glfwGetWindowUserPointer(window));
    if (app == nullptr)
        return;
    app->cameraDistance = std::clamp(app->cameraDistance - static_cast<float>(yOffset), 7.0f, 22.0f);
}

void keyCallback(GLFWwindow* window, int key, int, int action, int)
{
    if (action != GLFW_PRESS && action != GLFW_REPEAT)
        return;

    auto* app = static_cast<App*>(glfwGetWindowUserPointer(window));
    if (app == nullptr)
        return;

    switch (key)
    {
        case GLFW_KEY_ESCAPE:
            glfwSetWindowShouldClose(window, GLFW_TRUE);
            break;
        case GLFW_KEY_1:
            app->selectedDial = 0;
            break;
        case GLFW_KEY_2:
            app->selectedDial = 1;
            break;
        case GLFW_KEY_3:
            app->selectedDial = 2;
            break;
        case GLFW_KEY_A:
            rotateSelectedDial(*app, -1);
            break;
        case GLFW_KEY_D:
            rotateSelectedDial(*app, 1);
            break;
        case GLFW_KEY_ENTER:
            attemptUnlock(*app);
            break;
        case GLFW_KEY_P:
            if (action == GLFW_PRESS)
                app->perspectiveProjection = !app->perspectiveProjection;
            break;
        case GLFW_KEY_T:
            if (action == GLFW_PRESS)
                app->inspectionMode = !app->inspectionMode;
            break;
        case GLFW_KEY_F:
            if (action == GLFW_PRESS)
                app->wireframe = !app->wireframe;
            break;
        case GLFW_KEY_R:
            if (action == GLFW_PRESS)
                resetLock(*app);
            break;
        case GLFW_KEY_H:
            if (action == GLFW_PRESS)
                printControls();
            break;
        default:
            break;
    }
}

void updateCamera(App& app, float deltaSeconds)
{
    const auto held = [&app](int key)
    {
        return glfwGetKey(app.window, key) == GLFW_PRESS;
    };

    const float orbitSpeed = 55.0f * deltaSeconds;
    if (held(GLFW_KEY_LEFT))
        app.cameraYaw -= orbitSpeed;
    if (held(GLFW_KEY_RIGHT))
        app.cameraYaw += orbitSpeed;
    if (held(GLFW_KEY_UP))
        app.cameraPitch = std::min(app.cameraPitch + orbitSpeed, 75.0f);
    if (held(GLFW_KEY_DOWN))
        app.cameraPitch = std::max(app.cameraPitch - orbitSpeed, -20.0f);
    if (held(GLFW_KEY_W))
        app.cameraDistance = std::max(app.cameraDistance - 4.0f * deltaSeconds, 7.0f);
    if (held(GLFW_KEY_S))
        app.cameraDistance = std::min(app.cameraDistance + 4.0f * deltaSeconds, 22.0f);
}

//обновява всички движещи се и времеви елементи на всеки кадър.
void update(App& app, float deltaSeconds)
{
    updateCamera(app, deltaSeconds);

    for (std::size_t index = 0; index < app.dialAngles.size(); ++index)
        app.dialAngles[index] = approach(app.dialAngles[index], app.dialTargets[index], 290.0f, deltaSeconds);

    if (app.state == LockState::Error)
    {
        app.errorTimeLeft -= deltaSeconds;
        if (app.errorTimeLeft <= 0.0f)
            app.state = LockState::Locked;
    }
    else if (app.state == LockState::Unlocking)
    {
        app.unlockProgress = std::min(app.unlockProgress + deltaSeconds / 2.8f, 1.0f);
        if (app.unlockProgress >= 1.0f)
        {
            app.state = LockState::Unlocked;
            std::cout << "Safe is open. Press R to reset the demonstration.\n";
        }
    }

    const float inspectionTarget = app.inspectionMode ? 1.0f : 0.0f;
    app.inspectionAmount = approach(app.inspectionAmount, inspectionTarget, 2.8f, deltaSeconds);
    updateWindowTitle(app);
}

glm::mat4 cameraView(const App& app, glm::vec3& outPosition)
{
    const float yaw = glm::radians(app.cameraYaw);
    const float pitch = glm::radians(app.cameraPitch);
    const glm::vec3 target(0.0f, 0.0f, 0.2f);

    outPosition = target + glm::vec3(
        app.cameraDistance * std::cos(pitch) * std::sin(yaw),
        app.cameraDistance * std::sin(pitch),
        app.cameraDistance * std::cos(pitch) * std::cos(yaw));

    return glm::lookAt(outPosition, target, glm::vec3(0.0f, 1.0f, 0.0f));
}

glm::mat4 projectionMatrix(const App& app)
{
    const float aspect = static_cast<float>(app.framebufferWidth) /
                         static_cast<float>(std::max(app.framebufferHeight, 1));
    if (app.perspectiveProjection)
        return glm::perspective(glm::radians(42.0f), aspect, 0.1f, 100.0f);

    constexpr float verticalExtent = 5.4f;
    return glm::ortho(-verticalExtent * aspect,
                       verticalExtent * aspect,
                       -verticalExtent,
                       verticalExtent,
                       -30.0f,
                       30.0f);
}

void renderSafe(const App& app)
{
    const Material floor = {{0.055f, 0.075f, 0.105f}, 0.25f, 0.62f};
    const Material body = {{0.16f, 0.23f, 0.30f}, 0.88f, 0.28f, {}, true};
    const Material doorSteel = {{0.23f, 0.32f, 0.41f}, 0.84f, 0.23f, {}, true};
    const Material frame = {{0.36f, 0.46f, 0.56f}, 0.88f, 0.22f};
    const Material darkMetal = {{0.070f, 0.105f, 0.145f}, 0.72f, 0.32f};
    const Material brass = {{0.70f, 0.34f, 0.055f}, 0.88f, 0.2f};
    const Material brassBright = {{0.94f, 0.58f, 0.14f}, 0.92f, 0.16f};
    const Material innerLock = {{0.16f, 0.22f, 0.28f}, 0.75f, 0.26f};
    const Material compartment = {{0.035f, 0.050f, 0.070f}, 0.50f, 0.42f};
    const Material shelf = {{0.22f, 0.29f, 0.35f}, 0.82f, 0.24f};
    const Material gold = {{0.95f, 0.55f, 0.08f}, 0.90f, 0.14f};

    drawMesh(app, app.cube, transform({0.0f, -2.72f, -0.1f}, {8.2f, 0.22f, 6.0f}), floor);
    drawMesh(app, app.cube, transform({0.0f, -2.52f, -0.1f}, {6.45f, 0.28f, 3.05f}), darkMetal);

    drawMesh(app, app.cube, transform({0.0f, 0.0f, -1.05f}, {6.0f, 5.0f, 0.40f}), body);
    drawMesh(app, app.cube, transform({-2.70f, 0.0f, 0.0f}, {0.60f, 5.0f, 2.60f}), body);
    drawMesh(app, app.cube, transform({ 2.70f, 0.0f, 0.0f}, {0.60f, 5.0f, 2.60f}), body);
    drawMesh(app, app.cube, transform({0.0f,  2.20f, 0.0f}, {4.80f, 0.60f, 2.60f}), body);
    drawMesh(app, app.cube, transform({0.0f, -2.20f, 0.0f}, {4.80f, 0.60f, 2.60f}), body);
    drawMesh(app, app.cube, transform({0.0f, 2.28f, 1.39f}, {5.85f, 0.18f, 0.24f}), frame);
    drawMesh(app, app.cube, transform({0.0f, -2.28f, 1.39f}, {5.85f, 0.18f, 0.24f}), frame);
    drawMesh(app, app.cube, transform({-2.80f, 0.0f, 1.39f}, {0.18f, 4.72f, 0.24f}), frame);
    drawMesh(app, app.cube, transform({2.80f, 0.0f, 1.39f}, {0.18f, 4.72f, 0.24f}), frame);

    drawMesh(app, app.cube, transform({-1.58f, 0.0f, 0.95f}, {1.85f, 3.55f, 0.12f}), innerLock);

    drawMesh(app, app.cube, transform({0.88f, -0.35f, -0.80f}, {3.00f, 2.70f, 0.12f}), compartment);
    drawMesh(app, app.cube, transform({0.88f, -1.53f, 0.10f}, {3.10f, 0.15f, 1.85f}), shelf);
    drawMesh(app, app.cube, transform({-0.64f, -0.35f, 0.10f}, {0.12f, 2.55f, 1.85f}), shelf);
    drawMesh(app, app.cube, transform({2.40f, -0.35f, 0.10f}, {0.12f, 2.55f, 1.85f}), shelf);
    drawMesh(app, app.cube, transform({0.88f, 0.92f, 0.10f}, {3.10f, 0.12f, 1.85f}), shelf);

    glm::mat4 goldBarOne = transform({0.25f, -1.29f, 0.46f}, {0.92f, 0.30f, 0.52f});
    goldBarOne = rotated(goldBarOne, -6.0f, {0.0f, 0.0f, 1.0f});
    drawMesh(app, app.goldIngot, goldBarOne, gold);

    glm::mat4 goldBarTwo = transform({1.35f, -1.29f, 0.20f}, {0.92f, 0.30f, 0.52f});
    goldBarTwo = rotated(goldBarTwo, 5.0f, {0.0f, 0.0f, 1.0f});
    drawMesh(app, app.goldIngot, goldBarTwo, gold);

    glm::mat4 goldBarThree = transform({0.80f, -0.99f, 0.38f}, {0.92f, 0.30f, 0.52f});
    goldBarThree = rotated(goldBarThree, -2.0f, {0.0f, 0.0f, 1.0f});
    drawMesh(app, app.goldIngot, goldBarThree, gold);

    //вътрешни жълти механични камери на заключващия механизъм
    for (int index = 0; index < 3; ++index)
    {
        const float y = 1.08f - static_cast<float>(index) * 1.08f;
        glm::mat4 cam = transform({-1.58f, y, 1.18f}, {0.78f, 0.78f, 0.16f});
        drawMesh(app, app.cylinder, cam, brass);
    }

    const float openDoorAngle = smooth01((app.unlockProgress - 0.53f) / 0.47f) * 105.0f;
    const float explodedForwardDistance = app.inspectionAmount * 0.45f;
    const float explodedSideDistance = app.inspectionAmount * 4.35f;

    glm::mat4 doorRoot(1.0f);
    doorRoot = translated(doorRoot,
                          {-2.58f + explodedSideDistance,
                           0.0f,
                           1.49f + explodedForwardDistance});
    doorRoot = rotated(doorRoot, -openDoorAngle, {0.0f, 1.0f, 0.0f});
    doorRoot = translated(doorRoot, {2.58f, 0.0f, 0.0f});

    drawMesh(app, app.cube, scaled(doorRoot, {5.16f, 4.18f, 0.32f}), doorSteel);
    drawMesh(app,
             app.cube,
             scaled(translated(doorRoot, {0.0f, 0.0f, 0.19f}), {4.72f, 3.72f, 0.09f}),
             darkMetal);

    //пантите
    for (const float y : {-1.35f, 0.0f, 1.35f})
    {
        glm::mat4 hinge = translated(doorRoot, {-2.54f, y, -0.05f});
        hinge = rotated(hinge, 90.0f, {1.0f, 0.0f, 0.0f});
        hinge = scaled(hinge, {0.16f, 0.16f, 0.55f});
        drawMesh(app, app.cylinder, hinge, brass);
    }

    //трите външни кодови диска
    const std::array<float, 3> dialY = {1.08f, 0.0f, -1.08f};
    constexpr float dialX = -1.58f; //всички са на една и съща позиция спрямо вратата
    for (int index = 0; index < 3; ++index)
    {
        const bool selected = app.selectedDial == index;
        const Material rim = selected ? brassBright : frame;
        //рисува външния пръстен
        glm::mat4 ring = scaled(translated(doorRoot, {dialX, dialY[index], 0.39f}),
                                {1.50f, 1.50f, 0.13f});
        drawMesh(app, app.cylinder, ring, rim);
        //основният тъмен диск
        glm::mat4 dial = translated(doorRoot, {dialX, dialY[index], 0.51f});
        dial = rotated(dial, app.dialAngles[index], {0.0f, 0.0f, 1.0f});
        dial = scaled(dial, {1.24f, 1.24f, 0.28f});
        drawMesh(app, app.cylinder, dial, darkMetal);

        glm::mat4 dialCenter = scaled(translated(doorRoot, {dialX, dialY[index], 0.66f}),
                                      {0.63f, 0.63f, 0.12f});
        drawMesh(app, app.cylinder, dialCenter, brass);

        glm::mat4 rotatingMarker = translated(doorRoot, {dialX, dialY[index], 0.71f});
        rotatingMarker = rotated(rotatingMarker, app.dialAngles[index], {0.0f, 0.0f, 1.0f});
        rotatingMarker = translated(rotatingMarker, {0.0f, 0.42f, 0.0f});
        rotatingMarker = scaled(rotatingMarker, {0.10f, 0.28f, 0.08f});
        drawMesh(app, app.cube, rotatingMarker, brassBright);

    }

    const float boltRetraction = smooth01(app.unlockProgress / 0.56f);
    for (const float y : {-1.10f, 0.0f, 1.10f})
    {
        glm::mat4 housing = scaled(translated(doorRoot, {2.08f, y, 0.38f}), {0.58f, 0.30f, 0.21f});
        drawMesh(app, app.cube, housing, frame);

        glm::mat4 bolt = translated(doorRoot, {2.32f - boltRetraction * 0.92f, y, 0.57f});
        bolt = scaled(bolt, {0.88f, 0.16f, 0.16f});
        drawMesh(app, app.cube, bolt, brass);
    }

    const float handleTurn = smooth01((app.unlockProgress - 0.30f) / 0.32f) * 68.0f;
    glm::mat4 handleBase = scaled(translated(doorRoot, {1.25f, -0.10f, 0.49f}), {0.72f, 0.72f, 0.24f});
    drawMesh(app, app.cylinder, handleBase, brass);

    glm::mat4 handle = translated(doorRoot, {1.25f, -0.10f, 0.69f});
    handle = rotated(handle, -handleTurn, {0.0f, 0.0f, 1.0f});
    handle = scaled(handle, {0.17f, 1.38f, 0.16f});
    drawMesh(app, app.cube, handle, brassBright);

    const bool errorActive = app.state == LockState::Error && std::fmod(app.errorTimeLeft * 8.0f, 2.0f) > 1.0f;
    const bool unlocked = app.state == LockState::Unlocked || app.state == LockState::Unlocking;
    const Material indicator = errorActive
        ? Material{{0.8f, 0.03f, 0.02f}, 0.1f, 0.35f, {1.0f, 0.0f, 0.0f}}
        : unlocked
            ? Material{{0.02f, 0.55f, 0.16f}, 0.15f, 0.3f, {0.0f, 0.8f, 0.15f}}
            : Material{{0.18f, 0.25f, 0.31f}, 0.7f, 0.3f};

    drawMesh(app,
             app.cylinder,
             scaled(translated(doorRoot, {1.95f, 1.55f, 0.30f}), {0.32f, 0.32f, 0.15f}),
             frame);
    drawMesh(app,
             app.cylinder,
             scaled(translated(doorRoot, {1.95f, 1.55f, 0.42f}), {0.23f, 0.23f, 0.12f}),
             indicator);
}

void render(App& app)
{
    glClearColor(0.020f, 0.032f, 0.055f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glPolygonMode(GL_FRONT_AND_BACK, app.wireframe ? GL_LINE : GL_FILL);

    glUseProgram(app.program);
    glm::vec3 viewPosition;
    setMat4(app.program, "uView", cameraView(app, viewPosition));
    setMat4(app.program, "uProjection", projectionMatrix(app));
    setVec3(app.program, "uViewPosition", viewPosition);
    setVec3(app.program, "uLightPosition", {3.8f, 6.5f, 7.2f});
    setVec3(app.program, "uLightColor", {1.0f, 0.90f, 0.76f});

    renderSafe(app);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}

//подготвя ресурсите преди да е започнал главният цикъл на програмата
bool initialise(App& app)
{
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_MULTISAMPLE);
    glDisable(GL_CULL_FACE);

    app.cube = createCube();
    app.cylinder = createCylinder(48);
    app.goldIngot = createGoldIngot();
    app.program = createProgram(std::string(SAFE_SHADER_DIR) + "/safe.vert",
                                std::string(SAFE_SHADER_DIR) + "/safe.frag");
    return app.program != 0;
}

//освобождава ресурсите след затваряне на програмата
void cleanup(App& app)
{
    destroyMesh(app.cube);
    destroyMesh(app.cylinder);
    destroyMesh(app.goldIngot);
    if (app.program != 0)
        glDeleteProgram(app.program);
    if (app.window != nullptr)
        glfwDestroyWindow(app.window);
    glfwTerminate();
}

} 

int main()
{
    glfwSetErrorCallback(glfwErrorCallback);
    if (glfwInit() == GLFW_FALSE)
        return EXIT_FAILURE;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SAMPLES, 4);

    App app;
    app.window = glfwCreateWindow(1280, 720, "Mechanical Safe", nullptr, nullptr);
    if (app.window == nullptr)
    {
        glfwTerminate();
        return EXIT_FAILURE;
    }

    glfwMakeContextCurrent(app.window);
    glfwSwapInterval(1);

    if (gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress)) == 0)
    {
        std::cerr << "Failed to initialise GLAD.\n";
        cleanup(app);
        return EXIT_FAILURE;
    }

    glfwSetWindowUserPointer(app.window, &app);
    glfwSetKeyCallback(app.window, keyCallback);
    glfwSetFramebufferSizeCallback(app.window, framebufferSizeCallback);
    glfwSetScrollCallback(app.window, scrollCallback);
    glfwGetFramebufferSize(app.window, &app.framebufferWidth, &app.framebufferHeight);
    glViewport(0, 0, app.framebufferWidth, app.framebufferHeight);

    if (!initialise(app))
    {
        cleanup(app);
        return EXIT_FAILURE;
    }

    printControls();
    double previousTime = glfwGetTime();
    while (glfwWindowShouldClose(app.window) == GLFW_FALSE)
    {
        const double currentTime = glfwGetTime();
        const float deltaSeconds = static_cast<float>(std::min(currentTime - previousTime, 0.1));
        previousTime = currentTime;

        glfwPollEvents();
        update(app, deltaSeconds);
        render(app);
        glfwSwapBuffers(app.window);
    }

    cleanup(app);
    return EXIT_SUCCESS;
}
