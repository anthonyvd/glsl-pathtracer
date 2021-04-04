#include <iostream>

#include <SFML/Graphics.hpp>

const int kWindowWidth = 960;
const int kWindowHeight = 540;
const int kBuffers = 1;
const int kSamplesPerRound = 50;

int main() {
    sf::RenderWindow window(sf::VideoMode(kWindowWidth, kWindowHeight), "raytrace");
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

    sf::Clock clock;
    float prev_frame_time = 0.f;
    float total_fps_gather_time = 0.f;
    int frames = 0;
    int total_frames = 0;

    while (window.isOpen()) {
        sf::Event event;
        while (window.pollEvent(event)) {
            if (event.type == sf::Event::Closed)
                window.close();
        }

        // FPS
        ++frames;
        ++total_frames;
        if (clock.getElapsedTime().asSeconds() >= 1.0f) {
            std::cerr << "FPS: " << frames << std::endl;
            clock.restart();
            frames = 0;
        }

        buffers[0].clear(sf::Color::Red);
        buffers[0].draw(render_target, &shader);
        buffers[0].display();

        //shader.setUniform("frames_count", 10);

        window.clear();
        window.draw(sf::Sprite(buffers[0].getTexture()));
        window.display();
    }

    return 0;
}