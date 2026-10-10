"""(Re)creates the UnrealRocketViz materials. Idempotent: existing assets are cleared and rebuilt.

Run headless from a project that mounts the UnrealRocketViz plugin:
  UnrealEditor <Project>.uproject -run=pythonscript -script=<abs path>/make_materials.py -unattended -nosplash
"""
import unreal

FOLDER = "/UnrealRocketViz/Materials"
MEL = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary


def get_or_create(name):
    path = f"{FOLDER}/{name}"
    if EAL.does_asset_exist(path):
        mat = unreal.load_asset(path)
        if isinstance(mat, unreal.Material):
            MEL.delete_all_material_expressions(mat)
            return mat
        EAL.delete_asset(path)
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    return tools.create_asset(name, FOLDER, unreal.Material, unreal.MaterialFactoryNew())


def node(mat, cls, x, y, **props):
    e = MEL.create_material_expression(mat, cls, x, y)
    for k, v in props.items():
        e.set_editor_property(k, v)
    return e


def scalar(mat, name, value, x, y):
    return node(mat, unreal.MaterialExpressionScalarParameter, x, y, parameter_name=name, default_value=value)


def vector(mat, name, rgb, x, y):
    return node(mat, unreal.MaterialExpressionVectorParameter, x, y,
                parameter_name=name, default_value=unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1.0))


def custom(mat, code, out_type, input_names, x, y, desc):
    e = node(mat, unreal.MaterialExpressionCustom, x, y, code=code, output_type=out_type, description=desc)
    inputs = []
    for n in input_names:
        ci = unreal.CustomInput()
        ci.set_editor_property("input_name", n)
        inputs.append(ci)
    e.set_editor_property("inputs", inputs)
    return e


def link(src, dst, pin, out=""):
    if not MEL.connect_material_expressions(src, out, dst, pin):
        raise RuntimeError(f"connect failed: {src.get_name()} -> {dst.get_name()}.{pin}")


def local_position(mat, x, y):
    # Object-space position in cm (basic shapes span -50..50 along Z).
    if hasattr(unreal, "MaterialExpressionLocalPosition"):
        return node(mat, unreal.MaterialExpressionLocalPosition, x, y)
    wp = node(mat, unreal.MaterialExpressionWorldPosition, x - 250, y)
    tp = node(mat, unreal.MaterialExpressionTransformPosition, x, y,
              transform_source_type=unreal.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_WORLD,
              transform_type=unreal.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_LOCAL)
    link(wp, tp, "")
    return tp


# Noise lookup position: stretched along the plume and panned from nozzle (-Z) to tip (+Z).
NOISE_POS = """
return float3(P.x * 0.06, P.y * 0.06, P.z * 0.03 - T * 1.6);
"""

# t = 0 at the nozzle (cone base, z = -50), 1 at the far tip (z = +50).
PLUME = """
float t = saturate((P.z + 50.0) / 100.0);
float vac = saturate(Vac);
float ndv = abs(dot(normalize(Nrm), normalize(V)));
float edge = pow(ndv, lerp(1.3, 2.6, vac));
float along = pow(1.0 - t, lerp(1.8, 0.7, vac));
float core = pow(1.0 - t, lerp(12.0, 4.0, vac));
float3 col = lerp(TailC.rgb, CoreC.rgb, core);
float lum = dot(col, float3(0.3, 0.59, 0.11));
col = lerp(col, lerp(col, float3(0.55, 0.72, 1.0) * lum * 1.4, 0.75), vac);
float turb = lerp(0.6, 1.2, saturate(Nz));
float w = 0.35;
float win = pow(saturate(1.0 - t / w), 1.5);
float bands = pow(0.5 + 0.5 * cos(t / w * 5.0 * 6.2831853), 6.0) * win * saturate(Dia);
float3 e = col * along * turb * edge + CoreC.rgb * bands * 0.8 * edge;
return e * Intensity * lerp(1.0, 0.45, vac);
"""


def make_plume():
    mat = get_or_create("M_UrvPlume")
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
    mat.set_editor_property("two_sided", True)
    mat.set_editor_property("used_with_static_lighting", False)

    pos = local_position(mat, -1100, -200)
    time = node(mat, unreal.MaterialExpressionTime, -1100, -50)
    npos = custom(mat, NOISE_POS, unreal.CustomMaterialOutputType.CMOT_FLOAT3, ["P", "T"], -850, -120, "NoisePos")
    link(pos, npos, "P")
    link(time, npos, "T")
    noise = node(mat, unreal.MaterialExpressionNoise, -600, -120,
                 scale=1.0, quality=1, levels=3, output_min=0.0, output_max=1.0, turbulence=False)
    link(npos, noise, "World Position")  # pin label of the Position input (plain math, not a real world pos)

    nrm = node(mat, unreal.MaterialExpressionVertexNormalWS, -600, 60)
    cam = node(mat, unreal.MaterialExpressionCameraVectorWS, -600, 140)
    intensity = scalar(mat, "Intensity", 25.0, -600, 220)
    dia = scalar(mat, "Diamonds", 1.0, -600, 300)
    vac = scalar(mat, "Vacuum", 0.0, -600, 380)
    core = vector(mat, "CoreColor", (1.0, 0.92, 0.7), -600, 460)
    tail = vector(mat, "TailColor", (1.0, 0.38, 0.06), -600, 640)

    main = custom(mat, PLUME, unreal.CustomMaterialOutputType.CMOT_FLOAT3,
                  ["P", "Nrm", "V", "Nz", "Intensity", "Dia", "Vac", "CoreC", "TailC"], -250, 0, "Plume")
    for src, pin in ((pos, "P"), (nrm, "Nrm"), (cam, "V"), (noise, "Nz"), (intensity, "Intensity"),
                     (dia, "Dia"), (vac, "Vac"), (core, "CoreC"), (tail, "TailC")):
        link(src, main, pin)
    MEL.connect_material_property(main, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    return mat


def make_hull():
    mat = get_or_create("M_UrvHull")
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)
    mat.set_editor_property("two_sided", False)

    base = vector(mat, "BaseColor", (0.85, 0.85, 0.85), -500, 0)
    rough = scalar(mat, "Roughness", 0.35, -500, 200)
    metal = node(mat, unreal.MaterialExpressionConstant, -500, 280, r=0.0)
    floor_k = scalar(mat, "EmissiveFloor", 0.03, -500, 360)
    emis = node(mat, unreal.MaterialExpressionMultiply, -250, 320)
    link(base, emis, "A")
    link(floor_k, emis, "B")

    MEL.connect_material_property(base, "", unreal.MaterialProperty.MP_BASE_COLOR)
    MEL.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    MEL.connect_material_property(metal, "", unreal.MaterialProperty.MP_METALLIC)
    MEL.connect_material_property(emis, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    return mat


# Path ribbons (AUrvPaths): camera-facing strips built every frame.
#   vertex colour  rgb = colour, a = opacity
#   UV0            x = distance along the path [m], y = 0..1 across the strip
#   UV1            x = flow (1: light pulses travel toward the end), y = core weight
PATH_PROFILE = """
float v = abs(UV.y * 2.0 - 1.0);                  // 0 on the centre line, 1 at the edge
float core = exp(-pow(v / max(0.10 * Core, 0.02), 2.0));
float halo = exp(-pow(v / 0.55, 2.0)) * 0.45;
float soft = saturate((1.0 - v) * 6.0);           // no hard strip edge
return (core + halo) * soft;
"""

PATH_PULSE = """
float d = frac(UV.x / Spacing - T * Speed / Spacing);
return 1.0 + Flow * 2.2 * pow(saturate(1.0 - abs(d - 0.85) * 5.0), 3.0);
"""


def make_path():
    mat = get_or_create("M_UrvPath")
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    mat.set_editor_property("two_sided", True)
    mat.set_editor_property("used_with_static_lighting", False)

    vc = node(mat, unreal.MaterialExpressionVertexColor, -900, -100)
    uv0 = node(mat, unreal.MaterialExpressionTextureCoordinate, -900, 60, coordinate_index=0)
    uv1 = node(mat, unreal.MaterialExpressionTextureCoordinate, -900, 160, coordinate_index=1)
    flow = node(mat, unreal.MaterialExpressionComponentMask, -700, 160, r=True, g=False, b=False, a=False)
    corew = node(mat, unreal.MaterialExpressionComponentMask, -700, 240, r=False, g=True, b=False, a=False)
    link(uv1, flow, "")
    link(uv1, corew, "")
    time = node(mat, unreal.MaterialExpressionTime, -900, 300)
    spacing = scalar(mat, "PulseSpacing", 60.0, -900, 380)
    speed = scalar(mat, "PulseSpeed", 45.0, -900, 460)
    intensity = scalar(mat, "Intensity", 3.0, -900, 540)

    profile = custom(mat, PATH_PROFILE, unreal.CustomMaterialOutputType.CMOT_FLOAT1, ["UV", "Core"], -450, 40, "Profile")
    link(uv0, profile, "UV")
    link(corew, profile, "Core")
    pulse = custom(mat, PATH_PULSE, unreal.CustomMaterialOutputType.CMOT_FLOAT1,
                   ["UV", "T", "Spacing", "Speed", "Flow"], -450, 260, "Pulse")
    for src, pin in ((uv0, "UV"), (time, "T"), (spacing, "Spacing"), (speed, "Speed"), (flow, "Flow")):
        link(src, pulse, pin)

    glow = node(mat, unreal.MaterialExpressionMultiply, -200, 100)
    link(profile, glow, "A")
    link(pulse, glow, "B")
    col = node(mat, unreal.MaterialExpressionMultiply, -50, -60)
    link(vc, col, "A")
    link(glow, col, "B")
    emis = node(mat, unreal.MaterialExpressionMultiply, 120, -60)
    link(col, emis, "A")
    link(intensity, emis, "B")
    opa = node(mat, unreal.MaterialExpressionMultiply, 120, 140)
    link(vc, opa, "A", "A")
    link(glow, opa, "B")
    opa_c = node(mat, unreal.MaterialExpressionClamp, 280, 140)
    link(opa, opa_c, "")

    MEL.connect_material_property(emis, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    MEL.connect_material_property(opa_c, "", unreal.MaterialProperty.MP_OPACITY)
    return mat


def finish(mat):
    errors = MEL.recompile_material(mat)
    if errors:
        raise RuntimeError(f"compile errors in {mat.get_name()}: " + " | ".join(errors))
    if not EAL.save_loaded_asset(mat, only_if_is_dirty=False):
        raise RuntimeError(f"save failed: {mat.get_path_name()}")
    unreal.log(f"[make_materials] saved {mat.get_path_name()}")


# Commandlets start with an unscanned asset registry; scan so existing assets are found.
unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous(["/UnrealRocketViz"], True)
EAL.make_directory(FOLDER)
for m in (make_plume(), make_hull(), make_path()):
    finish(m)
unreal.log("[make_materials] done")
