#include "Halide.h"
#include <iostream>
#include <iomanip>
#include <random>
#include <cstdlib>
#include <unordered_map>

using namespace Halide;
using namespace Halide::Internal;
using namespace Halide::ConciseCasts;
using std::vector;
using std::unordered_map;

// Convert a vector of Vars to Exprs. Useful for generating references
// to Funcs.
vector<Expr> make_arguments(vector<Var> vars) {
    vector<Expr> result;
    for (Var i : vars) {
        result.push_back(i);
    }
    return result;
}

std::mt19937 rng;

// Helpers to generate random values.
int16_t rand_int(int min, int max) { return (rng() % (max - min + 1)) + min; }
bool rand_bool() { return rng() % 2 == 0; }
float rand_float() { return rand_int(0, 1 << 30) / (float)(1 << 30); }

// Generate random expressions
// Given a vector of expresions and a tree depth, recursively
// generates an expression by combining subexpressions.
// At the base case where depth is 0, we just return a randomly
// chosen input.

//Type expr_types[] = { UInt(8), UInt(16), UInt(32), Int(8), Int(16), Int(32), Float(32) };
Type expr_types[] = { UInt(16), Int(16) };
const int expr_type_count = sizeof(expr_types)/sizeof(expr_types[0]);

typedef Expr (*make_bin_op_fn)(Expr, Expr);

vector<make_bin_op_fn> make_bin_op = {
    (make_bin_op_fn)operator+,
    (make_bin_op_fn)operator-,
    (make_bin_op_fn)operator*,
    (make_bin_op_fn)min,
    (make_bin_op_fn)max,
};

make_bin_op_fn make_div_op = (make_bin_op_fn)operator/;
make_bin_op_fn make_mod_op = (make_bin_op_fn)operator%;

make_bin_op_fn make_bool_bin_op[] = {
    (make_bin_op_fn)operator&&,
    (make_bin_op_fn)operator||,
};

make_bin_op_fn make_comp_bin_op[] = {
    (make_bin_op_fn)operator==,
    (make_bin_op_fn)operator!=,
    (make_bin_op_fn)operator<,
    (make_bin_op_fn)operator<=,
    (make_bin_op_fn)operator>,
    (make_bin_op_fn)operator>=
};

int bin_op_count = make_bin_op.size();
const int bool_bin_op_count = sizeof(make_bool_bin_op) / sizeof(make_bool_bin_op[0]);
const int comp_bin_op_count = sizeof(make_comp_bin_op) / sizeof(make_comp_bin_op[0]);

Type random_type() {
    Type T = expr_types[rng()%expr_type_count];
    return T;
}

Expr random_expr(vector<Expr> inputs, int depth, int func_size);

// takes a vector of inputs (points in functions) and an expected Type
// if the chosen input is not of the given type, cast it to conform
Expr make_leaf(vector<Expr> inputs) {
  //if (rand_bool()) {
  if (true) {
    auto chosen_input = inputs[rand_int(0, inputs.size()-1)];
    return chosen_input;
  } else {
    uint bitwidth = inputs.at(0).type().bits();
    if (bitwidth == 1) {
      Expr constant_bool = UIntImm::make(UInt(1), rand_int(0,1));
    } else {
      Expr constant_number = IntImm::make(Int(bitwidth), rand_int(1, 100));
      return constant_number;
    }
  }
}

std::string to_string(Halide::Internal::IRNodeType t) {
  switch (t) {
  case IRNodeType::IntImm: return "intimm";
  case IRNodeType::UIntImm: return "uintimm";
  case IRNodeType::FloatImm: return "floatimm";
  case IRNodeType::StringImm: return "stringimm";
  case IRNodeType::Broadcast: return "broadcast";
  case IRNodeType::Cast: return "cast";
  case IRNodeType::Variable: return "variable";
  case IRNodeType::Add: return "add";
  case IRNodeType::Sub: return "sub";
  case IRNodeType::Mod: return "mod";
  case IRNodeType::Mul: return "mul";
  case IRNodeType::Div: return "div";
  case IRNodeType::Min: return "min";
  case IRNodeType::Max: return "max";
  case IRNodeType::EQ: return "eq";
  case IRNodeType::NE: return "ne";
  case IRNodeType::LT: return "lt";
  case IRNodeType::LE: return "le";
  case IRNodeType::GT: return "gt";
  case IRNodeType::GE: return "ge";
  case IRNodeType::And: return "and";
  case IRNodeType::Or: return "or";
  case IRNodeType::Not: return "not";
  case IRNodeType::Select: return "select";
  case IRNodeType::Load: return "load";
  case IRNodeType::Ramp: return "ramp";
  default: return "unknown";
  }
}

// Generator to produce a random pipeline. The generated pipeline will
// be solely a function of the seed and the number of stages.
class RandomPipeline : public Halide::Generator<RandomPipeline> {
public:
    int num_stage_types = 18;
    // The random seed to use to generate the pipeline.
    GeneratorParam<int> seed{"seed", 10}; // seed=7 is quite complex
    // The approximate max number of stages to generate in the random pipeline.
    GeneratorParam<int> max_stages{"max_stages", 30};

    // This set of params enable/disable operators being used
    GeneratorParam<bool> gen_conv1d{        "gen_conv1d", true};
    GeneratorParam<bool> gen_conv2d{        "gen_conv2d", false};
    GeneratorParam<bool> gen_pool{            "gen_pool", false};
    GeneratorParam<bool> gen_activate{    "gen_activate", false};
    GeneratorParam<bool> gen_padding{      "gen_padding", false};
    GeneratorParam<bool> gen_upsample{    "gen_upsample", false};
    GeneratorParam<bool> gen_downsample{"gen_downsample", false};
    GeneratorParam<bool> gen_all2all{      "gen_all2all", false};
    GeneratorParam<bool> gen_scan{            "gen_scan", false};
    GeneratorParam<bool> gen_histogram{  "gen_histogram", false};
    GeneratorParam<bool> gen_sin{              "gen_sin", false};
    GeneratorParam<bool> gen_tanh{            "gen_tanh", false};
    GeneratorParam<bool> gen_exp{              "gen_exp", false};
    GeneratorParam<bool> gen_sqrt{            "gen_sqrt", false};
    GeneratorParam<bool> gen_log{              "gen_log", false};
    GeneratorParam<bool> gen_div{              "gen_div", true};
    GeneratorParam<bool> gen_mod{              "gen_mod", false};
    GeneratorParam<bool> gen_bool{            "gen_bool", true};
    GeneratorParam<bool> gen_cond{            "gen_cond", true};

  //Input<Buffer<float>>  input{"input", 3};
    Input<Buffer<uint8_t>>  input{"input", 3};
  
  // Input<Buffer<uint8_t>>  uint8_weights {"uint8_weights", 4};
  // Input<Buffer<uint16_t>>  uint16_weights{"uint16_weights", 4};
  // Input<Buffer<uint32_t>>  uint32_weights{"uint32_weights", 4};
  // Input<Buffer<int8_t>>  int8_weights {"int8_weights", 4};
  // Input<Buffer<int16_t>>  int16_weights{"int16_weights", 4};
  // Input<Buffer<int32_t>>  int32_weights{"int32_weights", 4};
  // Input<Buffer<float>>  float32_weights{"float32_weights", 4};

  //Output<Buffer<float>> output{"output", 3};
    Output<Buffer<uint8_t>> output{"output", 3};

Expr random_condition(vector<Expr> inputs, int depth, int func_size) {
    Expr a = random_expr(inputs, depth, func_size);
    Expr b = random_expr(inputs, depth, func_size);
    int op = rng() % comp_bin_op_count;
    return make_comp_bin_op[op](a, b);
}

Expr random_expr(vector<Expr> inputs, int depth, int func_size) {
    const int op_count = bin_op_count + bool_bin_op_count + 9;
    const int func_size_thresh = 1e4; // if input is too large do not use trig functions

    if (depth <= 0) {
        return make_leaf(inputs);
    }

    // pick a random operation to combine exprs
    int op = rng() % op_count; // ops need to be defined
    switch(op) {
      /*
    case 0:  // casting
    {
        // Get a random type
        Type convertT = random_type();
        auto e1 = random_expr(inputs, depth, func_size);
        return cast(convertT, e1);
    }
      */
    case 1: // select operation
    {
        if (!gen_cond) break;
        auto c = random_condition(inputs, depth-2, func_size); // arbitrarily chose to make condition expression shorter
        auto e1 = random_expr(inputs, depth-1, func_size);
        auto e2 = random_expr(inputs, depth-2, func_size);
        // make sure e1 and e2 have the same type
        if (e1.type() != e2.type()) {
            e2 = cast(e1.type(), e2);
        }
        return select(c, e1, e2);
    }
    case 2: // unary boolean op
    {
        if (!gen_bool) break;
        auto e1 = random_expr(inputs, depth-1, func_size);
        if (e1.type().is_bool()) {
            return !e1;
        }
        break;
    }
    case 3: // sin
    {
        if (!gen_sin) break;
        if (func_size > func_size_thresh)
            break;
        auto e1 = random_expr(inputs, depth-1, func_size);
        return sin(cast<float>(e1));
    }
    case 4: // tanh
    {
        if (!gen_tanh) break;
        if (func_size > func_size_thresh)
            break;
        auto e1 = random_expr(inputs, depth-1, func_size);
        return tanh(cast<float>(e1));
    }
    case 5: // exp
    {
        if (!gen_exp) break;
        auto e1 = random_expr(inputs, depth-1, func_size);
        return fast_exp(cast<float>(e1));
    }
    case 6: // sqrt
    {
        if (!gen_sqrt) break;
        auto e1 = random_expr(inputs, depth-1, func_size);
        return sqrt(cast<float>(e1));
    }
    case 7: // log
    {
        if (!gen_log) break;
        auto e1 = random_expr(inputs, depth-1, func_size);
        return fast_log(cast<float>(e1));
    }
    case 8: // condition
    {
        if (!gen_cond) break;
        return random_condition(inputs, depth-1, func_size);
    }
    default: // binary op
        make_bin_op_fn maker;
        auto e1 = random_expr(inputs, depth-1, func_size);
        auto e2 = random_expr(inputs, depth-2, func_size);
        if (e1.type().is_bool() && e2.type().is_bool()) {
            maker = make_bool_bin_op[op % bool_bin_op_count];
        } else {
            maker = make_bin_op[op % bin_op_count];
        }

        return maker(e1, e2);
    }

    // selected case did not return an expression, try again
    return random_expr(inputs, depth, func_size);
}

Expr rand_value(Type t) {
    if (t.is_bool()) {
        return cast(t, rand_int(0,1));
    } else if (t.is_int() || t.is_uint()) {
        return cast(t, rand_int(1, 127));
    } else if (t.is_float()) {
        return cast(t, rand_float());
    } else {
        // Shouldn't get here.
        assert(false);
        return undef(t);
    }
}

  
    void set_upcast_types(Type input_type, Type& mult_type, Type& sum_type) {
        if (input_type.is_int() && rand_int(0,1)) {
            int input_bits = input_type.bits();
            int mult_bits = std::min(16, 2*input_bits);
            int sum_bits = std::min(16, 2*mult_bits);
            mult_type = Int(mult_bits);
            sum_type = Int(sum_bits);
        } else if (input_type == UInt(1)) {
            mult_type = UInt(8);
            sum_type = UInt(8);
        } else {
            mult_type = input_type;
            sum_type = input_type;
        }
        return;
    }

    void set_downcast_type(Type input_type, Type& output_type) {
        if (input_type.is_int() && rand_int(0,1)) {
            int input_bits = input_type.bits();
            int factor = rand_int(1, 2) * 2;
            int output_bits = std::max(8, input_bits/factor);
            output_type = Int(output_bits);
        } else {
            output_type = input_type;
        }
        return;
    }

  Func get_conv_weights(Type t, vector<Var> vars) {
        //if (t == UInt(8)) return uint8_weights;
        //else if (t == UInt(16)) return uint16_weights;
        //else if (t == UInt(32)) return uint32_weights;
        //else if (t == Int(8)) return int8_weights;
        //else if (t == Int(16)) return int16_weights;
        //else if (t == Int(32)) return int32_weights;
        //else if (t == UInt(1)) return uint8_weights;
        //else {
        //  std::cerr << "type is " << t << std::endl;
        //  assert(t == Float(32));
        //
        //    
        //    return float32_weights;
        //}
    Func weights;
    Var p;
    vars.emplace_back(p);
      weights(vars) = 2;
      return weights;
      
    }

    struct Stage {
        Func func;
        int w, h, c; // approx width and height and channels; TODO: ADD 4TH DIMENSION FOR BATCH SIZE

        static constexpr int max_size = 10000000;
        static constexpr int min_size = 100;
        static constexpr int max_stride = 3; // for convs and pools

        int size() const {
            return w*h*c;
        }

        bool may_increase_size() const {
            return size() < max_size && w <= 8000 && h <= 8000 && c <= 512;
        }

        bool may_reduce_size() const {
            return size() > min_size;
        }

        int random_size_increase_factor() const {
            int sz = size();
            int max_factor = (max_size + sz - 1) / sz;
            if (max_factor <= 1) return 1;
            int log_max_factor = std::ceil(std::log(max_factor) / std::log(2));
            int factor = 1 << rand_int(std::max(1, log_max_factor - 3), log_max_factor);
            return factor;
        }

        int random_size_reduce_factor() const {
            int sz = size();
            int max_factor = (sz + min_size - 1) / min_size;
            if (max_factor <= 1) return 1;
            return std::min(8, 1 << rand_int(1, std::ceil(std::log(max_factor) / std::log(2))));
        }

        int random_out_channels() const {
            int min = (min_size + w * h - 1) / (w * h);
            int max = std::min(512, max_size / (w * h));
            if (min >= max) return min;
            return rand_int(min, max);
        }
    };

    // Generate a random convolution of one dimension of f, statically unrolled.
    Stage convolve(Stage f, int dim, int kernel_min, int kernel_max) {
        std::cout << "Convolving dimension " << dim
                  << " with kernel [" << kernel_min << ", " << kernel_max << "]\n";

        vector<Var> args = f.func.args();

        // generate random expression using potentially all values in the stencil
        vector<Expr> inputs;
        for (int i = kernel_min; i <= kernel_max; i++) {
            vector<Expr> coords = make_arguments(f.func.args());
            coords[dim] += i;
            inputs.push_back(f.func(coords));
        }
        int min_depth = std::floor(std::log(kernel_max - kernel_min + 1));
        int max_depth = min_depth + 1;
        Expr def = random_expr(inputs, rand_int(min_depth, max_depth), f.size());
        std::cerr << def << "\n";

        Func conv("conv_" + args[dim].name());
        conv(args) = def;

        return {conv, f.w, f.h, f.c};
    }

    // Generate a random convolution of one dimension of f using a reduction.
    Stage convolve_r(Stage f, int dim, int kernel_min, int kernel_max) {
        std::cout << "Convolving dimension " << dim
                  << " with kernel [" << kernel_min << ", " << kernel_max << "]"
                  << " using +=\n";

        vector<Var> args = f.func.args();

        Func conv("conv_r_" + args[dim].name());
        RDom r(kernel_min, kernel_max - kernel_min + 1);
        vector<Expr> coords = make_arguments(f.func.args());
        coords[dim] += r;
        conv(args) += rand_value(f.func.value().type()) * f.func(coords);

        return {conv, f.w, f.h, f.c};
    }

    // Generate a random convolution of one dimension of f using a reduction with a wrapper
    Stage convolve_w(Stage f, int dim, int kernel_min, int kernel_max) {
        std::cout << "Convolving dimension " << dim
                  << " with kernel [" << kernel_min << ", " << kernel_max << "]"
                  << " using sum() helper\n";

        vector<Var> args = f.func.args();

        Func conv("conv_w_" + args[dim].name());
        RDom r(kernel_min, kernel_max - kernel_min + 1);
        vector<Expr> coords = make_arguments(f.func.args());
        coords[dim] += r;
        conv(args) = sum(rand_value(f.func.value().type()) * f.func(coords));

        return {conv, f.w, f.h, f.c};
    }

    /*****
     * convolutional net type layers
     *****/
    Stage padding(Stage f) {
        std::cout << "Padding\n";
        std::vector<std::pair<Expr, Expr>> bounds(3); // assuming all stages have 3 dims
        bounds.at(0).first = 0;
        bounds.at(0).second = f.w;
        bounds.at(1).first = 0;
        bounds.at(1).second = f.h;
        bounds.at(2).first = 0;
        bounds.at(2).second = f.c;
        Expr zero = cast(f.func.value().type(), 0);
        return {BoundaryConditions::constant_exterior(f.func, zero, bounds), f.w, f.h, f.c};
    }

    Stage convolve2D(Stage f, int kernel_min, int kernel_max) {
      int conv_type = 0;//rand_int(0,2);
        if (conv_type == 0) return convolve2D_unrolled(f, kernel_min, kernel_max);
        if (conv_type == 1) return convolve2D_w(f, kernel_min, kernel_max);
        else return convolve2D_r(f, kernel_min, kernel_max);
    }

    Stage pool2D(Stage f, int kernel_min, int kernel_max) {
        int pool_type = rand_int(0,2);
        if (pool_type == 0) return pool2D_unrolled(f, kernel_min, kernel_max);
        if (pool_type == 1) return pool2D_w(f, kernel_min, kernel_max);
        else return pool2D_r(f, kernel_min, kernel_max);
    }

    Stage activation(Stage f) {
        return relu_layer(f);
        /*
        int activation_type = rand_int(0,1);
        if (activation_type == 0) return relu_layer(f);
        else return tanh_layer(f);
        */
    }

    Stage relu_layer(Stage f) {
        std::cout << "Relu\n";
        Func activation("relu");
        // if input type is int, downcast with 50% chance
        Type input_type = f.func.value().type();
        Type output_type;
        set_downcast_type(input_type, output_type);

        vector<Expr> coords = make_arguments(f.func.args());
        activation(f.func.args()) = max(cast(output_type, 0), cast(output_type,f.func(coords)));
        return {activation, f.w, f.h, f.c};
    }

    Stage tanh_layer(Stage f) {
        std::cout << "Tanh\n";
        Func activation("tanh");
        // if input type is int, downcast with 50% chance
        Type input_type = f.func.value().type();
        Type output_type;
        set_downcast_type(input_type, output_type);

        vector<Expr> coords = make_arguments(f.func.args());
        Expr exp_pos = fast_exp(2 * cast<float>(f.func(coords)));
        activation(f.func.args()) = (exp_pos - 1) / (exp_pos + 1);
        return {activation, f.w, f.h, f.c};
    }

    /*** pooling stages ***/
    Stage pool2D_unrolled(Stage f, int kernel_min, int kernel_max) {
        vector<Var> args = f.func.args();
        Func pooled2D("pooled2D" + args[0].name() + args[1].name());
        int stride = f.random_size_reduce_factor();

        int extent = kernel_max - kernel_min + 1;
        int scale = extent * extent;

        if (stride > extent) {
            stride = 1;
        }

        std::cout << "Pooling unrolled with stride: " << stride
                  << " and kernel [ " << kernel_min
                  << ", " << kernel_max << "]\n";

        Expr def = cast(f.func.value().type(), 0);

        // Avoid huge unrolled loops
        if (extent >= 4) return pool2D_r(f, kernel_min, kernel_max);

        // assuming input is 3d: w, h, c
        for (int i = kernel_min; i <= kernel_max; i++) {
            for (int j = kernel_min; j <= kernel_max; j++) {
                vector<Expr> pooled_coords = make_arguments(f.func.args());
                pooled_coords[0] = pooled_coords[0] * stride + i;
                pooled_coords[1] = pooled_coords[1] * stride + j;
                if (def.type().is_bool()) {
                    def = def && f.func(pooled_coords);
                } else {
                    def = def + f.func(pooled_coords);
                }
            }
        }

        if (!def.type().is_bool()) {
            def /= scale;
        }

        pooled2D(args) = def;

        return {pooled2D, (f.w + stride - 1) / stride, (f.h + stride - 1) / stride, f.c};
    }

    Stage pool2D_r(Stage f, int kernel_min, int kernel_max) {
        vector<Var> args = f.func.args();
        Func pooled2D_r("pool2D_r_" + args[0].name() + args[1].name());
        int stride = f.random_size_reduce_factor();
        int extent = kernel_max - kernel_min + 1;
        int scale = extent * extent;

        if (stride > extent) {
            stride = 1;
        }

        std::cout << "Pooling using += with stride: " << stride << " and kernel [ " << kernel_min
          << ", " << kernel_max << "]\n";

        RDom r(kernel_min, extent,
               kernel_min, extent);

        vector<Expr> coords = make_arguments(f.func.args());
        Type ty = f.func.value().type();
        coords[0] = coords[0] * stride + r.x;
        coords[1] = coords[1] * stride + r.y;
        if (ty.is_bool()) {
            pooled2D_r(args) = const_true();
            pooled2D_r(args) = pooled2D_r(args) && f.func(coords);
        } else {
            pooled2D_r(args) += f.func(coords) / scale;
        }

        return {pooled2D_r, (f.w + stride - 1) / stride, (f.h + stride - 1) / stride, f.c};
    }

    Stage pool2D_w(Stage f, int kernel_min, int kernel_max) {
        vector<Var> args = f.func.args();
        Func pooled2D_w("pooled2D_w_" + args[0].name() + args[1].name());
        int stride = f.random_size_reduce_factor();
        int extent = kernel_max - kernel_min + 1;
        int scale = extent * extent;

        if (stride > extent) {
            stride = 1;
        }

        std::cout << "Pooling using sum() with stride: " << stride << " and kernel [ " << kernel_min
          << ", " << kernel_max << "]\n";

        RDom r(kernel_min, extent,
               kernel_min, extent);

        vector<Expr> coords = make_arguments(f.func.args());
        coords[0] = (coords[0] * stride + r.x);
        coords[1] = (coords[1] * stride + r.y);
        pooled2D_w(args) = sum(cast<float>(f.func(coords))) / scale;

        return {pooled2D_w, (f.w + stride - 1) / stride, (f.h + stride - 1) / stride, f.c};
    }

    /******* set of 2 dimensional (non separable) convs *********/
    // Generate a random convolution of one dimension of f, statically unrolled.
    Stage convolve2D_unrolled(Stage f, int kernel_min, int kernel_max) {
        std::cout << "Convolving 2D dimension 1: " << 0
                  << " dimension 2: " << 1
                  << " with kernel [" << kernel_min << ", " << kernel_max << "]\n";

        vector<Var> args = f.func.args();
        // Avoid huge unrolled loops
        if (f.c >= 4) return convolve2D_r(f, kernel_min, kernel_max);

        vector<Expr> inputs;
        for (int c = 0; c < f.c; c++)  {
            for (int i = kernel_min; i <= kernel_max; i++) {
                for (int j = kernel_min; j <= kernel_max; j++) {
                    vector<Expr> coords = make_arguments(f.func.args());
                    coords[0] += i;
                    coords[1] += j;
                    //coords[2] = c;
                    inputs.push_back(f.func(coords));
                }
            }
        }

        int out_channels = f.random_out_channels();
        int kernel_width = kernel_max - kernel_min + 1;
        int min_depth = std::floor(std::log(kernel_width * kernel_width * f.c));
        int max_depth = min_depth + 1;
        int func_size = f.w * f.h * out_channels;

        Expr def = random_expr(inputs, rand_int(min_depth, max_depth), func_size);
        std::cerr << def << "\n";

        Func conv("conv2D_" + args[0].name() + args[1].name());
        conv(args) = def;

        return {conv, f.w, f.h, out_channels};
    }

    // Generate a random convolution of one dimension of f using a reduction.
    Stage convolve2D_r(Stage f, int kernel_min, int kernel_max) {
        std::cout << "Convolving 2D dimension 1: " << 0
                  << " dimension 2: " << 1
                  << " with kernel [" << kernel_min << ", " << kernel_max << "]"
                  << " using +=\n";

        vector<Var> args = f.func.args();
        Func conv("conv2D_r_" + args[0].name() + args[1].name());
        // if input type is int, upcast with 50% chance
        Type mult_type, sum_type;
        Type input_type = f.func.value().type();
        Func weights = get_conv_weights(input_type, args);
        set_upcast_types(input_type, mult_type, sum_type);

        int stride = f.random_size_reduce_factor();
        int extent = kernel_max - kernel_min + 1;
        if (stride > extent) {
            stride = 1;
        }

        RDom r(kernel_min, extent,
               kernel_min, extent,
               0, f.c);

        vector<Expr> coords = make_arguments(f.func.args());
        coords[0] = coords[0] * stride + r.x; // only stride in w and h
        coords[1] = coords[1] * stride + r.y;
        coords[2] += r.z;
        conv(args) += cast(sum_type, cast(mult_type, weights(r.z, r.x, r.y, args[2]) * f.func(coords)));

        Stage out {conv, f.w, f.h, f.random_out_channels()};
        out.w = (out.w + stride - 1)/stride;
        out.h = (out.h + stride - 1)/stride;
        return out;
    }

    // Generate a random convolution of one dimension of f using a reduction with a wrapper
    Stage convolve2D_w(Stage f, int kernel_min, int kernel_max) {
        std::cout << "Convolving 2D dimension 1: " << 0
                  << " dimension 2: " << 1
                  << " with kernel [" << kernel_min << ", " << kernel_max << "]"
                  << " using sum() helper\n";

        vector<Var> args = f.func.args();
        Func conv("conv2D_w_" + args[0].name() + args[1].name());
        // if input type is int, upcast with 50% chance
        Type mult_type, sum_type;
        Type input_type = f.func.value().type();
        Func weights = get_conv_weights(input_type, args);
        set_upcast_types(input_type, mult_type, sum_type);

        int stride = f.random_size_reduce_factor();
        int extent = kernel_max - kernel_min + 1;

        if (stride > extent) {
            stride = 1;
        }

        RDom r(kernel_min, extent,
               kernel_min, extent,
               0, f.c);
        vector<Expr> coords = make_arguments(f.func.args());
        coords[0] = coords[0] * stride + r.x;
        coords[1] = coords[1] * stride + r.y;
        coords[2] = r.z;
        // sum() captures free vars in the order found, and the new
        // autoscheduler isn't clever enough to do storage reordering
        // yet, so make sure to put the term that depends on the
        // output channel last.
        conv(args) = sum(cast(sum_type, cast(mult_type, weights(r.z, r.x, r.y, args[2]) * f.func(coords))));

        // choose a channel output size - 0.5 prob of doubling channel dim
        Stage out {conv, f.w, f.h, f.random_out_channels()};
        out.w = (out.w + stride -1)/stride;
        out.h = (out.h + stride -1)/stride;
        return out;
    }

    // Generate an upsampling or downsampling of dimension dim by factor.
    Stage upsample(Stage f, int dim, int factor = 0) {
        std::cout << "Upsampling dimension " << dim << " by " << factor << "x\n";

        if (factor == 0) factor = f.random_size_increase_factor();

        Func resampled;

        if (rand_bool()) {
            // Nearest neighbour
            resampled = Func("upsampled_nn_" + f.func.args()[dim].name());
            vector<Expr> resampled_coords = make_arguments(f.func.args());
            resampled_coords[dim] = resampled_coords[dim] / factor;
            resampled(f.func.args()) = f.func(resampled_coords);
        } else {
            // Linear interpolation
            resampled = Func("upsampled_linear_" + f.func.args()[dim].name());
            vector<Expr> resampled_coords = make_arguments(f.func.args());
            Expr x = cast<float>(resampled_coords[dim]) / factor;
            resampled_coords[dim] = cast<int>(floor(x));
            Expr s1 = f.func(resampled_coords);
            resampled_coords[dim] += 1;
            Expr s2 = f.func(resampled_coords);
            Expr fx = x - floor(x);
            resampled(f.func.args()) = lerp(s1, s2, fx);
        }

        Stage s {resampled, f.w, f.h, f.c};
        if (dim == 0) {
            s.w *= factor;
        } else if (dim == 1) {
            s.h *= factor;
        } else {
            assert(false);
        }
        return s;
    }

    Stage downsample(Stage f, int dim, int factor = 0) {
        std::cout << "Downsampling dimension " << dim << " by " << factor << "x\n";

        if (factor == 0) factor = f.random_size_reduce_factor();

        Func resampled;
        if (rand_bool()) {
            // Nearest neighbour
            resampled = Func("downsampled_nn_" + f.func.args()[dim].name());
            vector<Expr> resampled_coords = make_arguments(f.func.args());
            resampled_coords[dim] = resampled_coords[dim] * factor;
            resampled(f.func.args()) = f.func(resampled_coords);
        } else {
            // Averaging down
            resampled = Func("downsampled_box_" + f.func.args()[dim].name());
            vector<Expr> resampled_coords = make_arguments(f.func.args());
            resampled_coords[dim] = resampled_coords[dim] * factor;
            Expr e = cast(f.func.value().type(), 0);
            for (int i = 0; i < factor; i++) {
                resampled_coords[dim] += 1;
                e += f.func(resampled_coords);
            }
            resampled(f.func.args()) = e;
        }

        Stage s {resampled, f.w, f.h, f.c};
        if (dim == 0) {
            s.w = (s.w + factor - 1)/factor;
        } else if (dim == 1) {
            s.h = (s.h + factor - 1)/factor;
        } else {
            assert(false);
        }
        return s;
    }
  
    Stage binary_op(Stage f, Stage g) {
        if ((f.w != g.w || f.h != g.h || f.c != g.c) && gen_upsample) {
            if (f.size() < g.size()) {
                f = resample_to(f, g.w, g.h, g.c);
            } else {
                g = resample_to(g, f.w, f.h, f.c);
            }
        }

        vector<Expr> inputs = {f.func(f.func.args()), g.func(f.func.args())};
        int min_depth = 1;
        int max_depth = 3;
        int func_size = f.w * f.h * std::min(f.c, g.c);
        Expr def = random_expr(inputs, rand_int(min_depth, max_depth), func_size);
        std::cout << "Binary op: ";
        std::string node_str = to_string(def.node_type());
        std::cout << node_str << "\n";
        std::cerr << "\t" << def << "\n";

        Func binary("binary_op_" + node_str);
        binary(f.func.args()) = def;
        return {binary, f.w, f.h, std::min(f.c, g.c)};
    }

    Stage unary_op(Stage f) {
        std::cout << "Unary op\n";
        Func unary("unary_op");
        vector<Expr> coords = make_arguments(f.func.args());
        int op_type = rand_int(0,2); // exp, log, sqrt

        if (op_type == 0) {
            unary(f.func.args()) = fast_exp(cast<float>(f.func(coords)));
            std::cout << "Unary op: exp\n";
        } else if (op_type == 1) {
            unary(f.func.args()) = fast_log(cast<float>(f.func(coords)));
            std::cout << "Unary op: log\n";
        } else if (op_type == 2) {
            unary(f.func.args()) = sqrt(cast<float>(f.func(coords)));
            std::cout << "Unary op: sqrt\n";
        }
        return {unary, f.w, f.h, f.c};
    }

    Stage leaf_op(Stage f) {
        std::cout << "Leaf op\n";
        Func leaf("leaf_op");
        vector<Expr> coords = make_arguments(f.func.args());
        vector<Expr> inputs = {f.func(f.func.args())};
        Expr def = make_leaf(inputs);
        std::cerr << "\t" << def << "\n";
        leaf(f.func.args()) = def;

        return {leaf, f.w, f.h, f.c};
    }
  
    // Generate an all-to-all communication in dimension dim, statically unrolled.
    Stage all_to_all(Stage f, int dim) {
        std::cout << "All to all on dimension " << dim << '\n';

        vector<Expr> reduction_coords = make_arguments(f.func.args());
        Expr e = 0.f;
        for (int i = 0; i < f.c; i++) {
            reduction_coords[dim] = i;
            e += f.func(reduction_coords) * (i + 1) * (f.func.args()[dim] + 1);
        }

        Func all("all");
        all(f.func.args()) = e;

        return {all, f.w, f.h, f.random_out_channels()};
    }

    // Generate an all-to-all communication in dimension dim using an RDom
    Stage all_to_all_r(Stage f, int dim) {
        std::cout << "All to all on dimension " << dim << " using += \n";

        vector<Expr> reduction_coords = make_arguments(f.func.args());
        RDom r(0, f.c);
        reduction_coords[dim] = r;
        Func all("all_r");
        all(f.func.args()) += f.func(reduction_coords) * (r + 1) * (f.func.args()[dim] + 1);

        return {all, f.w, f.h, f.random_out_channels()};
    }

    // Generate an all-to-all communication in dimension dim using an RDom with wrapper func
    Stage all_to_all_w(Stage f, int dim) {
        std::cout << "All to all on dimension " << dim << " using += \n";

        vector<Expr> reduction_coords = make_arguments(f.func.args());
        RDom r(0, f.c);
        reduction_coords[dim] = r;
        Func all("all_w");
        all(f.func.args()) = sum(f.func(reduction_coords) * (r + 1) * (f.func.args()[dim] + 1));

        return {all, f.w, f.h, f.random_out_channels()};
    }

    // Generate a forwards-then-backwards scan along a dimension
    Stage scan(Stage f, int dim) {
        std::cout << "Scan on dimension " << dim << '\n';
        int extent = dim == 0 ? f.w : dim == 1 ? f.h : 3;
        RDom r(1, extent - 1);
        Func scan("scan_" + f.func.args()[dim].name());
        vector<Expr> coords = make_arguments(f.func.args());
        scan(coords) = f.func(coords);
        coords[dim] = r;
        vector<Expr> prev_coords = coords;
        prev_coords[dim] = r-1;
        scan(coords) += scan(prev_coords);
        // Now in reverse
        coords[dim] = extent - r - 1;
        prev_coords[dim] = extent - r;
        scan(coords) += scan(prev_coords);
        return {scan, f.w, f.h, f.c};
    }

    // Transpose
    Stage transpose(Stage f) {
        std::cout << "Transpose\n";
        Func transpose("transpose");
        vector<Expr> coords = make_arguments(f.func.args());
        vector<Expr> swizzled_coords = coords;
        std::swap(swizzled_coords[0], swizzled_coords[1]);

        transpose(coords) = f.func(swizzled_coords);

        return {transpose, f.h, f.w, f.c};
    }

    Stage slice(Stage f, Stage g) {
        std::cout << "Slice\n";
        if (f.c > g.c) {
            std::swap(f, g);
        }

        // Index g's channels using f

        f = resample_to(f, g.w, g.h, 1);

        Func sliced("sliced");
        vector<Expr> coords = make_arguments(f.func.args());
        coords.back() = clamp(cast<int>(f.func(f.func.args())), 0, g.c - 1);
        sliced(f.func.args()) = g.func(coords);

        return {sliced, f.w, f.h, f.c};
    }

    Stage tiled_histogram(Stage f) {
        // Make a histogram of NxN patches of f, preserving total size
        std::cout << "Tiled histogram\n";

        int old_c = f.c;
        f = resample_to(f, f.w, f.h, 1);

        int box_size = 1 << rand_int(1, 3);
        int histogram_buckets = box_size * box_size * old_c;

        RDom r(0, box_size, 0, box_size);
        vector<Expr> from_coords = make_arguments(f.func.args());
        vector<Expr> to_coords = from_coords;

        Func hist("hist");
        hist(f.func.args()) = 0.0f;
        from_coords[0] = to_coords[0] * box_size + r.x;
        from_coords[1] = to_coords[1] * box_size + r.y;
        from_coords[2] = 0;
        to_coords[2] = clamp(cast<int>(f.func(from_coords) * histogram_buckets), 0, histogram_buckets - 1);
        hist(to_coords) += 1;

        return {hist, f.w / box_size, f.h / box_size, histogram_buckets};
    }

    Stage resample_to(Stage f, int w, int h, int c) {
        std::cout << "Resampling from " << f.w << ", " << f.h << ", " << f.c << " to " << w << ", " << h << ", " << c << "\n";
        Stage out = f;
        // First decrease any sizes that need decreasing
        if (out.w > w) {
            int factor = (out.w + w/2) / w;
            if (factor != 1) {
                out = downsample(out, 0, factor);
            }
        }
        if (out.h > h) {
            int factor = (out.h + h/2) / h;
            if (factor != 1) {
                out = downsample(out, 1, (out.h + h/2) / h);
            }
        }
        // Adapt channel count with an all-to-all
        if (out.c != c) {
            out = all_to_all_r(out, 2);
            out.c = c;
        }
        // Increase any sizes that need increasing
        if (out.w < w) {
            int factor = (w + out.w/2) / out.w;
            if (factor != 1) {
                out = upsample(out, 0, factor);
            }
        }
        if (out.h < h) {
            int factor = (h + out.h/2) / out.h;
            if (factor != 1) {
                out = upsample(out, 1, factor);
            }
        }
        std::cout << "Resulting size: " << out.w << ", " << out.h << ", " << out.c << "\n";
        return out;
    }

    Stage cast_stage(Type t, Stage f) {
        Func casted("casted");
        casted(f.func.args()) = cast(t, f.func(f.func.args()));
        return {casted, f.w, f.h, f.c};
    }

    struct TransitionCDF {
        int num_states;
        int size;
        vector<float> cdf;

        void initialize(int n) {
            num_states = n;
            size = n*n;
            for (int i = 0; i < size; i++) {
                cdf.push_back(0.0f);
            }
        }

        float get(int i, int j) {
            int index = i*num_states+j;
            assert(index < size);
            return cdf[index];
        }

        void set(int i, int j, float val) {
            int index = i*num_states+j;
            assert(index < size);
            cdf[index] = val;
        }

        // use inverse transform sampling to sample next state given current state
        int sample_cdf(int state) {
            float sample_val = rand_float();
            for (int i = 0; i < num_states; i++) {
                if (get(state, i) >= sample_val) {
                    return i;
                }
            }
            return 1;
        }

        void print() {
            std::cout << std::setprecision(2) << std::fixed;
            for (int i = 0; i < num_states; i++) {
                for (int j = 0; j < num_states; j++) {
                    std::cout << " | " << get(i,j);
                }
                std::cout << " |\n";
            }
        }
    };

    // helper function to generate probability transition matrix between stages
    struct TransitionMatrix {
        int num_states; // number of states
        int size;

        // vector representaiton of 2D transition matrix.
        // value at (i,j) is probability of moving from state i to state j
        vector<float> probabilities;

        float get(int i, int j) {
            int index = i*num_states+j;
            assert(index < size);
            return probabilities[i*num_states+j];
        }

        void set(int i, int j, float val) {
            int index = i*num_states+j;
            assert(index < size);
            probabilities[index] = val;
        }

        void set_cdf(TransitionCDF& cdf) {
            assert(cdf.size == size);
            assert(cdf.num_states == num_states);

            for (int i = 0; i < num_states; i++) {
                float sum = 0.0f;
                for (int j = 0; j < num_states; j++) {
                    sum += get(i,j);
                    cdf.set(i,j,sum);
                }
            }
        }

        void initialize(int n) {
            num_states = n;
            size = n*n;
            // transition to every state equally likely
            float transition_prob = 1.0f/(num_states);
            for (int i = 0; i < size; i++) {
                probabilities.push_back(transition_prob);
            }
        }

        void print() {
            std::cout << std::setprecision(2) << std::fixed;
            for (int i = 0; i < num_states; i++) {
                for (int j = 0; j < num_states; j++) {
                    std::cout << " | " << get(i,j);
                }
                std::cout << " |\n";
            }
        }
    };


    // Generate a random stage using f as an input.
    Stage random_stage(const vector<Stage> &s, TransitionCDF& CDF, int& curr_stage_id) {
        int m = (int)s.size() - 1;
        int i2 = m > 0 ? rand_int(0, m - 1) : 0;
        int i1 = m > 0 ? rand_int(i2 + 1, m) : 0;
        Stage f = s[i1], g = s[i2];

        // generate stage based on transition probabilities
        int stage_type = CDF.sample_cdf(curr_stage_id);
        // set current stage id to the chosen stage for next iteration
        curr_stage_id = stage_type;


        if (stage_type == 0 && gen_conv1d) {
            int dim = rand_int(0, 1);
            int kernel_min = -1;//rand_int(-1, -1);
            int kernel_max = rand_int(1, 3);
            return convolve(f, dim, kernel_min, kernel_max);
        } else if (stage_type == 1 && gen_conv1d && false) {
            int dim = rand_int(0, 1);
            int kernel_min = rand_int(-10, -1);
            int kernel_max = rand_int(0, 10);
            return convolve_r(f, dim, kernel_min, kernel_max);
        } else if (stage_type == 2 && gen_conv1d && false) {
            int dim = rand_int(0, 1);
            int kernel_min = rand_int(-10, -1);
            int kernel_max = rand_int(0, 10);
            return convolve_w(f, dim, kernel_min, kernel_max);
        } else if (stage_type == 3 && gen_conv2d) {
            int kernel_min = rand_int(-5, 0);
            int kernel_max = rand_int(0, 5);
            return convolve2D(f, kernel_min, kernel_max);
        } else if (stage_type == 4 && f.may_reduce_size() && f.w >= 32 && f.h >= 32 && gen_pool) {
            int kernel_min = rand_int(-5, 0);
            int kernel_max = rand_int(0, 5);
            return pool2D(f, kernel_min, kernel_max);
        } else if (stage_type == 5 && gen_activate) {
            return activation(f);
        } else if (stage_type == 6 && gen_padding) {
            return padding(f);
        } else if (stage_type == 7 && f.may_increase_size() && gen_upsample) {
            // For now, only upsample dimensions 0 or 1.
            return upsample(f, rand_int(0, 1));
        } else if (stage_type == 8 && f.may_reduce_size() && gen_downsample) {
            // For now, only downsample dimensions 0 or 1.
            return downsample(f, rand_int(0, 1));
        } else if (stage_type == 9 && gen_all2all) {
            int dim = 2;
            return all_to_all(f, dim);
        } else if (stage_type == 10 && gen_all2all) {
            int dim = 2;
            return all_to_all_r(f, dim);
        } else if (stage_type == 11 && gen_all2all) {
            int dim = 2;
            return all_to_all_w(f, dim);
        } else if (stage_type == 12 && gen_scan) {
            int dim = rand_int(0, 2);
            return scan(f, dim);
        } else if (stage_type == 13 && false) {
            // TODO: transpose disabled for now because f(x, y) + f(y, x) totally breaks the bounds inference done by the autoscheduler.
            return transpose(f);
        } else if (stage_type == 14 && f.size() < 10000 &&
                   gen_exp && gen_sqrt && gen_exp) {
            return unary_op(f);
        } else if (stage_type == 15 && f.w > 32 && f.h > 32 && gen_histogram) {
            return tiled_histogram(f);
        } else if (stage_type == 16 && false) {
            return slice(f, g);
        } else if (i1 != i2) {
            return binary_op(f, g);
        } else if (i1 == i2) {
            return binary_op(f, leaf_op(f));
        } else {
            return random_stage(s, CDF, curr_stage_id);
        }
    }

    // Insert transition probabilities for deep network type stages
    void setup_transitions(TransitionMatrix& P) {
        int conv2D_id = 3;
        int pool2D_id = 4;
        int activation_id = 5;
        int padding_id = 6;
        float escape_prob; // prob of leaving a convnet state

        // P(activation | conv) = 0.8
        escape_prob = 0.2f/(num_stage_types-1);
        for (int i = 0; i < num_stage_types; i++) {
            if (i == activation_id) {
                P.set(conv2D_id, i, 0.8f);
            } else {
                P.set(conv2D_id, i, escape_prob);
            }
        }
        // P(padding | activation) = P(pool | activation) = 0.4
        escape_prob = 0.2f/(num_stage_types-2);
        for (int i = 0; i < num_stage_types; i++) {
            if (i == padding_id || i == pool2D_id) {
                P.set(activation_id, i, 0.4f);
            } else {
                P.set(activation_id, i, escape_prob);
            }
        }
        // P(conv | padding) = P(pool | padding) = 0.4
        escape_prob = 0.2f/(num_stage_types-2);
        for (int i = 0; i < num_stage_types; i++) {
            if (i == conv2D_id || i == pool2D_id) {
                P.set(padding_id, i, 0.4f);
            } else {
                P.set(padding_id, i, escape_prob);
            }
        }
        // P(conv | pool) = 0.8
        escape_prob = 0.2f/(num_stage_types-1);
        for (int i = 0; i < num_stage_types; i++) {
            if (i == conv2D_id) {
                P.set(pool2D_id, i, 0.8f);
            } else {
                P.set(pool2D_id, i, escape_prob);
            }
        }
    }

    void generate() {
        rng.seed((int)seed);

        // add div or mod based on gen_param
        if (gen_div) {
          make_bin_op.emplace_back(make_div_op);
        }
        if (gen_mod) {
          make_bin_op.emplace_back(make_mod_op);
        }
        bin_op_count = make_bin_op.size();

        // create transition matrix between stages
        TransitionMatrix P;
        P.initialize(num_stage_types);
        setup_transitions(P);
        TransitionCDF CDF;
        CDF.initialize(num_stage_types);
        P.set_cdf(CDF);

        Var x("x"), y("y"), c("c");

        Func uint8_weights;   uint8_weights(x, y) = 2;
        Func uint16_weights;  uint16_weights(x, y) = 2;
        Func uint32_weights;  uint32_weights(x, y) = 2;
        Func int8_weights;    int8_weights(x, y) = 2;
        Func int16_weights;   int16_weights(x, y) = 2;
        Func int32_weights;   int32_weights(x, y) = 2;
        Func float32_weights; float32_weights(x, y) = 2;


        Func hw_input;
        hw_input(x,y,c) = u16(input(x+6, y+6, c));
        Func first;
        first(x, y, c) = u16(hw_input(x, y, c));
        

        vector<Stage> stages;
        // Assume input starts at ~2000x2000
        stages.emplace_back(Stage{first, 2000, 2000, 3});
        // set starting stage type to a random stage
        int curr_stage_id = rand_int(0, num_stage_types-1);

        for (int i = 0; i < max_stages - 2; i++) {
            std::cout << "\tApprox size: " << stages.back().w << ", " << stages.back().h << ", " << stages.back().c << "\n";
            Stage next = random_stage(stages, CDF, curr_stage_id);
            stages.push_back(next);

            //stages.back().func.bound(c, 0, 1);
            if (!auto_schedule) {
              //stages.back().func.compute_root().reorder(x, c, y).vectorize(x, 8).parallel(y, 8);
            }
        }

        Stage tail = stages.back();

        // Resample back to the correct resolution
        if (gen_upsample) {
          tail = resample_to(tail, 2000, 2000, 3);
        }
        
        //Stage casted = cast_stage(output.type(), tail);
        
        Func hw_output;
        //hw_output = casted.func;
        hw_output = tail.func;
        
        output(x,y,c) = u8(hw_output(x,y,c));

        input.dim(2).set_bounds(0, 3);
        
        /* THE SCHEDULE */
        if (get_target().has_feature(Target::CoreIR)) {
        } else if (get_target().has_feature(Target::Clockwork)) {
          Var xi,yi, xo,yo;

          hw_output.bound(x,0,50);
          hw_output.bound(y,0,50);
          hw_output.bound(c,0,3);
          
          hw_output.compute_root();

          hw_output.tile(x,y, xo,yo, xi,yi, 50, 50)
            .reorder(xi, yi, c, xo, yo)
            .hw_accelerate(xi, xo);

          hw_input.stream_to_accelerator();
        
        } else if (!auto_schedule) {
          // schedule to CPU
          //output.compute_root().reorder(x, c, y).vectorize(x, 8).parallel(y);
          
        } else if (auto_schedule) {
            input.dim(0).set_bounds_estimate(0, 2000)
                .dim(1).set_bounds_estimate(0, 2000)
                .dim(2).set_bounds_estimate(0, 3);
//            uint8_weights.dim(0).set_bounds_estimate(0, 512)
//                .dim(1).set_bounds_estimate(-5, 5)
//                .dim(2).set_bounds_estimate(-5, 5)
//                .dim(3).set_bounds_estimate(0, 512);
//            uint16_weights.dim(0).set_bounds_estimate(0, 512)
//                .dim(1).set_bounds_estimate(-5, 5)
//                .dim(2).set_bounds_estimate(-5, 5)
//                .dim(3).set_bounds_estimate(0, 512);
//            uint32_weights.dim(0).set_bounds_estimate(0, 512)
//                .dim(1).set_bounds_estimate(-5, 5)
//                .dim(2).set_bounds_estimate(-5, 5)
//                .dim(3).set_bounds_estimate(0, 512);
//            int8_weights.dim(0).set_bounds_estimate(0, 512)
//                .dim(1).set_bounds_estimate(-5, 5)
//                .dim(2).set_bounds_estimate(-5, 5)
//                .dim(3).set_bounds_estimate(0, 512);
//            int16_weights.dim(0).set_bounds_estimate(0, 512)
//                .dim(1).set_bounds_estimate(-5, 5)
//                .dim(2).set_bounds_estimate(-5, 5)
//                .dim(3).set_bounds_estimate(0, 512);
//            int32_weights.dim(0).set_bounds_estimate(0, 512)
//                .dim(1).set_bounds_estimate(-5, 5)
//                .dim(2).set_bounds_estimate(-5, 5)
//                .dim(3).set_bounds_estimate(0, 512);
//            float32_weights.dim(0).set_bounds_estimate(0, 512)
//                .dim(1).set_bounds_estimate(-5, 5)
//                .dim(2).set_bounds_estimate(-5, 5)
//                .dim(3).set_bounds_estimate(0, 512);

            output.estimate(output.args()[0], 0, 2000);
            output.estimate(output.args()[0], 0, 2000);
            output.estimate(output.args()[1], 0, 2000);
            output.estimate(output.args()[2], 0, 3);

            output.dim(0).set_bounds(0, 2000);
            output.dim(1).set_bounds(0, 2000);
            output.dim(2).set_bounds(0, 3);
        }
    }
};



HALIDE_REGISTER_GENERATOR(RandomPipeline, random_pipeline)
