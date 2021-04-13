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
const int kSamplesPerRound = 10;

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

    sf::Shader shader;
    std::string shader_content = load_shader_from_parts(kShaderPartsPaths);
    shader_content += compiled;
    shader_content += load_shader_from_parts(kShaderLaterPartsPaths);

    if (!shader.loadFromMemory(shader_content, sf::Shader::Fragment)) {
        std::cerr << "Can't load fragment shader" << std::endl;
        return -1;
    }

    shader.setUniform("samples_per_round", kSamplesPerRound);
    shader.setUniform("width", kWindowWidth);
    shader.setUniform("height", kWindowHeight);

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
    }

    return 0;
}