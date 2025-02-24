#include "app.h"
#include "ubuffer.h"
#include "codegen.h"
#include "prog.h"

prog resnet_output_stationary() {
  prog prg;
  prg.compute_unit_file = "resnet_output_stationary_compute.h";
  prg.name = "resnet_output_stationary";

// Stencil<uint16_t, 4, 58, 58> &input_host_stencil = arg_0;
  prg.add_input("input_host_stencil");
  prg.buffer_port_widths["input_host_stencil"] = 16;
// Stencil<uint16_t, 4, 16, 3, 3> &kernel_host_stencil = arg_1;
  prg.add_input("kernel_host_stencil");
  prg.buffer_port_widths["kernel_host_stencil"] = 16;
// Stencil<uint16_t, 16, 56, 56> &hw_output_stencil = arg_2;
  prg.add_output("hw_output_stencil");
  prg.buffer_port_widths["hw_output_stencil"] = 16;

////producing input_glb.stencil
  auto input_glb_s0_y = prg.add_loop("input_glb_s0_y", 0, 58);
  auto input_glb_s0_x = input_glb_s0_y->add_loop("input_glb_s0_x", 0, 58);
  auto input_glb_s0_z = input_glb_s0_x->add_loop("input_glb_s0_z", 0, 4);

//store is: input_glb.stencil(input_glb_s0_z, input_glb_s0_x, input_glb_s0_y) = input_host.stencil(input_glb_s0_z, input_glb_s0_x, input_glb_s0_y)
  auto hcompute_input_glb_stencil = input_glb_s0_z->add_op("op_hcompute_input_glb_stencil");
  hcompute_input_glb_stencil->add_function("hcompute_input_glb_stencil");
  hcompute_input_glb_stencil->add_load("input_host_stencil", "input_glb_s0_y", "input_glb_s0_x", "input_glb_s0_z");
  prg.buffer_port_widths["input_glb_stencil"] = 16;
  hcompute_input_glb_stencil->add_store("input_glb_stencil", "input_glb_s0_y", "input_glb_s0_x", "input_glb_s0_z");

//consuming input_glb.stencil
////producing kernel_glb.stencil
  auto kernel_glb_s0_y = prg.add_loop("kernel_glb_s0_y", 0, 3);
  auto kernel_glb_s0_x = kernel_glb_s0_y->add_loop("kernel_glb_s0_x", 0, 3);
  auto kernel_glb_s0_w = kernel_glb_s0_x->add_loop("kernel_glb_s0_w", 0, 16);
  auto kernel_glb_s0_z = kernel_glb_s0_w->add_loop("kernel_glb_s0_z", 0, 4);

//store is: kernel_glb.stencil(kernel_glb_s0_z, kernel_glb_s0_w, kernel_glb_s0_x, kernel_glb_s0_y) = kernel_host.stencil(kernel_glb_s0_z, kernel_glb_s0_w, kernel_glb_s0_x, kernel_glb_s0_y)
  auto hcompute_kernel_glb_stencil = kernel_glb_s0_z->add_op("op_hcompute_kernel_glb_stencil");
  hcompute_kernel_glb_stencil->add_function("hcompute_kernel_glb_stencil");
  hcompute_kernel_glb_stencil->add_load("kernel_host_stencil", "kernel_glb_s0_y", "kernel_glb_s0_x", "kernel_glb_s0_w", "kernel_glb_s0_z");
  prg.buffer_port_widths["kernel_glb_stencil"] = 16;
  hcompute_kernel_glb_stencil->add_store("kernel_glb_stencil", "kernel_glb_s0_y", "kernel_glb_s0_x", "kernel_glb_s0_w", "kernel_glb_s0_z");

//consuming kernel_glb.stencil
////producing output_glb.stencil
  auto output_glb_s0_w_w_glb = prg.add_loop("output_glb_s0_w_w_glb", 0, 4);
  auto output_glb_s0_y_y_glb = output_glb_s0_w_w_glb->add_loop("output_glb_s0_y_y_glb", 0, 2);
  auto output_glb_s0_x_x_glb = output_glb_s0_y_y_glb->add_loop("output_glb_s0_x_x_glb", 0, 2);
////producing output_cgra.stencil
  auto output_cgra_s0_y = output_glb_s0_x_x_glb->add_loop("output_cgra_s0_y", 0, 28);
  auto output_cgra_s0_x = output_cgra_s0_y->add_loop("output_cgra_s0_x", 0, 28);
  auto output_cgra_s0_w_w_cgra = output_cgra_s0_x->add_loop("output_cgra_s0_w_w_cgra", 0, 4);

//store is: output_cgra.stencil(output_cgra_s0_w_w_cgra, output_cgra_s0_x, output_cgra_s0_y) = (uint16)0
  auto hcompute_output_cgra_stencil = output_cgra_s0_w_w_cgra->add_op("op_hcompute_output_cgra_stencil");
  hcompute_output_cgra_stencil->add_function("hcompute_output_cgra_stencil");
  prg.buffer_port_widths["output_cgra_stencil"] = 16;
  hcompute_output_cgra_stencil->add_store("output_cgra_stencil", "output_cgra_s0_y", "output_cgra_s0_x", "output_cgra_s0_w_w_cgra");
////producing input_cgra.stencil
  auto input_cgra_s0_y = output_glb_s0_x_x_glb->add_loop("input_cgra_s0_y", 0, 30);
  auto input_cgra_s0_x = input_cgra_s0_y->add_loop("input_cgra_s0_x", 0, 30);
  auto input_cgra_s0_z_z_cgra = input_cgra_s0_x->add_loop("input_cgra_s0_z_z_cgra", 0, 4);

//store is: input_cgra.stencil(input_cgra_s0_z_z_cgra, input_cgra_s0_x, input_cgra_s0_y) = input_glb.stencil(input_cgra_s0_z_z_cgra, ((output_glb_s0_x_x_glb*28) + input_cgra_s0_x), ((output_glb_s0_y_y_glb*28) + input_cgra_s0_y))
  auto hcompute_input_cgra_stencil = input_cgra_s0_z_z_cgra->add_op("op_hcompute_input_cgra_stencil");
  hcompute_input_cgra_stencil->add_function("hcompute_input_cgra_stencil");
  hcompute_input_cgra_stencil->add_load("input_glb_stencil", "((output_glb_s0_y_y_glb*28) + input_cgra_s0_y)", "((output_glb_s0_x_x_glb*28) + input_cgra_s0_x)", "input_cgra_s0_z_z_cgra");
  prg.buffer_port_widths["input_cgra_stencil"] = 16;
  hcompute_input_cgra_stencil->add_store("input_cgra_stencil", "input_cgra_s0_y", "input_cgra_s0_x", "input_cgra_s0_z_z_cgra");

//consuming input_cgra.stencil
////producing kernel_cgra.stencil
  auto kernel_cgra_s0_y = output_glb_s0_x_x_glb->add_loop("kernel_cgra_s0_y", 0, 3);
  auto kernel_cgra_s0_x = kernel_cgra_s0_y->add_loop("kernel_cgra_s0_x", 0, 3);
  auto kernel_cgra_s0_w_w_cgra = kernel_cgra_s0_x->add_loop("kernel_cgra_s0_w_w_cgra", 0, 4);
  auto kernel_cgra_s0_z_z_cgra = kernel_cgra_s0_w_w_cgra->add_loop("kernel_cgra_s0_z_z_cgra", 0, 4);

//store is: kernel_cgra.stencil(kernel_cgra_s0_z_z_cgra, kernel_cgra_s0_w_w_cgra, kernel_cgra_s0_x, kernel_cgra_s0_y) = kernel_glb.stencil(kernel_cgra_s0_z_z_cgra, ((output_glb_s0_w_w_glb*4) + kernel_cgra_s0_w_w_cgra), kernel_cgra_s0_x, kernel_cgra_s0_y)
  auto hcompute_kernel_cgra_stencil = kernel_cgra_s0_z_z_cgra->add_op("op_hcompute_kernel_cgra_stencil");
  hcompute_kernel_cgra_stencil->add_function("hcompute_kernel_cgra_stencil");
  hcompute_kernel_cgra_stencil->add_load("kernel_glb_stencil", "kernel_cgra_s0_y", "kernel_cgra_s0_x", "((output_glb_s0_w_w_glb*4) + kernel_cgra_s0_w_w_cgra)", "kernel_cgra_s0_z_z_cgra");
  prg.buffer_port_widths["kernel_cgra_stencil"] = 16;
  hcompute_kernel_cgra_stencil->add_store("kernel_cgra_stencil", "kernel_cgra_s0_y", "kernel_cgra_s0_x", "kernel_cgra_s0_w_w_cgra", "kernel_cgra_s0_z_z_cgra");

//consuming kernel_cgra.stencil
  auto output_cgra_s1_r_y = output_glb_s0_x_x_glb->add_loop("output_cgra_s1_r_y", 0, 3);
  auto output_cgra_s1_r_x = output_cgra_s1_r_y->add_loop("output_cgra_s1_r_x", 0, 3);
  auto output_cgra_s1_y = output_cgra_s1_r_x->add_loop("output_cgra_s1_y", 0, 28);
  auto output_cgra_s1_x = output_cgra_s1_y->add_loop("output_cgra_s1_x", 0, 28);

//store is: output_cgra.stencil(0, output_cgra_s1_x, output_cgra_s1_y) = ((kernel_cgra.stencil(0, 0, output_cgra_s1_r_x, output_cgra_s1_r_y)*input_cgra.stencil(0, (output_cgra_s1_r_x + output_cgra_s1_x), (output_cgra_s1_r_y + output_cgra_s1_y))) + (output_cgra.stencil(0, output_cgra_s1_x, output_cgra_s1_y) + ((kernel_cgra.stencil(1, 0, output_cgra_s1_r_x, output_cgra_s1_r_y)*input_cgra.stencil(1, (output_cgra_s1_r_x + output_cgra_s1_x), (output_cgra_s1_r_y + output_cgra_s1_y))) + ((kernel_cgra.stencil(3, 0, output_cgra_s1_r_x, output_cgra_s1_r_y)*input_cgra.stencil(3, (output_cgra_s1_r_x + output_cgra_s1_x), (output_cgra_s1_r_y + output_cgra_s1_y))) + (kernel_cgra.stencil(2, 0, output_cgra_s1_r_x, output_cgra_s1_r_y)*input_cgra.stencil(2, (output_cgra_s1_r_x + output_cgra_s1_x), (output_cgra_s1_r_y + output_cgra_s1_y)))))))
  auto hcompute_output_cgra_stencil_1 = output_cgra_s1_x->add_op("op_hcompute_output_cgra_stencil_1");
  hcompute_output_cgra_stencil_1->add_function("hcompute_output_cgra_stencil_1");
  hcompute_output_cgra_stencil_1->add_load("input_cgra_stencil", "(output_cgra_s1_r_y + output_cgra_s1_y)", "(output_cgra_s1_r_x + output_cgra_s1_x)", "0");
  hcompute_output_cgra_stencil_1->add_load("input_cgra_stencil", "(output_cgra_s1_r_y + output_cgra_s1_y)", "(output_cgra_s1_r_x + output_cgra_s1_x)", "1");
  hcompute_output_cgra_stencil_1->add_load("input_cgra_stencil", "(output_cgra_s1_r_y + output_cgra_s1_y)", "(output_cgra_s1_r_x + output_cgra_s1_x)", "3");
  hcompute_output_cgra_stencil_1->add_load("input_cgra_stencil", "(output_cgra_s1_r_y + output_cgra_s1_y)", "(output_cgra_s1_r_x + output_cgra_s1_x)", "2");
  hcompute_output_cgra_stencil_1->add_load("kernel_cgra_stencil", "output_cgra_s1_r_y", "output_cgra_s1_r_x", "0", "0");
  hcompute_output_cgra_stencil_1->add_load("kernel_cgra_stencil", "output_cgra_s1_r_y", "output_cgra_s1_r_x", "0", "1");
  hcompute_output_cgra_stencil_1->add_load("kernel_cgra_stencil", "output_cgra_s1_r_y", "output_cgra_s1_r_x", "0", "3");
  hcompute_output_cgra_stencil_1->add_load("kernel_cgra_stencil", "output_cgra_s1_r_y", "output_cgra_s1_r_x", "0", "2");
  hcompute_output_cgra_stencil_1->add_load("output_cgra_stencil", "output_cgra_s1_y", "output_cgra_s1_x", "0");
  hcompute_output_cgra_stencil_1->add_store("output_cgra_stencil", "output_cgra_s1_y", "output_cgra_s1_x", "0");

//store is: output_cgra.stencil(1, output_cgra_s1_x, output_cgra_s1_y) = ((kernel_cgra.stencil(0, 1, output_cgra_s1_r_x, output_cgra_s1_r_y)*input_cgra.stencil(0, (output_cgra_s1_r_x + output_cgra_s1_x), (output_cgra_s1_r_y + output_cgra_s1_y))) + (output_cgra.stencil(1, output_cgra_s1_x, output_cgra_s1_y) + ((kernel_cgra.stencil(1, 1, output_cgra_s1_r_x, output_cgra_s1_r_y)*input_cgra.stencil(1, (output_cgra_s1_r_x + output_cgra_s1_x), (output_cgra_s1_r_y + output_cgra_s1_y))) + ((kernel_cgra.stencil(3, 1, output_cgra_s1_r_x, output_cgra_s1_r_y)*input_cgra.stencil(3, (output_cgra_s1_r_x + output_cgra_s1_x), (output_cgra_s1_r_y + output_cgra_s1_y))) + (kernel_cgra.stencil(2, 1, output_cgra_s1_r_x, output_cgra_s1_r_y)*input_cgra.stencil(2, (output_cgra_s1_r_x + output_cgra_s1_x), (output_cgra_s1_r_y + output_cgra_s1_y)))))))
  auto hcompute_output_cgra_stencil_2 = output_cgra_s1_x->add_op("op_hcompute_output_cgra_stencil_2");
  hcompute_output_cgra_stencil_2->add_function("hcompute_output_cgra_stencil_2");
  hcompute_output_cgra_stencil_2->add_load("input_cgra_stencil", "(output_cgra_s1_r_y + output_cgra_s1_y)", "(output_cgra_s1_r_x + output_cgra_s1_x)", "0");
  hcompute_output_cgra_stencil_2->add_load("input_cgra_stencil", "(output_cgra_s1_r_y + output_cgra_s1_y)", "(output_cgra_s1_r_x + output_cgra_s1_x)", "1");
  hcompute_output_cgra_stencil_2->add_load("input_cgra_stencil", "(output_cgra_s1_r_y + output_cgra_s1_y)", "(output_cgra_s1_r_x + output_cgra_s1_x)", "3");
  hcompute_output_cgra_stencil_2->add_load("input_cgra_stencil", "(output_cgra_s1_r_y + output_cgra_s1_y)", "(output_cgra_s1_r_x + output_cgra_s1_x)", "2");
  hcompute_output_cgra_stencil_2->add_load("kernel_cgra_stencil", "output_cgra_s1_r_y", "output_cgra_s1_r_x", "1", "0");
  hcompute_output_cgra_stencil_2->add_load("kernel_cgra_stencil", "output_cgra_s1_r_y", "output_cgra_s1_r_x", "1", "1");
  hcompute_output_cgra_stencil_2->add_load("kernel_cgra_stencil", "output_cgra_s1_r_y", "output_cgra_s1_r_x", "1", "3");
  hcompute_output_cgra_stencil_2->add_load("kernel_cgra_stencil", "output_cgra_s1_r_y", "output_cgra_s1_r_x", "1", "2");
  hcompute_output_cgra_stencil_2->add_load("output_cgra_stencil", "output_cgra_s1_y", "output_cgra_s1_x", "1");
  hcompute_output_cgra_stencil_2->add_store("output_cgra_stencil", "output_cgra_s1_y", "output_cgra_s1_x", "1");

//store is: output_cgra.stencil(2, output_cgra_s1_x, output_cgra_s1_y) = ((kernel_cgra.stencil(0, 2, output_cgra_s1_r_x, output_cgra_s1_r_y)*input_cgra.stencil(0, (output_cgra_s1_r_x + output_cgra_s1_x), (output_cgra_s1_r_y + output_cgra_s1_y))) + (output_cgra.stencil(2, output_cgra_s1_x, output_cgra_s1_y) + ((kernel_cgra.stencil(1, 2, output_cgra_s1_r_x, output_cgra_s1_r_y)*input_cgra.stencil(1, (output_cgra_s1_r_x + output_cgra_s1_x), (output_cgra_s1_r_y + output_cgra_s1_y))) + ((kernel_cgra.stencil(3, 2, output_cgra_s1_r_x, output_cgra_s1_r_y)*input_cgra.stencil(3, (output_cgra_s1_r_x + output_cgra_s1_x), (output_cgra_s1_r_y + output_cgra_s1_y))) + (kernel_cgra.stencil(2, 2, output_cgra_s1_r_x, output_cgra_s1_r_y)*input_cgra.stencil(2, (output_cgra_s1_r_x + output_cgra_s1_x), (output_cgra_s1_r_y + output_cgra_s1_y)))))))
  auto hcompute_output_cgra_stencil_3 = output_cgra_s1_x->add_op("op_hcompute_output_cgra_stencil_3");
  hcompute_output_cgra_stencil_3->add_function("hcompute_output_cgra_stencil_3");
  hcompute_output_cgra_stencil_3->add_load("input_cgra_stencil", "(output_cgra_s1_r_y + output_cgra_s1_y)", "(output_cgra_s1_r_x + output_cgra_s1_x)", "1");
  hcompute_output_cgra_stencil_3->add_load("input_cgra_stencil", "(output_cgra_s1_r_y + output_cgra_s1_y)", "(output_cgra_s1_r_x + output_cgra_s1_x)", "3");
  hcompute_output_cgra_stencil_3->add_load("input_cgra_stencil", "(output_cgra_s1_r_y + output_cgra_s1_y)", "(output_cgra_s1_r_x + output_cgra_s1_x)", "2");
  hcompute_output_cgra_stencil_3->add_load("input_cgra_stencil", "(output_cgra_s1_r_y + output_cgra_s1_y)", "(output_cgra_s1_r_x + output_cgra_s1_x)", "0");
  hcompute_output_cgra_stencil_3->add_load("kernel_cgra_stencil", "output_cgra_s1_r_y", "output_cgra_s1_r_x", "2", "1");
  hcompute_output_cgra_stencil_3->add_load("kernel_cgra_stencil", "output_cgra_s1_r_y", "output_cgra_s1_r_x", "2", "3");
  hcompute_output_cgra_stencil_3->add_load("kernel_cgra_stencil", "output_cgra_s1_r_y", "output_cgra_s1_r_x", "2", "2");
  hcompute_output_cgra_stencil_3->add_load("kernel_cgra_stencil", "output_cgra_s1_r_y", "output_cgra_s1_r_x", "2", "0");
  hcompute_output_cgra_stencil_3->add_load("output_cgra_stencil", "output_cgra_s1_y", "output_cgra_s1_x", "2");
  hcompute_output_cgra_stencil_3->add_store("output_cgra_stencil", "output_cgra_s1_y", "output_cgra_s1_x", "2");

//store is: output_cgra.stencil(3, output_cgra_s1_x, output_cgra_s1_y) = ((kernel_cgra.stencil(0, 3, output_cgra_s1_r_x, output_cgra_s1_r_y)*input_cgra.stencil(0, (output_cgra_s1_r_x + output_cgra_s1_x), (output_cgra_s1_r_y + output_cgra_s1_y))) + (output_cgra.stencil(3, output_cgra_s1_x, output_cgra_s1_y) + ((kernel_cgra.stencil(1, 3, output_cgra_s1_r_x, output_cgra_s1_r_y)*input_cgra.stencil(1, (output_cgra_s1_r_x + output_cgra_s1_x), (output_cgra_s1_r_y + output_cgra_s1_y))) + ((kernel_cgra.stencil(3, 3, output_cgra_s1_r_x, output_cgra_s1_r_y)*input_cgra.stencil(3, (output_cgra_s1_r_x + output_cgra_s1_x), (output_cgra_s1_r_y + output_cgra_s1_y))) + (kernel_cgra.stencil(2, 3, output_cgra_s1_r_x, output_cgra_s1_r_y)*input_cgra.stencil(2, (output_cgra_s1_r_x + output_cgra_s1_x), (output_cgra_s1_r_y + output_cgra_s1_y)))))))
  auto hcompute_output_cgra_stencil_4 = output_cgra_s1_x->add_op("op_hcompute_output_cgra_stencil_4");
  hcompute_output_cgra_stencil_4->add_function("hcompute_output_cgra_stencil_4");
  hcompute_output_cgra_stencil_4->add_load("input_cgra_stencil", "(output_cgra_s1_r_y + output_cgra_s1_y)", "(output_cgra_s1_r_x + output_cgra_s1_x)", "0");
  hcompute_output_cgra_stencil_4->add_load("input_cgra_stencil", "(output_cgra_s1_r_y + output_cgra_s1_y)", "(output_cgra_s1_r_x + output_cgra_s1_x)", "1");
  hcompute_output_cgra_stencil_4->add_load("input_cgra_stencil", "(output_cgra_s1_r_y + output_cgra_s1_y)", "(output_cgra_s1_r_x + output_cgra_s1_x)", "3");
  hcompute_output_cgra_stencil_4->add_load("input_cgra_stencil", "(output_cgra_s1_r_y + output_cgra_s1_y)", "(output_cgra_s1_r_x + output_cgra_s1_x)", "2");
  hcompute_output_cgra_stencil_4->add_load("kernel_cgra_stencil", "output_cgra_s1_r_y", "output_cgra_s1_r_x", "3", "0");
  hcompute_output_cgra_stencil_4->add_load("kernel_cgra_stencil", "output_cgra_s1_r_y", "output_cgra_s1_r_x", "3", "1");
  hcompute_output_cgra_stencil_4->add_load("kernel_cgra_stencil", "output_cgra_s1_r_y", "output_cgra_s1_r_x", "3", "3");
  hcompute_output_cgra_stencil_4->add_load("kernel_cgra_stencil", "output_cgra_s1_r_y", "output_cgra_s1_r_x", "3", "2");
  hcompute_output_cgra_stencil_4->add_load("output_cgra_stencil", "output_cgra_s1_y", "output_cgra_s1_x", "3");
  hcompute_output_cgra_stencil_4->add_store("output_cgra_stencil", "output_cgra_s1_y", "output_cgra_s1_x", "3");

//consuming output_cgra.stencil
  auto output_glb_s0_y_y_cgra = output_glb_s0_x_x_glb->add_loop("output_glb_s0_y_y_cgra", 0, 28);
  auto output_glb_s0_x_x_cgra = output_glb_s0_y_y_cgra->add_loop("output_glb_s0_x_x_cgra", 0, 28);
  auto output_glb_s0_w_w_cgra = output_glb_s0_x_x_cgra->add_loop("output_glb_s0_w_w_cgra", 0, 4);

//store is: output_glb.stencil(((output_glb_s0_w_w_glb*4) + output_glb_s0_w_w_cgra), ((output_glb_s0_x_x_glb*28) + output_glb_s0_x_x_cgra), ((output_glb_s0_y_y_glb*28) + output_glb_s0_y_y_cgra)) = output_cgra.stencil(output_glb_s0_w_w_cgra, output_glb_s0_x_x_cgra, output_glb_s0_y_y_cgra)
  auto hcompute_output_glb_stencil = output_glb_s0_w_w_cgra->add_op("op_hcompute_output_glb_stencil");
  hcompute_output_glb_stencil->add_function("hcompute_output_glb_stencil");
  hcompute_output_glb_stencil->add_load("output_cgra_stencil", "output_glb_s0_y_y_cgra", "output_glb_s0_x_x_cgra", "output_glb_s0_w_w_cgra");
  prg.buffer_port_widths["output_glb_stencil"] = 16;
  hcompute_output_glb_stencil->add_store("output_glb_stencil", "((output_glb_s0_y_y_glb*28) + output_glb_s0_y_y_cgra)", "((output_glb_s0_x_x_glb*28) + output_glb_s0_x_x_cgra)", "((output_glb_s0_w_w_glb*4) + output_glb_s0_w_w_cgra)");

//consuming output_glb.stencil
  auto hw_output_s0_w = prg.add_loop("hw_output_s0_w", 0, 16);
  auto hw_output_s0_y_yi = hw_output_s0_w->add_loop("hw_output_s0_y_yi", 0, 56);
  auto hw_output_s0_x_xi = hw_output_s0_y_yi->add_loop("hw_output_s0_x_xi", 0, 56);

//store is: hw_output.stencil(hw_output_s0_w, hw_output_s0_x_xi, hw_output_s0_y_yi) = output_glb.stencil(hw_output_s0_w, hw_output_s0_x_xi, hw_output_s0_y_yi)
  auto hcompute_hw_output_stencil = hw_output_s0_x_xi->add_op("op_hcompute_hw_output_stencil");
  hcompute_hw_output_stencil->add_function("hcompute_hw_output_stencil");
  hcompute_hw_output_stencil->add_load("output_glb_stencil", "hw_output_s0_y_yi", "hw_output_s0_x_xi", "hw_output_s0_w");
  hcompute_hw_output_stencil->add_store("hw_output_stencil", "hw_output_s0_y_yi", "hw_output_s0_x_xi", "hw_output_s0_w");

  return prg;
}


