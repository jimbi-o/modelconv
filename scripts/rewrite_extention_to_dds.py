import json
import sys
import os

# python3 ./scripts/rewrite_extention_to_dds.py ./resources/output/BoomBoxWithAxes/BoomBoxWithAxes.json

def output_to_file(array, filename):
    file = open(filename, "w")
    for content in array:
        file.write(content)
    file.close()

filename = sys.argv[1]
file = open(filename)
json_data = json.load(file)
file.close()

out_dir = json_data["output_directory"]

for entity in json_data["material_settings"]["textures"]:
    filepath = entity["path"]
    if filepath.count('.') == 0:
        continue
    basename = os.path.splitext(filepath)[0]
    entity["path"] = out_dir + "/" + basename + ".dds"

with open(filename, "w") as outfile:
    outfile.write(json.dumps(json_data, indent=2))
