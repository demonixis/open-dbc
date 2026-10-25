#version 450
layout(set = 2, binding = 0) uniform sampler2D Source;
layout(set = 3, binding = 0, std140) uniform Transfer
{
    vec4 GainAndDestinationSrgb;
    vec4 SourceSrgb;
};
layout(location = 0) in vec2 Uv;
layout(location = 0) out vec4 Color;
vec3 EncodeSrgb(vec3 Value)
{
    return mix(12.92 * Value, 1.055 * pow(max(Value, vec3(0)), vec3(1.0 / 2.4)) - 0.055, greaterThan(Value, vec3(0.0031308)));
}
vec3 DecodeSrgb(vec3 Value)
{
    return mix(Value / 12.92, pow((Value + 0.055) / 1.055, vec3(2.4)), greaterThan(Value, vec3(0.04045)));
}
void main()
{
    vec3 Value = texture(Source, Uv).rgb;
    if (SourceSrgb.x != 0.0) Value = EncodeSrgb(Value);
    Value = clamp(Value * GainAndDestinationSrgb.rgb, 0.0, 1.0);
    if (GainAndDestinationSrgb.w != 0.0) Value = DecodeSrgb(Value);
    Color = vec4(Value, 1.0);
}
