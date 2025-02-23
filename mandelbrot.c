#include <SDL3/SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdbool.h>

#define WIDTH 800
#define HEIGHT 800
#define MAX_ITER 1000
#define NUM_THREADS 16
#define CACHELINE_SIZE 64

// Mandelbrot data.
typedef struct {
    // View and zoom level.
    double centerX, centerY, zoom;

    // Pixel data for the image.
    uint32_t* pixels;
} Mandelbrot;

// Thread specific data.
typedef struct {
    // Mandelbrot object to work on.
    Mandelbrot* mandelbrot;

    // Vertical slice of the image to be rendered by this thread.
    int startY, endY;

    // Cache line padding to avoid false sharing between threads.
    char padding[CACHELINE_SIZE];
} ThreadData;

static Uint32 colorPalette[MAX_ITER + 1];

// Generate the color palette based on the iteration count.
void generateColorPalette() {
    for (int i = 0; i <= MAX_ITER; i++) {
        double t = (double)i / MAX_ITER;
        uint8_t r = (uint8_t)(9 * (1 - t) * t * t * t * 255);
        uint8_t g = (uint8_t)(15 * (1 - t) * (1 - t) * t * t * 255);
        uint8_t b = (uint8_t)(8.5 * (1 - t) * (1 - t) * (1 - t) * t * 255);
        colorPalette[i] = (0xFF << 24) | (r << 16) | (g << 8) | b;
    }
}

// Thread to render a portion of the Mandelbrot set.
void* renderPart(void* arg) {
    ThreadData* data = (ThreadData*)arg;
    Mandelbrot* m = data->mandelbrot;
    uint32_t* pixels = m->pixels;

    // Rows.
    for (int y = data->startY; y < data->endY; y++) {
        double imag = (y - HEIGHT / 2.0) / (0.5 * m->zoom * HEIGHT) + m->centerY;

        // Start of the row.
        uint32_t* row = &pixels[y * WIDTH];

        // Columns.
        for (int x = 0; x < WIDTH; x++) {
            double real = (x - WIDTH / 2.0) / (0.5 * m->zoom * WIDTH) + m->centerX;
            double zr = 0.0, zi = 0.0;
            int iter = 0;
            double zr2, zi2;

            // Unrolled two iterations of Mandelbrot set.
            while (iter < MAX_ITER) {
                zr2 = zr * zr;
                zi2 = zi * zi;
                if (zr2 + zi2 >= 4.0) break;

                // Unroll 4 iterations.
                for (int i = 0; i < 2 && iter < MAX_ITER; i++) {
                    zi = zr * zi;
                    zi += zi + imag;
                    zr = zr2 - zi2 + real;
                    iter++;

                    zr2 = zr * zr;
                    zi2 = zi * zi;
                    if (zr2 + zi2 >= 4.0) break;
                }
            }

            // Assign color based on the iteration count.
            row[x] = colorPalette[iter];
        }
    }
    return NULL;
}

// Draw the Mandelbrot set on the texture.
void drawMandelbrot(Mandelbrot* m, SDL_Texture* texture) {
    pthread_t threads[NUM_THREADS];
    ThreadData threadData[NUM_THREADS];

    // Height of the slice for each thread.
    int sliceHeight = HEIGHT / NUM_THREADS;

    // Rectangles for updating parts of the texture.
    SDL_Rect updateRects[NUM_THREADS];

    // Create threads.
    for (int i = 0; i < NUM_THREADS; i++) {
        threadData[i].mandelbrot = m;
        threadData[i].startY = i * sliceHeight;
        threadData[i].endY = (i == NUM_THREADS - 1) ? HEIGHT : (i + 1) * sliceHeight;
        pthread_create(&threads[i], NULL, renderPart, &threadData[i]);

        // Define update rectangles for each thread's region.
        updateRects[i].x = 0;
        updateRects[i].y = threadData[i].startY;
        updateRects[i].w = WIDTH;
        updateRects[i].h = threadData[i].endY - threadData[i].startY;
    }

    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);

        // Update only the modified region.
        SDL_UpdateTexture(texture, &updateRects[i], m->pixels + updateRects[i].y * WIDTH, WIDTH * sizeof(uint32_t));
    }
}

int main() {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "SDL Init Error: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow("Mandelbrot Set", WIDTH, HEIGHT, SDL_WINDOW_OPENGL);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, NULL);
    SDL_Texture* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, WIDTH, HEIGHT);

    if (!window || !renderer || !texture) {
        fprintf(stderr, "Failed to create window/renderer/texture: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    // Use posix_memalign for proper alignment.
    Mandelbrot mandelbrot = {-0.5, 0.0, 1.0, NULL};
    int result = posix_memalign((void**)&mandelbrot.pixels, CACHELINE_SIZE, WIDTH * HEIGHT * sizeof(uint32_t));
    if (result != 0 || !mandelbrot.pixels) {
        fprintf(stderr, "Memory allocation failed\n");
        SDL_Quit();
        return 1;
    }

    generateColorPalette();

    int running = true, dragging = false;
    int prevMouseX = 0, prevMouseY = 0;
    SDL_Event event;

    while (running) {
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_EVENT_QUIT:
                    running = false;
                    break;
                case SDL_EVENT_MOUSE_WHEEL:
                    // Zoom in or out based on mouse wheel direction.
                    mandelbrot.zoom *= (event.wheel.y > 0) ? 1.1 : 1 / 1.1;
                    break;
                case SDL_EVENT_MOUSE_BUTTON_DOWN:
                    // Start dragging on left mouse button press
                    if (event.button.button == SDL_BUTTON_LEFT) {
                        dragging = true;
                        prevMouseX = event.button.x;
                        prevMouseY = event.button.y;
                    }
                    break;
                case SDL_EVENT_MOUSE_BUTTON_UP:
                    // Stop dragging on mouse button release.
                    dragging = false;
                    break;
                case SDL_EVENT_MOUSE_MOTION:
                    // Update center if dragging.
                    if (dragging) {
                        mandelbrot.centerX -= (event.motion.x - prevMouseX) / (mandelbrot.zoom * WIDTH);
                        mandelbrot.centerY -= (event.motion.y - prevMouseY) / (mandelbrot.zoom * HEIGHT);
                        prevMouseX = event.motion.x;
                        prevMouseY = event.motion.y;
                    }
                    break;
            }
        }

        // Render the Mandelbrot set and update the screen.
        drawMandelbrot(&mandelbrot, texture);
        SDL_RenderClear(renderer);
        SDL_RenderTexture(renderer, texture, NULL, NULL);
        SDL_RenderPresent(renderer);
    }

    free(mandelbrot.pixels);
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
