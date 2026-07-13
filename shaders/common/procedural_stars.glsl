// Procedural stars are laid out in cubemap-face space instead of latitude /
// longitude space. Equirectangular cells collapse at the zenith and nadir,
// which turns otherwise round stars into long radial streaks.

const float ENGINE_STAR_PI = 3.14159265358979323846;
const float ENGINE_STAR_FACE_RESOLUTION = 208.0;

float EngineStarHash(vec2 p)
{
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

vec2 EngineStarCubeGrid(vec3 direction, out float faceIndex)
{
    vec3 axis = abs(direction);
    vec2 faceUv;
    if (axis.x >= axis.y && axis.x >= axis.z)
    {
        faceUv = direction.zy / max(axis.x, 1e-6);
        faceIndex = direction.x >= 0.0 ? 0.0 : 1.0;
    }
    else if (axis.y >= axis.z)
    {
        faceUv = direction.xz / max(axis.y, 1e-6);
        faceIndex = direction.y >= 0.0 ? 2.0 : 3.0;
    }
    else
    {
        faceUv = direction.xy / max(axis.z, 1e-6);
        faceIndex = direction.z >= 0.0 ? 4.0 : 5.0;
    }
    return (faceUv * 0.5 + 0.5) * ENGINE_STAR_FACE_RESOLUTION;
}

struct EngineProceduralStar
{
    float Shape;
    float Temperature;
    float TwinklePhase;
    float TwinkleSpeed;
};

EngineProceduralStar EngineEvaluateProceduralStar(vec3 direction,
                                                   float density,
                                                   float size)
{
    float faceIndex;
    vec2 starGrid = EngineStarCubeGrid(normalize(direction), faceIndex);
    vec2 starCell = floor(starGrid);
    vec2 starLocal = fract(starGrid) - 0.5;

    // Each face gets a separate, deterministic section of the hash domain.
    vec2 hashCell = starCell + vec2(faceIndex * 277.0, faceIndex * 619.0);
    float seed = EngineStarHash(hashCell);
    float radius = mix(0.045, 0.16, EngineStarHash(hashCell + 19.7))
                 * clamp(size, 0.25, 4.0);
    float antialias = max(length(fwidth(starGrid)) * 0.22, 0.012);
    float core = 1.0 - smoothstep(radius, radius + antialias, length(starLocal));
    float rareBrightness = pow(EngineStarHash(hashCell + 91.3), 8.0);
    float horizontalRay = exp(-abs(starLocal.x) * 34.0)
                        * exp(-abs(starLocal.y) * 5.0);
    float verticalRay = exp(-abs(starLocal.y) * 34.0)
                      * exp(-abs(starLocal.x) * 5.0);

    EngineProceduralStar result;
    result.Shape = max(core, (horizontalRay + verticalRay) * rareBrightness * 0.32)
                 * step(1.0 - clamp(density, 0.0, 0.05), seed);
    result.Temperature = EngineStarHash(hashCell + 47.2);
    result.TwinklePhase = EngineStarHash(hashCell + 73.1) * 2.0 * ENGINE_STAR_PI;
    result.TwinkleSpeed = mix(0.7, 2.4, EngineStarHash(hashCell + 12.4));
    return result;
}
