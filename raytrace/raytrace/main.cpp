#include <vector>
#include <map>
#include <iostream>
#include <sstream>
#include <fstream>

#include <SFML/Graphics.hpp>
#include <GL/glew.h>

#include "sdf_compile.h"

const bool kRenderToImages = false;
const int kSamplesPerImage = 50;
const float kFrameRate = 1.0 / 30.0;
const float kSecondsToRender = 10.f;

const int kWindowWidth = 960;
const int kWindowHeight = 540;
const int kBuffers = 2;
const int kSamplesPerRound = 100;

const int LIT = 0;
const int BOX = LIT + 1;
const int OCTOHEDRON_B = BOX + 1;
const int PLANE = OCTOHEDRON_B + 1;
const int SPHERE = PLANE + 1;
const int UNION = SPHERE + 1;
const int NEG = UNION + 1;
const int ROT = NEG + 1;
const int TRANS = ROT + 1;
const int TIME = TRANS + 1;
// Always should be last
const int BASE_MATERIAL = TIME + 1;

const std::map<int, int> kNetStackSize {
    { LIT, 1 },
    { BOX, -2 },
    { OCTOHEDRON_B, 0 },
    { PLANE, -3 },
    { SPHERE, 0 },
    { UNION, -1 },
    { NEG, 0 },
    { ROT, -4 },
    { TRANS, 0 }, // TODO: correct when implemented correctly
    { TIME, 1 },
    { BASE_MATERIAL, 0 },
    { BASE_MATERIAL + 1, 0 },
    { BASE_MATERIAL + 2, 0 },
    { BASE_MATERIAL + 3, 0 },
};
/*
float literals[] = { 
    0.25, 0.25, 0.25, // Box bounds
    0.5, // Octohedron side length
    0.0, 1.0, 0.0, 45.0 // rotation axis and angle
};
int ops[] = { LIT, LIT, LIT, BASE_MATERIAL + 0, TRANS, BOX, LIT, BASE_MATERIAL + 1, LIT, LIT, LIT, LIT, ROT, OCTOHEDRON_B, UNION };

float literals[] = {
    0, -1, 0, 5.5, // emitter plane
    // wall planes
    0,  1, 0, 4.5,

    -1, 0, 0, 4.5,
     1, 0, 0, 4.5,

    0, 0,  1, 14.5,
    0, 0, -1, 4.5,

    1, // sphere
};
int ops[] = { 
    LIT, LIT, LIT, LIT, BASE_MATERIAL + 3, PLANE, // emitter
    LIT, LIT, LIT, LIT, BASE_MATERIAL + 2, PLANE,
        UNION,
    LIT, LIT, LIT, LIT, BASE_MATERIAL + 2, PLANE,
    LIT, LIT, LIT, LIT, BASE_MATERIAL + 2, PLANE,
        UNION,
    LIT, LIT, LIT, LIT, BASE_MATERIAL + 2, PLANE,
    LIT, LIT, LIT, LIT, BASE_MATERIAL + 2, PLANE,
        UNION,
            UNION,
                UNION,
                LIT, BASE_MATERIAL, SPHERE, 
                    UNION,
};*/

float literals[] = {
    0, -1, 0, 4.5, // emitter plane
    // wall box
    5, 5, 5,
    0, 1, 0,
    1, // sphere
};
int ops[] = {
    LIT, LIT, LIT, LIT, BASE_MATERIAL + 3, PLANE, // emitter
    LIT, LIT, LIT, BASE_MATERIAL + 2, LIT, LIT, LIT, TIME, ROT, BOX, NEG,
        UNION,
        LIT, BASE_MATERIAL, SPHERE,
            UNION,
};

const std::vector<std::string> kShaderPartsPaths {
    "raytrace.frag.part",
    "random.frag.part",
    "camera.frag.part",
    "materials.frag.part",
    "common.frag.part",
    "scattering.frag.part",
    "exact_surfaces.frag.part",
    "sdf.frag.part",
};

const std::vector<std::string> kShaderLaterPartsPaths {
    "sdf_eval.frag.part",
    "main.frag.part",
};

std::string load_shader_part(const std::string& part_path) {
    std::fstream f(part_path);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

std::string load_shader_from_parts(const std::vector<std::string>& part_paths) {
    std::stringstream ss;
    for (const auto& p : part_paths) {
        ss << load_shader_part(p);
    }

    return ss.str();
}

void print_lines(const std::string& shader, int line) {
    std::stringstream ss(shader);
    std::string l;
    int line_count = 0;

    while (std::getline(ss, l, '\n')) {
        ++line_count;
        if (line_count >= line - 5 && line_count <= line + 5) {
            std::cout << line_count << ": " << l << std::endl;
        }
    }
}

int main() {
    sf::RenderWindow window(sf::VideoMode(kWindowWidth, kWindowHeight), "raytrace");

    GLenum err = glewInit();
    if (err != GLEW_OK) {
        std::cerr << "Failed to init GLEW" << std::endl;
        return -1;
    }

    sf::RenderTexture buffers[kBuffers];
    for (int i = 0; i < kBuffers; ++i) {
        if (!buffers[i].create(kWindowWidth, kWindowHeight)) {
            std::cerr << "Can't create texture for buffer " << i << std::endl;
            return -1;
        }
    }

    if (!sf::Shader::isAvailable()) {
        std::cerr << "Shaders not available" << std::endl;
        return -1;
    }

    std::string scene = load_shader_part("scene.sc");

    std::string compiled = compile_scene(scene);
    std::cout << compiled;

    sf::Shader shader;
    std::string shader_content = load_shader_from_parts(kShaderPartsPaths);
    shader_content += compiled;
    shader_content += load_shader_from_parts(kShaderLaterPartsPaths);

    print_lines(shader_content, 442);

    if (!shader.loadFromMemory(shader_content, sf::Shader::Fragment)) {
        std::cerr << "Can't load fragment shader" << std::endl;
        return -1;
    }

    shader.setUniform("samples_per_round", kSamplesPerRound);
    shader.setUniform("width", kWindowWidth);
    shader.setUniform("height", kWindowHeight);

    int stack_size = 0;
    int max_stack_size = std::numeric_limits<int>::min();
    for (int i = 0; i < (sizeof(ops) / sizeof(ops[0])); ++i) {
        stack_size += kNetStackSize.at(ops[i]);
        max_stack_size = std::max(max_stack_size, stack_size);
    }

    // TODO: Investigate using a texture to store the stacks. An SSBO might work as well, but either case needs to ensure that the data being written doesn't overwrite the data from the other shader executions.
    std::cerr << "Max stack size used is " << max_stack_size << ". It's possible that the rendering shader require more, double check that the |stack| array is large enough to accomodate this stack size. Larger stack sizes have performance implications, so it should always be set to be as small as possible." << std::endl;
    if (stack_size != 1) {
        std::cerr << "Stack size should be 1 after scene traversing, is actually " << stack_size << std::endl;
    }

    sf::RectangleShape render_target(sf::Vector2f(kWindowWidth, kWindowHeight));
    render_target.setFillColor(sf::Color(150, 50, 250));

    sf::Clock fps_clock;
    sf::Clock clock;
    float prev_frame_time = 0.f;
    float total_fps_gather_time = 0.f;
    int frames = 0;
    int total_frames = 0;
    int samples_in_image = 0;
    bool animating = false;
    float elapsed = 0.f;
    int frames_rendered_to_file = 0;

    GLuint ssbo[2];
    glGenBuffers(2, ssbo);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo[0]);
    glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(literals), literals, GL_STATIC_READ);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, ssbo[0]);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo[1]);
    glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(ops), ops, GL_STATIC_READ);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, ssbo[1]);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    while (window.isOpen()) {
        sf::Event event;
        while (window.pollEvent(event)) {
            if (event.type == sf::Event::Closed)
                window.close();
            if (event.type == sf::Event::KeyReleased) {
                if (event.key.code == sf::Keyboard::Space) {
                    animating = !animating;
                }
            }
        }

        // FPS
        ++frames;
        ++total_frames;
        if (fps_clock.getElapsedTime().asSeconds() >= 1.0f) {
            std::cerr << "FPS: " << frames << std::endl;
            fps_clock.restart();
            frames = 0;
        }

        //std::cerr << "Frame # " << total_frames << std::endl;
        
        int source_buffer_idx = (total_frames - 1) % kBuffers;
        int target_buffer_idx = total_frames % kBuffers;

        if (animating) elapsed = clock.getElapsedTime().asSeconds();
        shader.setUniform("elapsed_time", elapsed);

        if (animating && !kRenderToImages) {
            samples_in_image = 0;
            buffers[source_buffer_idx].clear();
            buffers[source_buffer_idx].display();
        }

        samples_in_image += kSamplesPerRound;

        shader.setUniform("partial_render", buffers[source_buffer_idx].getTexture());
        shader.setUniform("frame_count", total_frames);
        shader.setUniform("samples_in_image", samples_in_image);

        buffers[target_buffer_idx].clear(sf::Color::Red);
        buffers[target_buffer_idx].draw(render_target, &shader);
        buffers[target_buffer_idx].display();

        if (samples_in_image >= kSamplesPerImage && kRenderToImages) {
            ++frames_rendered_to_file;
            elapsed += kFrameRate;
            samples_in_image = 0;
            std::stringstream ss;
            ss << "out/frames_" << frames_rendered_to_file << ".png";
            buffers[target_buffer_idx].getTexture().copyToImage().saveToFile(ss.str());
            if (kSecondsToRender <= elapsed) {
                return 0;
            }
        }

        window.draw(sf::Sprite(buffers[target_buffer_idx].getTexture()));
        window.display();

        //sf::sleep(sf::seconds(0.2));
    }

    return 0;
}