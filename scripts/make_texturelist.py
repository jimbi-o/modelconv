import json
import os
import sys

# python3 ./scripts/make_texturelist.py ./resources/output/BoomBoxWithAxes/BoomBoxWithAxes.json

def output_to_file(array, filename):
    file = open(filename, "w")
    for content in array:
        file.write(content)
    file.close()

filename = sys.argv[1]
file = open(filename)
json_data = json.load(file)
file.close()

filelist_srgb = []
filelist_linear = []
for entity in json_data["material_settings"]["textures"]:
    filepath = entity["path"]
    if filepath.count('.') == 0:
        continue
    if entity["type"] == "albedo" or entity["type"] == "emissive":
        filelist_srgb.append(filepath)
    else:
        filelist_linear.append(filepath)

output_to_file(filelist_srgb, sys.argv[1]+"_texturelist_srgb.txt")
output_to_file(filelist_linear, sys.argv[1]+"_texturelist_linear.txt")
