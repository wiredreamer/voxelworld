export module vw.world:terrain.noise;

import std;

import vw.core;

export namespace vw::ecs {

class perlin_noise {
public:
    explicit perlin_noise(uint32 seed) {
        for (int32 i = 0; i < 256; ++i) {
            perm_[i] = i;
        }

        uint32 state = seed;
        for (int32 i = 255; i > 0; --i) {
            state   = state * 1664525u + 1013904223u;
            int32 j = static_cast<int32>(state % static_cast<uint32>(i + 1));
            std::swap(perm_[i], perm_[j]);
        }

        for (int32 i = 0; i < 256; ++i) {
            perm_[i + 256] = perm_[i];
        }
    }

    [[nodiscard]] auto noise2d(float64 x, float64 y) const -> float64 {
        const int32 xi = static_cast<int32>(std::floor(x)) & 255;
        const int32 yi = static_cast<int32>(std::floor(y)) & 255;

        const float64 xf = x - std::floor(x);
        const float64 yf = y - std::floor(y);

        const float64 u = fade(xf);
        const float64 v = fade(yf);

        const int32 aa = perm_[perm_[xi] + yi];
        const int32 ab = perm_[perm_[xi] + yi + 1];
        const int32 ba = perm_[perm_[xi + 1] + yi];
        const int32 bb = perm_[perm_[xi + 1] + yi + 1];

        const float64 x1 = lerp(u, grad(aa, xf, yf), grad(ba, xf - 1.0, yf));
        const float64 x2 = lerp(u, grad(ab, xf, yf - 1.0), grad(bb, xf - 1.0, yf - 1.0));

        return lerp(v, x1, x2);
    }

    [[nodiscard]] auto noise3d(float64 x, float64 y, float64 z) const -> float64 {
        const int32 xi = static_cast<int32>(std::floor(x)) & 255;
        const int32 yi = static_cast<int32>(std::floor(y)) & 255;
        const int32 zi = static_cast<int32>(std::floor(z)) & 255;

        const float64 xf = x - std::floor(x);
        const float64 yf = y - std::floor(y);
        const float64 zf = z - std::floor(z);

        const float64 u = fade(xf);
        const float64 v = fade(yf);
        const float64 w = fade(zf);

        const int32 a  = perm_[xi] + yi;
        const int32 aa = perm_[a] + zi;
        const int32 ab = perm_[a + 1] + zi;
        const int32 b  = perm_[xi + 1] + yi;
        const int32 ba = perm_[b] + zi;
        const int32 bb = perm_[b + 1] + zi;

        const auto g = [](int32 hash, float64 px, float64 py, float64 pz) -> float64 {
            const int32 h    = hash & 15;
            const float64 gu = h < 8 ? px : py;
            const float64 gv = h < 4 ? py : (h == 12 || h == 14 ? px : pz);
            return ((h & 1) != 0 ? -gu : gu) + ((h & 2) != 0 ? -gv : gv);
        };

        const float64 x1 = lerp(u, g(perm_[aa], xf, yf, zf), g(perm_[ba], xf - 1.0, yf, zf));
        const float64 x2 =
            lerp(u, g(perm_[ab], xf, yf - 1.0, zf), g(perm_[bb], xf - 1.0, yf - 1.0, zf));
        const float64 x3 = lerp(
            u, g(perm_[aa + 1], xf, yf, zf - 1.0), g(perm_[ba + 1], xf - 1.0, yf, zf - 1.0)
        );
        const float64 x4 = lerp(
            u, g(perm_[ab + 1], xf, yf - 1.0, zf - 1.0),
            g(perm_[bb + 1], xf - 1.0, yf - 1.0, zf - 1.0)
        );

        return lerp(w, lerp(v, x1, x2), lerp(v, x3, x4));
    }

    [[nodiscard]] auto fractal(
        float64 x, float64 y, int32 octaves, float64 persistence = 0.5
    ) const -> float64 {
        float64 total   = 0.0;
        float64 freq    = 1.0;
        float64 amp     = 1.0;
        float64 max_amp = 0.0;

        for (int32 i = 0; i < octaves; ++i) {
            total += noise2d(x * freq, y * freq) * amp;
            max_amp += amp;
            freq *= 2.0;
            amp *= persistence;
        }

        return total / max_amp;
    }

    [[nodiscard]] auto ridged(
        float64 x, float64 y, int32 octaves, float64 persistence = 0.5
    ) const -> float64 {
        float64 total   = 0.0;
        float64 freq    = 1.0;
        float64 amp     = 1.0;
        float64 max_amp = 0.0;

        for (int32 i = 0; i < octaves; ++i) {
            const float64 n = 1.0 - std::abs(noise2d(x * freq, y * freq));
            total += n * n * amp;
            max_amp += amp;
            freq *= 2.0;
            amp *= persistence;
        }

        return total / max_amp;
    }

private:
    [[nodiscard]] static auto fade(float64 t) -> float64 {
        return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
    }

    [[nodiscard]] static auto lerp(float64 t, float64 a, float64 b) -> float64 {
        return a + t * (b - a);
    }

    [[nodiscard]] static auto grad(int32 hash, float64 x, float64 y) -> float64 {
        const int32 h   = hash & 3;
        const float64 u = h < 2 ? x : y;
        const float64 v = h < 2 ? y : x;
        return ((h & 1) != 0 ? -u : u) + ((h & 2) != 0 ? -v : v);
    }

    std::array<int32, 512> perm_{};
};

}  // namespace vw::ecs
