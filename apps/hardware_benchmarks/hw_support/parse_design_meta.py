import argparse
import json
import os
import pprint
import sys
import re

def atoi(text):
    return int(text) if text.isdigit() else text

def natural_keys(text):
    return [ atoi(c) for c in re.split(r'(\d+)', text) ]

# python ../../hw_support/parse_design_meta.py bin/design_meta_halide.json --top bin/design_top.json --place bin/design.place
def parseArguments():
    # Create argument parser
    parser = argparse.ArgumentParser()

    # Positional mandatory arguments
    parser.add_argument("DesignMeta", help="design_meta.json: file so far", type=str)

    # Optional arguments
    parser.add_argument("--top", help="design_top.json: parse out address sequence", type=str, default=None)
    parser.add_argument("--place", help="design.place: parse IO placement", type=str, default=None)
    parser.add_argument("--shuffle", help="update design_meta.json file to use shuffled data", action='store_true')
    # Parse arguments
    args = parser.parse_args()

    return args

def findBetween(s:str, start:str, end:str):
    end = end if "clkwrk" in s else "_op_hcompute"
    assert (start in s) and (end in s)
    starti = s.find(start) + len(start)
    endi = s.find(end, starti)
    return s[starti:endi]

# this does not require an exact match
def findIO(metaBase, name:str):
    for metaIO in metaBase:
        if name in metaIO["name"]:
            return metaIO
    print(f"Could not find {name} in list of IOs")
    assert False

def setOrCheck(json, key, value):
    if key in json:
        pass
        # assert value == json[key]
    else:
        json[key] = value

def parseDesignTop(meta, filename: str):
    meta["testing"]["coreir"] = os.path.basename(filename)

    metaIn = []
    metaMU_in = [] 
    metaOut = []
    with open(filename, "r") as readFile:
        designTop = json.load(readFile)
        topName = designTop["top"]
        prefix, success, designName = topName.partition("global.")
        assert prefix == "" and success
        print("parsing design top:", designName)

        coreirInstances = designTop["namespaces"]["global"]["modules"][designName]["instances"]

        dense_ready_valid = "DENSE_READY_VALID" in os.environ and os.environ.get("DENSE_READY_VALID") == "1" 
        for inst in coreirInstances:
            if inst.startswith("MU"):    
                # this is a MU input
                ioName = findBetween(inst, "MU_io16in_", "_clkwrk")
                metaMU_in = findIO(meta["IOs"]["mu_inputs"], ioName)


                if "mu_io_tiles" in metaMU_in:
                    metaMU_in["mu_io_tiles"].append({"name":inst})
                else:
                    metaMU_in["mu_io_tiles"] = [{"name":inst}]

                # # change read_data_stride based on the number of input tiles
                # metaIn["io_tiles"]
            
            elif inst.startswith("io16in_"):
                # this is a data input
                ioName = findBetween(inst, "io16in_", "_clkwrk")
                addr = coreirInstances[inst]["metadata"]["glb2out_0"]
                metaIn = findIO(meta["IOs"]["inputs"], ioName)

                mode_string = "RV" if dense_ready_valid else "STATIC"

                if "io_tiles" in metaIn:
                    metaIn["io_tiles"].append({"name":inst, "addr":addr, "mode":mode_string})
                else:
                    metaIn["io_tiles"] = [{"name":inst, "addr":addr, "mode":mode_string}]

                # change read_data_stride based on the number of input tiles
                metaIn["io_tiles"]


            elif inst.startswith("io16_"):
                # this is a data output
                ioName = findBetween(inst, "io16_", "_clkwrk")
                addr = coreirInstances[inst]["metadata"]["in2glb_0"]
                metaOut = findIO(meta["IOs"]["outputs"], ioName)

                mode_string = "RV" if dense_ready_valid else "VALID"

                if "io_tiles" in metaOut:
                    #metaOut["io_tiles"].append({"name":inst, "addr":addr, "mode":"VALID"})
                    metaOut["io_tiles"].append({"name":inst, "addr":addr, "mode":mode_string})
                else:
                    #metaOut["io_tiles"] = [{"name":inst, "addr":addr, "mode":"VALID"}]
                    metaOut["io_tiles"] = [{"name":inst, "addr":addr, "mode":mode_string}]

        # alter the shape and data stride
        for input_struct in meta["IOs"]["inputs"]:
            if "io_tiles" in input_struct:
                num_tiles = len(input_struct["io_tiles"])
                # change the shape
                if num_tiles > 1 and input_struct["shape"][0] is not num_tiles:
                    new_shape = input_struct["shape"]
                    partial_unroll = num_tiles
                    # remove dimensions that are fully unrolled
                    while partial_unroll >= new_shape[0]:
                        assert(partial_unroll % new_shape[0] == 0), f"input shape has inner dim {new_shape[0]} and trying to unroll {partial_unroll}"
                        partial_unroll //= new_shape[0]
                        new_shape.pop(0)
                    assert(new_shape[0] % partial_unroll == 0), f"input shape has inner dim {new_shape[0]} and trying to unroll {num_tiles}"
                    new_shape.insert(0,num_tiles)
                    new_shape[1] //= partial_unroll
                    input_struct["shape"] = new_shape

        for output_struct in meta["IOs"]["outputs"]:
            num_tiles = len(output_struct["io_tiles"])
            # change the shape
            if num_tiles > 1 and output_struct["shape"][0] is not num_tiles:
                new_shape = output_struct["shape"]
                partial_unroll = num_tiles
                # remove dimensions that are fully unrolled
                while partial_unroll >= new_shape[0]:
                    assert(partial_unroll % new_shape[0] == 0), f"output shape has inner dim {new_shape[0]} and trying to unroll {partial_unroll}"
                    partial_unroll //= new_shape[0]
                    new_shape.pop(0)
                assert(new_shape[0] % partial_unroll == 0), f"output shape has inner dim {new_shape[0]} and trying to unroll {partial_unroll}"
                new_shape.insert(0,num_tiles)
                new_shape[1] //= partial_unroll
                output_struct["shape"] = new_shape

        # sort the inputs and outputs based on human sorting
        if "mu_io_tiles" in metaMU_in:
            metaMU_in["mu_io_tiles"].sort(key=lambda x: natural_keys(x["name"]))

        if "io_tiles" in metaIn:
            metaIn["io_tiles"].sort(key=lambda x: natural_keys(x["name"]))
        metaOut["io_tiles"].sort(key=lambda x: natural_keys(x["name"]))


def parseDesignPlace(meta, filename: str):
    print("parsing design place", filename)
    meta["testing"]["placement"] = os.path.basename(filename)

    with open(filename, "r") as readFile:
        lines = readFile.readlines()
        for line in lines:
            words = line.split()
            name = words[0]
            if ("\t#I" in line) and name.startswith("io16_"):
                assert len(words) == 4
                ioName = findBetween(name, "io16_", "_clkwrk")
                metaOut = findIO(meta["IOs"]["outputs"], ioName)
                tileOut = findIO(metaOut["io_tiles"], name)
                setOrCheck(tileOut, "x_pos", int(words[1]))
                setOrCheck(tileOut, "y_pos", int(words[2]))

            elif ("\t#I" in line) and name.startswith("io16in_"):
                assert len(words) == 4
                ioName = findBetween(name, "io16in_", "_clkwrk")
                metaOut = findIO(meta["IOs"]["inputs"], ioName)
                tileOut = findIO(metaOut["io_tiles"], name)
                setOrCheck(tileOut, "x_pos", int(words[1]))
                setOrCheck(tileOut, "y_pos", int(words[2]))

            elif ("\t#i" in line) and name.startswith("io1_"):
                ioName = findBetween(name, "io1_", "_clkwrk")
                validName = findBetween(name, "io1_", "_write_valid")
                metaOut = findIO(meta["IOs"]["outputs"], ioName)
                tileOut = findIO(metaOut["io_tiles"], validName)
                setOrCheck(tileOut, "x_pos", int(words[1]))
                setOrCheck(tileOut, "y_pos", int(words[2]))
                tileOut["valid_name"] = validName


            elif ("\t#U" in line) and name.startswith("MU_io16in_"):
                assert len(words) == 4
                ioName = findBetween(name, "MU_io16in_", "_clkwrk")
                metaOut = findIO(meta["IOs"]["mu_inputs"], ioName)
                tileOut = findIO(metaOut["mu_io_tiles"], name)
                setOrCheck(tileOut, "x_pos", int(words[1]))
                setOrCheck(tileOut, "y_pos", int(words[2])) 

# Hacky functions to tile xy loop when flattened and add padding to extents
def parseLoopExtentforPadding(meta, halide_gen_args):
    # Get pad_o values
    args = halide_gen_args.split()
    args_dict = {key: int(value) for key, value in (item.split('=') for item in halide_gen_args.split())}
    pad_o_left = args_dict.get('pad_o_left', 0)
    pad_o_right = args_dict.get('pad_o_right', 0)

    # Get X_dim
    out_shape_list = meta['IOs']['outputs'][0]['shape']
    assert out_shape_list[-1] == out_shape_list[-2], "Only square output supported for output padding"
    X_dim = out_shape_list[-1]

    # Change extent for all io tiles
    assert len(meta['IOs']['outputs']) == 1, "Only one output supported for output padding"
    io_tiles_list = meta['IOs']['outputs'][0]['io_tiles']
    for io_tile in io_tiles_list:
        addr_dict = io_tile['addr']
        found_X_cnt = 0
        # add dimension if X_dim or Y_dim are flattened
        for i, ext in enumerate(addr_dict['extent']):
            if ext == X_dim:
                found_X_cnt += 1
                addr_dict['extent'][i] += (pad_o_left + pad_o_right)
            elif ext == X_dim * X_dim:
                found_X_cnt += 2
                addr_dict['dimensionality'] += 1
                addr_dict['extent'][i] = X_dim + (pad_o_left + pad_o_right)
                addr_dict['extent'].insert(i+1, X_dim + (pad_o_left + pad_o_right))
                addr_dict['cycle_stride'].insert(i+1, addr_dict['cycle_stride'][i] * X_dim)
                addr_dict['write_data_stride'].insert(i+1, addr_dict['write_data_stride'][i] * X_dim)
                break
            elif ext % X_dim == 0:
                found_X_cnt += 1
                addr_dict['dimensionality'] += 1
                addr_dict['extent'][i] = X_dim + (pad_o_left + pad_o_right)
                addr_dict['extent'].insert(i+1, ext // X_dim)
                addr_dict['cycle_stride'].insert(i+1, addr_dict['cycle_stride'][i] * X_dim)
                addr_dict['write_data_stride'].insert(i+1, addr_dict['write_data_stride'][i] * X_dim)
        assert found_X_cnt == 2, "X_dim and Y_dim not found in addr_dict['extent']"

    # Add HALIDE_GEN_ARGS to meta file
    if meta.get("HALIDE_GEN_ARGS") is None: meta["HALIDE_GEN_ARGS"] = args_dict

def addGLBBankConfig(meta):
    with open("bin/glb_bank_config.json", "r") as f:
        glb_json = json.load(f)
    meta["GLB_BANK_CONFIG"] = glb_json
    for input_index, input_dict in enumerate(meta["IOs"]["inputs"]):
        assert "io_tiles" in input_dict, "io_tiles must be key of input_dict"
        for tile_index, io_tile in enumerate(input_dict["io_tiles"]):
            # Check if 'x_pos' of this io_tile is in 'glb_inputs' of the GLB_BANK_CONFIG
            if io_tile["x_pos"] in meta["GLB_BANK_CONFIG"].get("glb_inputs", []):
                meta["IOs"]["inputs"][input_index]["io_tiles"][tile_index]["is_glb_input"] = 1
            else:
                meta["IOs"]["inputs"][input_index]["io_tiles"][tile_index]["is_glb_input"] = 0

# Function to change extent for glb tiling or add dimensions
def parseLoopExtentforTiling(meta, halide_gen_args):
    # Get unroll values
    args = halide_gen_args.split()
    args_dict = {key: int(value) for key, value in (item.split('=') for item in halide_gen_args.split())}
    unroll = args_dict.get('unroll', 0)
    n_ic = args_dict.get('n_ic', 0)
    assert len(meta['IOs']['outputs']) == 1, "Only one output supported for output padding"
    io_tiles_list = meta['IOs']['outputs'][0]['io_tiles']
    for io_tile in io_tiles_list:
        addr_dict = io_tile['addr']
        assert addr_dict['dimensionality'] == 2, "Implement fully unrolled first"
    # Add HALIDE_GEN_ARGS to meta file
    if meta.get("HALIDE_GEN_ARGS") is None: meta["HALIDE_GEN_ARGS"] = args_dict
    if meta.get("NUM_GLB_TILING") is None: meta["NUM_GLB_TILING"] = os.getenv("NUM_GLB_TILING")

def E64_packing(json_data):
    print("INFO: Modifying design meta for E64 packing")
    unique_input_positions = set()
    unique_output_positions = set()
    trimmed_inputs = []
    trimmed_outputs = []
    
    # Assert that the number of inputs and outputs are multiples of 4
    # FIXME: Potentially fix this for apps with two loop levels 
    def process_io(io_entries, unique_positions):
        trimmed_entries = []
        for entry in io_entries:
            assert len(entry["io_tiles"]) % 4 == 0, "Number of io_tiles must be a multiple of 4 for E64 mode"
            new_io_tiles = []
            for tile in entry["io_tiles"]:
                position = (tile["x_pos"], tile["y_pos"])
                if position not in unique_positions:
                    unique_positions.add(position)
                    
                    # Multiply each element in extent by 4
                    if "addr" in tile and "extent" in tile["addr"]:
                        # tile["addr"]["extent"] = [x * 4 for x in tile["addr"]["extent"]]
                        tile["addr"]["extent"][0] = tile["addr"]["extent"][0] * 4
                    else:
                        print("ERROR: addr or extent not found in tile. Confirm that deisgn_top.json and design.place are correct for E64 mode. (Hint: Is unroll a multiple of 4?)")
                        sys.exit(1)
                    
                    new_io_tiles.append(tile)
            
            if new_io_tiles:
                entry["io_tiles"] = new_io_tiles
                
                # Modify shape to reflect packing 
                if "shape" in entry and len(entry["shape"]) >= 2:
                    entry["shape"][0] = int(entry["shape"][0] / 4)
                    entry["shape"][1] = entry["shape"][1] * 4
                    if entry["shape"][0] == 1:
                        entry["shape"] = [entry["shape"][1]] + entry["shape"][2:]
                else:
                    print("ERROR: shape not found or incorrectly formatted. Confirm that deisgn_top.json and design.place are correct for E64 mode. (Hint: Is unroll a multiple of 4?)")
                    sys.exit(1)
                
                trimmed_entries.append(entry)
        return trimmed_entries
    
    json_data["IOs"]["inputs"] = process_io(json_data["IOs"]["inputs"], unique_input_positions)
    json_data["IOs"]["outputs"] = process_io(json_data["IOs"]["outputs"], unique_output_positions)
    
    return json_data

def main():
    args = parseArguments()

    with open(args.DesignMeta, "r") as designMeta:
        meta = json.load(designMeta)

        # Search for *.bs file in the bin directory
        #bin_directory = args.DesignMeta.replace("/design_meta_halide.json", "");
        slash_index = args.DesignMeta.rfind("/")
        bin_directory = args.DesignMeta[0:slash_index]
        for file in os.listdir(bin_directory):
            if file.endswith(".bs"):
                meta["testing"]["bitstream"] = file
                break

        if args.top != None:
            parseDesignTop(meta, args.top)

        if args.place != None:
            parseDesignPlace(meta, args.place)

    if args.shuffle:
        inputs = meta['IOs']['inputs']
        for _input in inputs:
            if _input['datafile'][0:5] == 'input':
                _input['datafile'] = 'input_host_stencil_shuffle.raw'
            elif _input['datafile'][0:6] == 'kernel':
                _input['datafile'] = 'kernel_shuffle.raw'
        outputs = meta['IOs']['outputs']
        for _output in outputs:
            if _output['datafile'][0:9] == 'hw_output':
                _output['datafile'] = 'hw_output_shuffle.raw'


    outputName = 'bin/design_meta.json'
    with open(outputName, 'w', encoding='utf-8') as fileout:
        halide_gen_args = os.getenv("HALIDE_GEN_ARGS")
        if halide_gen_args is not None:
            # If pad_o in args call padding functions to modify extents
            if "pad_o_left" in halide_gen_args or "pad_o_right" in halide_gen_args: parseLoopExtentforPadding(meta, halide_gen_args)
            # If NUM_GLB_TILING is set then edit extent if necessary
            if os.getenv("NUM_GLB_TILING") is not None and atoi(os.getenv("NUM_GLB_TILING")) > 0: parseLoopExtentforTiling(meta, halide_gen_args)

        if os.path.isfile("bin/glb_bank_config.json"): addGLBBankConfig(meta)

        # pprint.pprint(meta, fileout, indent=2, compact=True)
        # MO: E64 MODE
        exchange_64_mode = "E64_MODE_ON" in os.environ and os.environ.get("E64_MODE_ON") == "1"
        if exchange_64_mode:
            meta = E64_packing(meta)
        print("writing to", outputName)
        json.dump(meta, fileout, indent=2)
  

        

if __name__ == "__main__":
    main()
