// Copyright lowRISC contributors (OpenTitan project).
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0
{
  name: "${module_instance_name}"
  import_testplans: ["${module_instance_name}_sec_cm_testplan.hjson"]
  testpoints: [
    {
      name: riscv_arithmetic_basic_test
      desc: '''
            Sanity test for basic RV32I instructions
              - Generate instruction binary and load to memory model
              - Only arithmetic instructions are generated
              - Assert fetch enable to start the core execution
              - Terminate the test after ecall instructinon in detected
              - All instruction results are compared against spike simulation'''
      stage: V1
      tests: ["riscv_arithmetic_basic_test"]
    }
    {
      name: riscv_rand_instr_test
      desc: '''
            Fully randomized RV32IMC instructions
              - Including all types of instructions
              - forward/backward branches
              - directed sequence for various hazard conditions
              - mixed load/store instructions
              - random jump instructions
              - fence instructions (treated as nop)
              - mret instruction
              - corner cases for each instruction (overflow, underflow, div by zero etc.)'''
      stage: V1
      tests: ["riscv_rand_instr_test"]
    }
    {
      name: riscv_machine_mode_rand_test
      desc: '''
            Fully randomized RV32IMC instructions
              - boot into Machine Mode'''
      stage: V1
      tests: ["riscv_machine_mode_rand_test"]
    }
    {
      name: riscv_rand_jump_test
      desc: '''
            Stresses I-Fetch operation:
              - Generates a large number of subprograms and frequently jumps between them'''
      stage: V1
      tests: ["riscv_rand_jump_test"]
    }
    {
      name: riscv_jump_stress_test
      desc: '''
            Stresses I-Fetch operation:
              - Generates sequences of many back-to-back jump instructions'''
      stage: V1
      tests: ["riscv_jump_stress_test"]
    }
    {
      name: riscv_loop_test
      desc: '''
            Generate directed sequences of loops to test branch operations'''
      stage: V1
      tests: ["riscv_loop_test"]
    }
    {
      name: riscv_mmu_stress_test
      desc: '''
            Create various load/store patterns to stress LSU
              - load/store from random addresses
              - load/store from dense or sparse locations
              - back-to-back load/store instructions
              - cover all byte/half-word/word load/store instructions
              - load/store with hazard conditions'''
      stage: V1
      tests: ["riscv_mmu_stress_test"]
    }
    {
      name: riscv_unaligned_load_store_test
      desc: '''
            Random mis-aligned half-word/word load/store instruction
            Expect the core finishes the load/store properly with no exception raised'''
      stage: V1
      tests: ["riscv_unaligned_load_store_test"]
    }
    {
      name: riscv_mem_error_test
      desc: '''
            Randomly assert error bit for instruction fetch and load/store.
            Expect the core to raise memory access fault exception
            Verify the fault load instruction won't modify GPR
            Verify the context of the exception is captured correctly in privileged CSRs
              - mstatus, mcause, mepc'''
      stage: V1
      tests: ["riscv_mem_error_test"]
    }
    {
      name: riscv_illegal_instr_test
      desc: '''
            Randomly inject below illegal instructions in the instruction stream
              - Instructions which not belong to RV32IMC
              - Reserved instructions as specified in RISC-V user mode spec
              - CSR instruction to not implemented CSR
              - Execute S/U mode instructions in machine mode
            Verify the illlegal instruction exception is raised and the context is captured correctly in privieged mode CSRs
              - mstatus, mcause, mepc
            Verify the core can resume execution after returning from trap handling'''
      stage: V1
      tests: ["riscv_illegal_instr_test"]
    }
    {
      name: riscv_hint_instr_test
      desc: '''
            Randomly inject HINT instructions as specified in RISC-V user mode specification 5.4
            Verify the core execute it as NOP and no exception is raised'''
      stage: V1
      tests: ["riscv_hint_instr_test"]
    }
    {
      name: riscv_ebreak_test
      desc: '''
            Generate random instructions including ebreak, expect core to raise ebreak exception'''
      stage: V1
      tests: ["riscv_ebreak_test"]
    }
    {
      name: riscv_debug_basic_test
      desc: '''
            Randomly assert debug_req_i signal
              - in debug_rom, insert normal random instruction sequence
                excluding control transfer instructions
              - verify wfi instructions act as NOPs
              - ensure normal execution finishes successfully'''
      stage: V1
      tests: ["riscv_debug_basic_test"]
    }
    {
      name: riscv_debug_stress_test
      desc: '''
            Randomly assert debug_req signal
              - debug rom will consist of just a dret instructions, so any switches into
              debug mode will immediately return
              - will use this jointly with full instruction checking with spike'''
      stage: V1
      tests: ["riscv_debug_stress_test"]
    }
    {
      name: riscv_debug_branch_jump_test
      desc: '''
            Randomly assert debug_req_i signal
              - in debug_rom, insert random instruction sequence,
                including control transfer instructions and multiple subprograms to
                jump between'''
      stage: V1
      tests: ["riscv_debug_branch_jump_test"]
    }
    {
      name: riscv_debug_interrupt_test
      desc: '''
            Randomly assert debug_req_i signal
              - in debug_rom, insert random instruction sequence
              - randomly toggle interrupts during debug code
              - verify that execution of debug_rom continues as normal, interrupts are
                ignored'''
      stage: V2
      tests: ["riscv_debug_interrupt_test"]
    }
    {
      name: riscv_debug_illegal_instr_test
      desc: '''
            - insert illegal instruction into debug rom to trigger internal exception
            - verify that no registers have been updated
            - verify that execution of debug rom ends'''
      stage: V2
      tests: ["riscv_debug_illegal_instr_test"]
    }
    {
      name: riscv_debug_wfi_test
      desc: '''
            Insert random generation of wfi instructions
              - while core is in wfi sleep state, assert debug request
              - verify core jumps to debug mode
              - debug rom consists of random generation instructions'''
      stage: V1
      tests: ["riscv_debug_wfi_test"]
    }
    {
      name: riscv_dret_test
      desc: '''
            Randomly insert dret instructions into M mode instructions
              - verify that ibex treats them as illegal instructions'''
      stage: V1
      tests: ["riscv_dret_test"]
    }
    {
      name: riscv_debug_ebreak_test
      desc: '''
            - insert ebreak instructions in M mode code, should be handled normally
            - assert debug request
            - in debug mode, insert ebreak instruction, verify that ibex re-enter debug mode'''
      stage: V1
      tests: ["riscv_debug_ebreak_test"]
    }
    {
      name: riscv_debug_ebreakmu_test
      desc: '''
            - set dcsr.ebreakm/u at begining of test
            - randomly insert ebreak instructions in generated code
            - boot randomly into either M/U modes
            - verify that ebreak instructions (in either mode) now enter debug mode'''
      stage: V1
      tests: ["riscv_debug_ebreakmu_test"]
    }
    {
      name: riscv_debug_single_step_test
      desc: '''
            Randomly assert debug_req_i signal to force core into debug mode.
              - in debug_rom, set dcsr.step
              - after exiting debug mode, core will execute one instruction and re-enter
                debug mode'''
      stage: V1
      tests: ["riscv_debug_single_step_test"]
    }
    {
      name: riscv_debug_csr_entry_test
      desc: '''
            Inject debug stimulus during CSR modification operations,
            core should jump to debug mode and return without any issues'''
      stage: V1
      tests: ["riscv_debug_csr_entry_test"]
    }
    {
      name: riscv_irq_in_debug_mode_test
      desc: '''
            Randomly assert interrupt lines while the core is in debug mode execution,
            verify that these interrupts are all completely ignored'''
      stage: V1
      tests: ["riscv_irq_in_debug_mode_test"]
    }
    {
      name: riscv_single_interrupt_test
      desc: '''
            Randomly assert a single interrupt line during program execution, core
            should jump to proper handler'''
      stage: V1
      tests: ["riscv_single_interrupt_test"]
    }
    {
      name: riscv_multiple_interrupt_test
      desc: '''
            Randomly assert multiple interrupt lines during program execution,
            core should jump to handler of highest priority interrupt'''
      stage: V1
      tests: ["riscv_multiple_interrupt_test"]
    }
    {
      name: riscv_interrupt_wfi_test
      desc: '''
            Randomly assert interrupt lines while the core is sleeping due to a WFI instruction'''
      stage: V1
      tests: ["riscv_interrupt_wfi_test"]
    }
    {
      name: riscv_interrupt_csr_test
      desc: '''
            Randomly assert interrupt lines during CSR modification operations,
            core should jump to proper handler'''
      stage: V1
      tests: ["riscv_interrupt_csr_test"]
    }
    {
      name: riscv_interrupt_nested_test
      desc: '''
            - randomly assert interrupt lines during program execution
            - verify that jumps to interrupt handler
            - trigger another interrupt during first intr_handler execution
            - verify that core takes this nested interrupt and handles it, then restores previous interrupt
              state and finishes executing first interrupt handler - ibex implements nonstandard MSTACK csrs to
              save/restore state in case of nested interrupt'''
      stage: V2
      tests: ["riscv_interrupt_nested_test"]
    }
    {
      name: riscv_csr_test
      desc: '''
            Perform all CSR instructions to implemented privileged CSR
            Verify the reset value of the privileged CSR
            Verify WARL field can be upated properly'''
      stage: V1
      tests: ["riscv_csr_test"]
    }
    {
      name: riscv_reset_test
      desc: '''
            Reset the core a random number of times during program execution, core
            should jump back to start address and re-enter the program.
            Ensure to flush all testbench state to prevent contamination after reset.'''
      stage: V1
      tests: ["riscv_reset_test"]
    }
    {
      name: riscv_perf_counter_test
      desc: '''
            Run random instruction test, and dump performance counters for checking'''
      stage: V1
      tests: ["riscv_perf_counter_test"]
    }
    {
      name: riscv_rv32im_instr_test
      desc: '''
            Specify RV32IM architecture to compiler to generate target specific
            instructions (no compressed instructions).'''
      stage: V1
      tests: ["riscv_rv32im_instr_test"]
    }
    {
      name: riscv_user_mode_rand_test
      desc: '''
            Fully randomized RV32IMC instructions, boot in U-mode,
            test should finish successfully
              - includes all types of instructions
              - no external debug/irq stimulus, exceptions, security aspects are checked here'''
      stage: V2
      tests: ["riscv_user_mode_rand_test"]
    }
    {
      name: riscv_umode_tw_test
      desc: '''
            Set mstatus.tw, and enable random generation of WFI instructions.
            Upon encountering WFI in U-mode, core should trap to M-mode
            illegal instruction exception handler.'''
      stage: V2
      tests: ["riscv_umode_tw_test"]
    }
    {
      name: riscv_invalid_csr_test
      desc: '''
            Boot core into a random privilege mode and generate accesses to higher level CSRs,
            expect to throw an illegal instruction exception for each of these accesses.'''
      stage: V2
      tests: ["riscv_invalid_csr_test"]
    }
    {
      // TODO: Need to add testplan entries for PMP and nested trap tests
    }
  ]
}
