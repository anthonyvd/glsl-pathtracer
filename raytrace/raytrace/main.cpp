#include <vector>
#include <iostream>

#include <SFML/Graphics.hpp>
#include <GL/glew.h>

const int kWindowWidth = 960;
const int kWindowHeight = 540;
const int kBuffers = 2;
const int kSamplesPerRound = 1;

const int LIT = 0;
const int BOX = LIT + 1;
const int OCTOHEDRON_B = BOX + 1;
const int UNION = OCTOHEDRON_B + 1;
const int ROT = UNION + 1;
const int TRANS = ROT + 1;
// Always should be last
const int BASE_MATERIAL = TRANS + 1;

float literals[] = { 0.25, 0.25, 0.25, 0.5, 0.0, 1.0, 0.0, 45.0 };
int ops[] = { LIT, LIT, LIT, BASE_MATERIAL + 0, TRANS, BOX, LIT, BASE_MATERIAL + 1, LIT, LIT, LIT, LIT, ROT, OCTOHEDRON_B, UNION };

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

    sf::Shader shader;
    if (!shader.loadFromFile("raytrace.frag", sf::Shader::Fragment)) {
        std::cerr << "Can't load fragment shader" << std::endl;
        return -1;
    }

    shader.setUniform("samples_per_round", kSamplesPerRound);

    sf::RectangleShape render_target(sf::Vector2f(kWindowWidth, 540));
    render_target.setFillColor(sf::Color(150, 50, 250));

    sf::Clock fps_clock;
    sf::Clock clock;
    float prev_frame_time = 0.f;
    float total_fps_gather_time = 0.f;
    int frames = 0;
    int total_frames = 0;
    int samples_in_image = 0;
    bool animating = false;

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

        if (animating) {
            samples_in_image = 0;
            shader.setUniform("elapsed_time", clock.getElapsedTime().asSeconds());
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

        window.clear();
        window.draw(sf::Sprite(buffers[target_buffer_idx].getTexture()));
        window.display();

        //sf::sleep(sf::seconds(0.2));
    }

    return 0;
}