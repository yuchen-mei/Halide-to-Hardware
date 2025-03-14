
#include "coreir.h"

#include <algorithm>
#include <iostream>
#include <set>
#include <sstream>

#include "Lower.h"

#include "AddImageChecks.h"
#include "AddParameterChecks.h"
#include "AllocationBoundsInference.h"
#include "AsyncProducers.h"
#include "BoundSmallAllocations.h"
#include "Bounds.h"
#include "BoundsInference.h"
#include "CSE.h"
#include "CanonicalizeGPUVars.h"
#include "Debug.h"
#include "DebugArguments.h"
#include "DebugToFile.h"
#include "Deinterleave.h"
#include "EarlyFree.h"
#include "ExtractHWAccelerators.h"
#include "ExtractHWKernelDAG.h"
#include "ExtractHWBuffers.h"
#include "EmulateFloat16Math.h"
#include "FindCalls.h"
#include "Func.h"
#include "Function.h"
#include "FuseGPUThreadLoops.h"
#include "FuzzFloatStores.h"
#include "HexagonOffload.h"
#include "HWBufferRename.h"
#include "HWBufferSimplifications.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "IRPrinter.h"
#include "InferArguments.h"
#include "InjectHostDevBufferCopies.h"
#include "InjectOpenGLIntrinsics.h"
#include "Inline.h"
#include "InsertHWBuffers.h"
#include "LICM.h"
#include "LoopCarry.h"
#include "LowerWarpShuffles.h"
#include "MarkHWKernels.h"
#include "Memoization.h"
#include "PartitionLoops.h"
#include "PurifyIndexMath.h"
#include "Prefetch.h"
#include "Profiling.h"
#include "Qualify.h"
#include "RealizationOrder.h"
#include "RemoveDeadAllocations.h"
#include "RemoveExternLoops.h"
#include "RemoveTrivialForLoops.h"
#include "RemoveUndef.h"
#include "ScheduleFunctions.h"
#include "SelectGPUAPI.h"
#include "Simplify.h"
#include "SimplifySpecializations.h"
#include "SkipStages.h"
#include "SlidingWindow.h"
#include "SplitTuples.h"
#include "StorageFlattening.h"
#include "StorageFolding.h"
#include "StreamOpt.h"
#include "StrictifyFloat.h"
#include "Substitute.h"
#include "Tracing.h"
#include "TrimNoOps.h"
#include "UBufferRewrites.h"
#include "UnifyDuplicateLets.h"
#include "UniquifyVariableNames.h"
#include "UnpackBuffers.h"
#include "UnsafePromises.h"
#include "UnrollLoops.h"
#include "VaryingAttributes.h"
#include "VectorizeLoops.h"
#include "WrapCalls.h"
#include "WrapExternStages.h"

namespace Halide {
namespace Internal {

using std::map;
using std::ostringstream;
using std::set;
using std::string;
using std::vector;

Module lower(const vector<Function> &output_funcs, const string &pipeline_name, const Target &t,
             const vector<Argument> &args, const LinkageType linkage_type,
             const vector<IRMutator *> &custom_passes) {

    //std::cout << "Starting lowering..." << std::endl;
    //std::cout << "Target = " << t << std::endl;
    std::vector<std::string> namespaces;
    std::string simple_pipeline_name = extract_namespaces(pipeline_name, namespaces);

    Module result_module(simple_pipeline_name, t);

    // Compute an environment
    map<string, Function> env;
    for (Function f : output_funcs) {
        populate_environment(f, env);
    }

    // Create a deep-copy of the entire graph of Funcs.
    vector<Function> outputs;
    std::tie(outputs, env) = deep_copy(output_funcs, env);

    bool any_strict_float = strictify_float(env, t);
    result_module.set_any_strict_float(any_strict_float);

    // Output functions should all be computed and stored at root.
    for (Function f: outputs) {
        Func(f).compute_root().store_root();
    }

    // Finalize all the LoopLevels
    for (auto &iter : env) {
        iter.second.lock_loop_levels();
    }

    // Substitute in wrapper Funcs
    env = wrap_func_calls(env);

    // Compute a realization order and determine group of functions which loops
    // are to be fused together
    vector<string> order;
    vector<vector<string>> fused_groups;
    std::tie(order, fused_groups) = realization_order(outputs, env);

    // Try to simplify the RHS/LHS of a function definition by propagating its
    // specializations' conditions
    simplify_specializations(env);

    debug(1) << "Creating initial loop nests...\n";
    bool any_memoized = false;
    Stmt s = schedule_functions(outputs, fused_groups, env, t, any_memoized);
    debug(2) << "Lowering after creating initial loop nests:\n" << s << '\n';
    //std::cout << "Lowering after creating initial loop nests:\n" << s << '\n';

    if (any_memoized) {
        debug(1) << "Injecting memoization...\n";
        s = inject_memoization(s, env, pipeline_name, outputs);
        debug(2) << "Lowering after injecting memoization:\n" << s << '\n';
    } else {
        debug(1) << "Skipping injecting memoization...\n";
    }

    debug(1) << "Injecting tracing...\n";
    s = inject_tracing(s, pipeline_name, env, outputs, t);
    debug(2) << "Lowering after injecting tracing:\n" << s << '\n';

    debug(1) << "Adding checks for parameters\n";
    s = add_parameter_checks(s, t);
    debug(2) << "Lowering after injecting parameter checks:\n" << s << '\n';

    // Compute the maximum and minimum possible value of each
    // function. Used in later bounds inference passes.
    debug(1) << "Computing bounds of each function's value\n";
    FuncValueBounds func_bounds = compute_function_value_bounds(order, env);
    //std::cout << "FuncValueBounds..." << std::endl;
    //for (auto fEntry : func_bounds) {
      //std::cout << "\t" << fEntry.first.first << " : " << fEntry.first.second <<
      //" -> " << "[" << fEntry.second.min << ", " << fEntry.second.max << "]" << std::endl;
    //}

    // The checks will be in terms of the symbols defined by bounds
    // inference.
    debug(1) << "Adding checks for images\n";
    s = add_image_checks(s, outputs, t, order, env, func_bounds);
    debug(2) << "Lowering after injecting image checks:\n" << s << '\n';

    // This pass injects nested definitions of variable names, so we
    // can't simplify statements from here until we fix them up. (We
    // can still simplify Exprs).
    vector<BoundsInference_Stage> inlined_stages;
    debug(1) << "Performing computation bounds inference...\n";
    s = bounds_inference(s, outputs, order, fused_groups, env, func_bounds, inlined_stages, t);
    debug(2) << "Lowering after computation bounds inference:\n" << s << '\n';
    //std::cout << "#### After bounds inference: " << s << "\n";

    debug(1) << "Removing extern loops...\n";
    s = remove_extern_loops(s);
    debug(2) << "Lowering after removing extern loops:\n" << s << '\n';

    debug(1) << "Performing sliding window optimization...\n";
    if (!t.has_feature(Target::CoreIRHLS) &&
        !t.has_feature(Target::CoreIR) &&
        !t.has_feature(Target::HLS)) {
      s = sliding_window(s, env);
    }
    debug(2) << "Lowering after sliding window:\n" << s << '\n';

    debug(1) << "Performing allocation bounds inference...\n";
    s = allocation_bounds_inference(s, env, func_bounds);
    debug(2) << "Lowering after allocation bounds inference:\n" << s << '\n';

    //std::cout << "doing sliding window lowering pass\n" << s;
    Stmt s_sliding;
    if (t.has_feature(Target::CoreIR)) {
      s_sliding = sliding_window(s, env);
      //std::cout << "finished sliding window lowering pass\n" << s;
    }

    debug(1) << "Removing code that depends on undef values...\n";
    s = remove_undef(s);
    debug(2) << "Lowering after removing code that depends on undef values:\n" << s << "\n\n";

    // This uniquifies the variable names, so we're good to simplify
    // after this point. This lets later passes assume syntactic
    // equivalence means semantic equivalence.
    debug(1) << "Uniquifying variable names...\n";
    s = uniquify_variable_names(s);
    debug(2) << "Lowering after uniquifying variable names:\n" << s << "\n\n";

    //bool use_ubuffer = !t.has_feature(Target::UseExtractHWKernel);
    bool use_ubuffer = true;
    //!t.has_feature(Target::UseExtractHWKernel);
    //cout << "Should use ubuffer ? " << use_ubuffer << endl;

    //if (t.has_feature(Target::UseExtractHWKernel) && t.has_feature(Target::CoreIR)) {
    //  vector<HWXcel> buf_xcels =
    //    extract_hw_accelerators(s_sliding, env, inlined_stages);
    //  synthesize_hwbuffers(s, env, buf_xcels);
    //}

    if (t.has_feature(Target::Clockwork)) {
      s = extract_hwaccelerators(s, env);
      //std::cout << "IR after hwxcel extracted:\n" << s << std::endl;
    }
    
    if (t.has_feature(Target::CoreIR) || t.has_feature(Target::HLS)) {
      // passes specific to HLS backend
      debug(1) << "Performing HLS target optimization..\n";
      //std::cout << "Performing HLS target optimization..." << s << '\n';

      if (use_ubuffer) {
        // hardware generation using the unified buffer
        vector<HWXcel> xcels;

        //std::cout << "extracting hw buffers" << std::endl << s << std::endl;
        xcels = extract_hw_accelerators(s_sliding, env, inlined_stages);
        synthesize_hwbuffers(s, env, xcels);

        //std::cout << "----- Accelerators" << std::endl;
        //for (auto xcel : xcels) {
        //std::cout << "\t" << xcel.name << std::endl;
        //}

        //for (auto hwbuffer : xcels.at(0).hwbuffers) {
        //std::cout << hwbuffer.first << " is lower w/ inline=" << hwbuffer.second.is_inlined << std::endl;
        //}

        //std::cout << "--- Before inserting hwbuffers" << std::endl;
        //std::cout << s << std::endl;
        for (const HWXcel &xcel : xcels) {
          s = insert_hwbuffers(s, xcel);
        }
        //std::cout << "--- After inserting hwbuffers" << std::endl;
        //std::cout << s << std::endl;

      } else {
        // older hardware generation passes for linebuffers
        vector<HWKernelDAG> dags;
        s = extract_hw_kernel_dag(s, env, inlined_stages, dags);

        //std::cout << "Lowering before HLS optimization:\n" << s << '\n';

        for(const HWKernelDAG &dag : dags) {
          s = stream_opt(s, dag);
          //s = replace_image_param(s, dag);
        }
      }

      debug(2) << "Lowering after HLS optimization:\n" << s << '\n';
      //std::cout << "Lowering after HLS optimization:\n" << s << '\n';
    }

    debug(1) << "Simplifying...\n";
    s = simplify(s, false); // Storage folding needs .loop_max symbols
    debug(2) << "Lowering after first simplification:\n" << s << "\n\n";
    //std::cout << "Lowering after first simplification:\n" << s << "\n\n";

    //std::cout << "Before storage folding...\n" << s << "\n\n";
    debug(1) << "Performing storage folding optimization...\n";
      s = storage_folding(s, env);
    debug(2) << "Lowering after storage folding:\n" << s << '\n';

    debug(1) << "Injecting debug_to_file calls...\n";
    s = debug_to_file(s, outputs, env);
    debug(2) << "Lowering after injecting debug_to_file calls:\n" << s << '\n';

    debug(1) << "Injecting prefetches...\n";
    s = inject_prefetch(s, env);
    debug(2) << "Lowering after injecting prefetches:\n" << s << "\n\n";

    debug(1) << "Dynamically skipping stages...\n";
    s = skip_stages(s, order);
    debug(2) << "Lowering after dynamically skipping stages:\n" << s << "\n\n";
    //std::cout << "Lowering after dynamically skipping stages:\n" << s << "\n\n";

    if (!t.has_feature(Target::CoreIRHLS) &&
        !t.has_feature(Target::CoreIR) &&
        !t.has_feature(Target::HLS) &&
        !t.has_feature(Target::Clockwork)) { // FIXME: don't omit this pass globally with CoreIR
      debug(1) << "Forking asynchronous producers...\n";
      s = fork_async_producers(s, env);
      debug(2) << "Lowering after forking asynchronous producers:\n" << s << '\n';
    }

    debug(1) << "Destructuring tuple-valued realizations...\n";
    s = split_tuples(s, env);
    debug(2) << "Lowering after destructuring tuple-valued realizations:\n" << s << "\n\n";

    // OpenGL relies on GPU var canonicalization occurring before
    // storage flattening.
    if (t.has_gpu_feature() ||
        t.has_feature(Target::OpenGLCompute) ||
        t.has_feature(Target::OpenGL)) {
        debug(1) << "Canonicalizing GPU var names...\n";
        s = canonicalize_gpu_vars(s);
        debug(2) << "Lowering after canonicalizing GPU var names:\n"
                 << s << '\n';
    }

    debug(1) << "Performing storage flattening...\n";
    //std::cout << "Before storage flattening...\n" << s << "\n\n";

    if (t.has_feature(Target::Clockwork)) {
      s = rename_hwbuffers(s, env);
    }
    
    s = storage_flattening(s, outputs, env, t);

    debug(2) << "Lowering after storage flattening:\n" << s << "\n\n";
    //std::cout << "Lowering after storage flattening:\n" << s << "\n\n";

    debug(1) << "Unpacking buffer arguments...\n";
    s = unpack_buffers(s);
    debug(2) << "Lowering after unpacking buffer arguments...\n" << s << "\n\n";

    if (any_memoized) {
        debug(1) << "Rewriting memoized allocations...\n";
        s = rewrite_memoized_allocations(s, env);
        debug(2) << "Lowering after rewriting memoized allocations:\n" << s << "\n\n";
    } else {
        debug(1) << "Skipping rewriting memoized allocations...\n";
    }

    if (t.has_gpu_feature() ||
        t.has_feature(Target::OpenGLCompute) ||
        t.has_feature(Target::OpenGL) ||
        t.has_feature(Target::HexagonDma) ||
        (t.arch != Target::Hexagon && (t.features_any_of({Target::HVX_64, Target::HVX_128})))) {
        debug(1) << "Selecting a GPU API for GPU loops...\n";
        s = select_gpu_api(s, t);
        debug(2) << "Lowering after selecting a GPU API:\n" << s << "\n\n";

        debug(1) << "Injecting host <-> dev buffer copies...\n";
        s = inject_host_dev_buffer_copies(s, t);
        debug(2) << "Lowering after injecting host <-> dev buffer copies:\n" << s << "\n\n";

        debug(1) << "Selecting a GPU API for extern stages...\n";
        s = select_gpu_api(s, t);
        debug(2) << "Lowering after selecting a GPU API for extern stages:\n" << s << "\n\n";
    }

    if (t.has_feature(Target::OpenGL)) {
        debug(1) << "Injecting OpenGL texture intrinsics...\n";
        s = inject_opengl_intrinsics(s);
        debug(2) << "Lowering after OpenGL intrinsics:\n" << s << "\n\n";
    }

    debug(1) << "Simplifying...\n";
    s = simplify(s);
    s = unify_duplicate_lets(s);
    s = remove_trivial_for_loops(s);
    debug(2) << "Lowering after second simplifcation:\n" << s << "\n\n";
    //std::cout << "Lowering after second simplifcation:\n" << s << "\n\n";

    debug(1) << "Reduce prefetch dimension...\n";
    s = reduce_prefetch_dimension(s, t);
    debug(2) << "Lowering after reduce prefetch dimension:\n" << s << "\n";

    //std::cout << "Before unrolling:\n" << s << "\n\n";    
    debug(1) << "Unrolling...\n";
    if (t.has_feature(Target::Clockwork)) {
      s = unroll_loops_and_merge(s);
      //std::cout << "After unrolling:\n" << s << "\n\n";
      s = inline_memory_constants(s);
      //std::cout << "After inlining:\n" << s << "\n\n";
    } else {
      s = unroll_loops(s);
    }

    s = simplify(s);
    debug(2) << "Lowering after unrolling:\n" << s << "\n\n";
    //std::cout << "Lowering after unrolling:\n" << s << "\n\n";

    debug(1) << "Vectorizing...\n";
    s = vectorize_loops(s, t);
    s = simplify(s);
    debug(2) << "Lowering after vectorizing:\n" << s << "\n\n";

    if (t.has_gpu_feature() ||
        t.has_feature(Target::OpenGLCompute)) {
        debug(1) << "Injecting per-block gpu synchronization...\n";
        s = fuse_gpu_thread_loops(s);
        debug(2) << "Lowering after injecting per-block gpu synchronization:\n" << s << "\n\n";
    }

    debug(1) << "Detecting vector interleavings...\n";
    s = rewrite_interleavings(s);
    s = simplify(s);
    debug(2) << "Lowering after rewriting vector interleavings:\n" << s << "\n\n";

    //std::cout << "Partitioning loops to simplify boundary conditions...\n" << s << '\n';
    debug(1) << "Partitioning loops to simplify boundary conditions...\n";
    s = partition_loops(s);
    s = simplify(s);
    debug(2) << "Lowering after partitioning loops:\n" << s << "\n\n";
    //std::cout << "Lowering after partitioning loops:\n" << s << "\n\n";

    debug(1) << "Trimming loops to the region over which they do something...\n";
    s = trim_no_ops(s);
    debug(2) << "Lowering after loop trimming:\n" << s << "\n\n";

    debug(1) << "Injecting early frees...\n";
    s = inject_early_frees(s);
    debug(2) << "Lowering after injecting early frees:\n" << s << "\n\n";

    if (t.has_feature(Target::Profile)) {
        debug(1) << "Injecting profiling...\n";
        s = inject_profiling(s, pipeline_name);
        debug(2) << "Lowering after injecting profiling:\n" << s << "\n\n";
    }

    if (t.has_feature(Target::FuzzFloatStores)) {
        debug(1) << "Fuzzing floating point stores...\n";
        s = fuzz_float_stores(s);
        debug(2) << "Lowering after fuzzing floating point stores:\n" << s << "\n\n";
    }

    debug(1) << "Bounding small allocations...\n";
    s = bound_small_allocations(s);
    debug(2) << "Lowering after bounding small allocations:\n" << s << "\n\n";

    if (t.has_feature(Target::CUDA)) {
        debug(1) << "Injecting warp shuffles...\n";
        s = lower_warp_shuffles(s);
        debug(2) << "Lowering after injecting warp shuffles:\n" << s << "\n\n";
    }

    debug(1) << "Simplifying...\n";
    s = common_subexpression_elimination(s);

    if (t.has_feature(Target::OpenGL)) {
        debug(1) << "Detecting varying attributes...\n";
        s = find_linear_expressions(s);
        debug(2) << "Lowering after detecting varying attributes:\n" << s << "\n\n";

        debug(1) << "Moving varying attribute expressions out of the shader...\n";
        s = setup_gpu_vertex_buffer(s);
        debug(2) << "Lowering after removing varying attributes:\n" << s << "\n\n";
    }

    debug(1) << "Lowering unsafe promises...\n";
    s = lower_unsafe_promises(s, t);
    debug(2) << "Lowering after lowering unsafe promises:\n" << s << "\n\n";

    debug(1) << "Emulating float16 math...\n";
    //std::cout << "Emulating float16 math...\n";
    //if (!t.has_feature(Target::Clockwork)) {
      s = emulate_float16_math(s, t);
      //}
    debug(2) << "Lowering after emulating float16 math:\n" << s << "\n\n";
    //std::cout << "Lowering after emulating float16 math:\n" << s << "\n\n";

    s = remove_dead_allocations(s);
    s = remove_trivial_for_loops(s);
    s = simplify(s);
    //std::cout << "Lowering before final simplification:\n" << s << "\n\n";
    s = loop_invariant_code_motion(s);
    debug(1) << "Lowering after final simplification:\n" << s << "\n\n";
    std::cout << "Lowering after final simplification:\n" << s << "\n\n";

    if (t.arch != Target::Hexagon && (t.features_any_of({Target::HVX_64, Target::HVX_128}))) {
        debug(1) << "Splitting off Hexagon offload...\n";
        s = inject_hexagon_rpc(s, t, result_module);
        debug(2) << "Lowering after splitting off Hexagon offload:\n" << s << '\n';
    } else {
        debug(1) << "Skipping Hexagon offload...\n";
    }
    //std::cout << "after passes: " << s << std::endl;

    if (!custom_passes.empty()) {
        for (size_t i = 0; i < custom_passes.size(); i++) {
            debug(1) << "Running custom lowering pass " << i << "...\n";
            s = custom_passes[i]->mutate(s);
            debug(1) << "Lowering after custom pass " << i << ":\n" << s << "\n\n";
        }
    }

    vector<Argument> public_args = args;
    for (const auto &out : outputs) {
        for (Parameter buf : out.output_buffers()) {
            public_args.push_back(Argument(buf.name(),
                                           Argument::OutputBuffer,
                                           buf.type(), buf.dimensions(), buf.get_argument_estimates()));
        }
    }

    vector<InferredArgument> inferred_args = infer_arguments(s, outputs);
    for (const InferredArgument &arg : inferred_args) {
        if (arg.param.defined() && arg.param.name() == "__user_context") {
            // The user context is always in the inferred args, but is
            // not required to be in the args list.
            continue;
        }

        internal_assert(arg.arg.is_input()) << "Expected only input Arguments here";

        bool found = false;
        for (Argument a : args) {
            found |= (a.name == arg.arg.name);
        }

        if (arg.buffer.defined() && !found) {
            // It's a raw Buffer used that isn't in the args
            // list. Embed it in the output instead.
            debug(1) << "Embedding image " << arg.buffer.name() << "\n";
            result_module.append(arg.buffer);
        } else if (!found) {
            std::ostringstream err;
            err << "Generated code refers to ";
            if (arg.arg.is_buffer()) {
                err << "image ";
            }
            err << "parameter " << arg.arg.name
                << ", which was not found in the argument list.\n";

            err << "\nArgument list specified: ";
            for (size_t i = 0; i < args.size(); i++) {
                err << args[i].name << " ";
            }
            err << "\n\nParameters referenced in generated code: ";
            for (const InferredArgument &ia : inferred_args) {
                if (ia.arg.name != "__user_context") {
                    err << ia.arg.name << " ";
                }
            }
            err << "\n\n";
            user_error << err.str();
        }
    }

    // We're about to drop the environment and outputs vector, which
    // contain the only strong refs to Functions that may still be
    // pointed to by the IR. So make those refs strong.
    class StrengthenRefs : public IRMutator {
        using IRMutator::visit;
        Expr visit(const Call *c) override {
            Expr expr = IRMutator::visit(c);
            c = expr.as<Call>();
            internal_assert(c);
            if (c->func.defined()) {
                FunctionPtr ptr = c->func;
                ptr.strengthen();
                expr = Call::make(c->type, c->name, c->args, c->call_type,
                                  ptr, c->value_index,
                                  c->image, c->param);
            }
            return expr;
        }
    };
    s = StrengthenRefs().mutate(s);

    LoweredFunc main_func(pipeline_name, public_args, s, linkage_type);

    // If we're in debug mode, add code that prints the args.
    if (t.has_feature(Target::Debug)) {
        debug_arguments(&main_func);
    }

    //if (!t.has_feature(Target::HLS)) {
    result_module.append(main_func);
    //}

    // Append a wrapper for this pipeline that accepts old buffer_ts
    // and upgrades them. It will use the same name, so it will
    // require C++ linkage. We don't need it when jitting.
    if (!t.has_feature(Target::JIT)) {
        add_legacy_wrapper(result_module, main_func);
    }

    return result_module;
}

Stmt lower_main_stmt(const std::vector<Function> &output_funcs, const std::string &pipeline_name,
                     const Target &t, const std::vector<IRMutator *> &custom_passes) {
    // We really ought to start applying for appellation d'origine contrôlée
    // status on types representing arguments in the Halide compiler.
    vector<InferredArgument> inferred_args = infer_arguments(Stmt(), output_funcs);
    vector<Argument> args;
    for (const auto &ia : inferred_args) {
        if (!ia.arg.name.empty() && ia.arg.is_input()) {
            args.push_back(ia.arg);
        }
    }

    Module module = lower(output_funcs, pipeline_name, t, args, LinkageType::External, custom_passes);

    return module.functions().front().body;
}

}  // namespace Internal
}  // namespace Halide
