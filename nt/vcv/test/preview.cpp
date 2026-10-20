// Panel preview with Rack's own SVG renderer (nanosvg, 75 dpi like Rack):
// panel + component SVGs at their places, written as a PPM (RGB).
//   ./preview <rack/res/ComponentLibrary> <panel.svg> <parts.txt> <scale> out.ppm
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#define NANOSVG_IMPLEMENTATION
#include "nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvgrast.h"

static int W, H;
static std::vector<float> canvas; // RGB 0…1

static void Blit(const char* path, float cx_mm, float cy_mm, float scale, bool centre)
{
    NSVGimage* img = nsvgParseFromFile(path, "px", 75.f);
    if(!img)
    {
        std::fprintf(stderr, "cannot load %s\n", path);
        return;
    }
    const int        w = static_cast<int>(std::ceil(img->width * scale)), h = static_cast<int>(std::ceil(img->height * scale));
    std::vector<unsigned char> buf(static_cast<size_t>(w) * h * 4);
    NSVGrasterizer*  r = nsvgCreateRasterizer();
    nsvgRasterize(r, img, 0, 0, scale, buf.data(), w, h, w * 4);
    const float px = 75.f / 25.4f * scale;
    const int   x0 = static_cast<int>(std::lround(cx_mm * px - (centre ? w / 2.f : 0.f)));
    const int   y0 = static_cast<int>(std::lround(cy_mm * px - (centre ? h / 2.f : 0.f)));
    for(int y = 0; y < h; ++y)
        for(int x = 0; x < w; ++x)
        {
            const int X = x0 + x, Y = y0 + y;
            if(X < 0 || Y < 0 || X >= W || Y >= H)
                continue;
            const unsigned char* p = &buf[(static_cast<size_t>(y) * w + x) * 4];
            const float          a = p[3] / 255.f;
            for(int c = 0; c < 3; ++c)
            {
                // nanosvg output is premultiplied
                float& d = canvas[(static_cast<size_t>(Y) * W + X) * 3 + c];
                d        = p[c] / 255.f + d * (1.f - a);
            }
        }
    nsvgDeleteRasterizer(r);
    nsvgDelete(img);
}

int main(int argc, char** argv)
{
    if(argc < 6)
        return 1;
    const std::string lib   = argv[1];
    const float       scale = std::atof(argv[4]);
    NSVGimage*        panel = nsvgParseFromFile(argv[2], "px", 75.f);
    W                       = static_cast<int>(std::ceil(panel->width * scale));
    H                       = static_cast<int>(std::ceil(panel->height * scale));
    nsvgDelete(panel);
    canvas.assign(static_cast<size_t>(W) * H * 3, 0.f);
    Blit(argv[2], 0, 0, scale, false);
    FILE* f = std::fopen(argv[3], "r");
    char  name[128];
    float x, y, w, h;
    while(std::fscanf(f, "%127s", name) == 1)
    {
        if(!std::strcmp(name, "DISPLAY"))
        {
            std::fscanf(f, "%f %f %f %f", &x, &y, &w, &h);
            std::printf("DISPLAY %d %d %d %d\n", static_cast<int>(x * 75 / 25.4 * scale), static_cast<int>(y * 75 / 25.4 * scale),
                        static_cast<int>(w * 75 / 25.4 * scale), static_cast<int>(h * 75 / 25.4 * scale));
            continue;
        }
        std::fscanf(f, "%f %f", &x, &y);
        std::string p = lib + "/" + name + ".svg";
        // Knobs: background first, as Rack draws them.
        std::string bg = lib + "/" + name + "_bg.svg";
        if(FILE* t = std::fopen(bg.c_str(), "r"))
        {
            std::fclose(t);
            Blit(bg.c_str(), x, y, scale, true);
        }
        Blit(p.c_str(), x, y, scale, true);
    }
    FILE* o = std::fopen(argv[5], "wb");
    std::fprintf(o, "P6\n%d %d\n255\n", W, H);
    for(float v : canvas)
        std::fputc(static_cast<int>(std::fmin(1.f, std::fmax(0.f, v)) * 255.f + 0.5f), o);
    std::fclose(o);
    return 0;
}
