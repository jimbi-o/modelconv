import json
import os
import sys
import subprocess

#  python3 scripts/convert_all.py resources/glTF/BoomBoxWithAxes.gltf resources/output build/vs/Release/modelconv.exe ../tools/texconv.exe

filename = sys.argv[1]
output_dir = sys.argv[2]
modelconv_exe = sys.argv[3]
texconv = sys.argv[4]

source_dir = os.path.dirname(filename)

# modelconv_exe uses assimp, assimp seems to need zlib.dll
subprocess.run([modelconv_exe, filename, output_dir])
basename = os.path.splitext(os.path.basename(filename))[0]
json_dir = os.path.abspath(output_dir + "/" + basename)
output_json = json_dir + "/" + basename + ".json"

file = open(output_json)
json_data = json.load(file)
file.close()

# join metallic, roughness, occlusion params
for entity in json_data["material_settings"]["materials"]:
    entity["metallic_roughness_occlusion"] = {
        "metallic_factor" : entity["metallic"]["factor"],
        "roughness_factor" : entity["roughness"]["factor"],
        "occlusion_strength" : entity["occlusion"]["strength"],
        "texture" : {
            "texture" : entity["metallic"]["texture"]["texture"],
            "sampler" : entity["metallic"]["texture"]["sampler"],
        },
    }
    # only converting from gltf format texture is implemented
    assert(entity["metallic"]["texture"]["texture"] == entity["roughness"]["texture"]["texture"])
    assert(entity["metallic"]["texture"]["sampler"] == entity["roughness"]["texture"]["sampler"])
    assert(entity["metallic"]["texture"]["texture"] == entity["occlusion"]["texture"]["texture"])
    assert(entity["metallic"]["texture"]["sampler"] == entity["occlusion"]["texture"]["sampler"])
    assert(entity["metallic"]["texture"]["channel"]  == 1)
    assert(entity["roughness"]["texture"]["channel"] == 2)
    assert(entity["occlusion"]["texture"]["channel"] == 0)
    entity.pop("metallic")
    entity.pop("roughness")
    entity.pop("occlusion")

# gather texture list and output dds names to json
filelist_srgb = []
filelist_linear = []
for entity in json_data["material_settings"]["textures"]:
    filepath = entity["path"]
    if filepath.count('.') == 0:
        del entity["type"]
        continue
    if entity["type"] == "albedo" or entity["type"] == "emissive":
        filelist_srgb.append(filepath)
    else:
        filelist_linear.append(filepath)
    entity["path"] = os.path.splitext(filepath)[0] + ".dds"
    del entity["type"]

# output new texture names to json
with open(output_json, "w") as outfile:
    outfile.write(json.dumps(json_data, indent=2))

# convert textures to dds
initial_directory = os.getcwd()
os.chdir(source_dir)
texconv_path = os.path.relpath(initial_directory, os.path.commonprefix([source_dir, initial_directory])) + "/" + texconv
common_path = os.path.commonprefix([source_dir, json_dir])
relative_path = os.path.relpath(json_dir, common_path)
win_json_dir = relative_path.replace(os.sep, "\\")
texconv_args = "-f BC7_UNORM_SRGB -srgb -o " + win_json_dir + " -y -sepalpha -keepcoverage 0.75 -dx10 -nologo " + ' '.join(str(e) for e in filelist_srgb)
subprocess.run(["cmd.exe", "/c " + texconv_path.replace(os.sep, "\\") + " " + texconv_args])
texconv_args = "-f BC7_UNORM_SRGB -srgb -o " + win_json_dir + " -y -sepalpha -keepcoverage 0.75 -dx10 -nologo " + ' '.join(str(e) for e in filelist_linear)
subprocess.run(["cmd.exe", "/c " + texconv_path.replace(os.sep, "\\") + " " + texconv_args])
os.chdir(initial_directory)
